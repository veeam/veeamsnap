// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include "log.h"
#include "queue_spinlocking.h"
#include <linux/time.h>

#define SECTION "logging   "
#include "log_format.h"

#define LOGFILE
#define LOGGING_CHECK_RENEW "CHECK_RENEW"
#define LOGGING_MODE_SYS    "MODE_SYS"
#define LOGGING_MODE_FILE   "MODE_FILE"

#define MAX_LOGLINE_SIZE 256
#define MAX_FILENAME_SIZE 256
#define MAX_TIMESTRING_SIZE 256
/*

void log_dump( void* p, size_t size )
{
    unsigned char* pch = (unsigned char*)p;
    int pos = 0;
    char str[16*3+1+1];
    char* pstr = (char*)str;

    while(pos<size) {
        sprintf(pstr, "%02x ",pch[pos++]);
        pstr+=3;

        if ( 0==(pos %16) ){
            pr_err( "%s\n", str );
            pstr = str;
        }
    }
    if ( 0!=(pos %16) ){
        pr_err( "%s\n", str );
    }
}
*/

typedef struct _logging_request_t
{
    queue_content_sl_t content;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
    struct timespec m_time;
#else
    struct timespec64 m_time;
#endif
    pid_t m_pid;
    const char* m_section;
    unsigned m_level;

    size_t m_len;
    char m_buff[];//must be last entry
}logging_request_t;

#define LOGGING_STATE_READY 0
#define LOGGING_STATE_ERROR 1
#define LOGGING_STATE_DONE 2


typedef struct _logging_t
{
    struct task_struct* m_rq_thread;

    wait_queue_head_t    m_new_rq_event;

    struct mutex m_lock;

    const char* m_logdir;
    unsigned long m_logmaxsize;
    struct file* m_filp;
    char m_filepath[MAX_FILENAME_SIZE];
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
    struct timespec m_modify_time;
#else
    struct timespec64 m_modify_time;
#endif
    volatile bool m_is_file_logging;
    volatile int m_state;

    queue_sl_t m_rq_proc_queue; //log request processing queue
}logging_t;

static logging_t g_logging;


static void _log_kernel( const char* section, const char* level_string, const char* s )
{
    switch (get_debuglogging( )){
    case VEEAM_LL_HI: pr_err( "%s:%s | %s | %s\n", MODULE_NAME, section, level_string, s ); break;
    case VEEAM_LL_LO: pr_info( "%s:%s | %s | %s\n", MODULE_NAME, section, level_string, s ); break;
    default:          pr_warn( "%s:%s | %s | %s\n", MODULE_NAME, section, level_string, s );
    }
}

static void _log_kernel_tr( const char* section, const char* s )
{
    switch (get_debuglogging( )){
    case VEEAM_LL_HI: pr_err( "%s:%s %s\n", MODULE_NAME, section, s ); break;
    case VEEAM_LL_LO: pr_info( "%s:%s %s\n", MODULE_NAME, section, s ); break;
    default:          pr_warn( "%s:%s %s\n", MODULE_NAME, section, s );
    }
}

static int __logging_filp_open( logging_t* logging )
{
    int res = SUCCESS;
    struct file* filp = NULL;

    if (logging->m_filp != NULL) //already opened
        return res;

    filp = filp_open(logging->m_filepath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (IS_ERR(filp)) {
        res = PTR_ERR(filp);
        //pr_err("ERR %s:%s Failed to open file. res=%d\n", MODULE_NAME, SECTION, res);
    }
    else
        logging->m_filp = filp;

    return res;
}
static void __logging_filp_close( logging_t* logging )
{
    if (logging->m_filp != NULL)
    {
        filp_close( logging->m_filp, NULL );

        logging->m_filp = NULL;
    }
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static ssize_t __logging_kernel_write( struct file *file, const void *buf, size_t count, loff_t *pos )
{
    mm_segment_t old_fs;
    ssize_t res;

    old_fs = get_fs( );
    set_fs( get_ds( ) );
    /* The cast to a user pointer is valid due to the set_fs() */
    res = vfs_write( file, (__force const char __user *)buf, count, pos );
    set_fs( old_fs );

    return res;
}
#endif

static int __logging_filp_write( logging_t* logging, const char* buff, const size_t len )
{
    int res = SUCCESS;
    if (logging == NULL){
        pr_err( "%s:%s pointer logging is NULL\n", MODULE_NAME, SECTION );
        return -EINVAL;
    }
    if (logging->m_filp == NULL){
        //pr_err( "%s:%s pointer m_filp is NULL\n", MODULE_NAME, SECTION );
        return -EINVAL;
    }

    if (buff == NULL){
        pr_err( "%s:%s buffer is NULL\n", MODULE_NAME, SECTION );
        return -EINVAL;
    }

    {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        ssize_t result = __logging_kernel_write( logging->m_filp, buff, len, &logging->m_filp->f_pos );
#else
        ssize_t result = kernel_write( logging->m_filp, buff, len, &logging->m_filp->f_pos );
#endif
        if (result < 0)
            res = result;
    }

    return res;
}

static int __logging_filp_get_size( logging_t* logging, loff_t* p_size )
{
    if ((logging != NULL) && (logging->m_filp != NULL)){
        //vfs_llseek( logging->m_filp, 0,  )
        //noop_llseek( )
        //no_llseek
        //generic_file_llseek
        *p_size = i_size_read( logging->m_filp->f_path.dentry->d_inode );
        return SUCCESS;
    }
    else
        return -EINVAL;
}

static int _logging_filename_create( logging_t* logging )
{
    if (logging->m_logdir == NULL)
        return -ENOTDIR;
    else
    {
        struct tm modify_time;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
        getnstimeofday(&logging->m_modify_time);
        time_to_tm(logging->m_modify_time.tv_sec, 0, &modify_time);
#else
        ktime_get_real_ts64(&logging->m_modify_time);
        time64_to_tm(logging->m_modify_time.tv_sec, 0, &modify_time);
#endif
        snprintf(logging->m_filepath, sizeof(logging->m_filepath), "%s/%s-%04ld%02d%02d.log",
            logging->m_logdir,
            MODULE_NAME,
            modify_time.tm_year + 1900,
            modify_time.tm_mon + 1,
            modify_time.tm_mday);
    }
    return SUCCESS;
}

static int _logging_open( logging_t* logging )
{
    int res = SUCCESS;

    mutex_lock( &logging->m_lock );
    res = __logging_filp_open( logging );
    mutex_unlock( &logging->m_lock );

    return res;
}

static void _logging_close( logging_t* logging )
{
    mutex_lock( &logging->m_lock );
    __logging_filp_close( logging );
    mutex_unlock( &logging->m_lock );
}

static void _logging_check_renew( logging_t* logging )
{
    loff_t sz = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
    struct timespec _time;
#else
    struct timespec64 _time;
#endif
    struct tm current_time;
    struct tm modify_time;

    __logging_filp_get_size( logging, &sz );
    if (sz < logging->m_logmaxsize)
        return;
    log_tr_lld( "Log file size: ", sz );

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
    getnstimeofday(&_time);
    time_to_tm( _time.tv_sec, 0, &current_time );
    time_to_tm( logging->m_modify_time.tv_sec, 0, &modify_time );
#else
    ktime_get_real_ts64(&_time);
    time64_to_tm(_time.tv_sec, 0, &current_time);
    time64_to_tm(logging->m_modify_time.tv_sec, 0, &modify_time);
#endif
    if (
        (modify_time.tm_mday == current_time.tm_mday) &&
        (modify_time.tm_mon == current_time.tm_mon) &&
        (modify_time.tm_year == current_time.tm_year))
    {
        return;
    }

    {// day changed
        int res;

        res = _logging_filename_create(logging);
        if (res != SUCCESS)
        {
            pr_warn("WAARN %s:%s Logging fo file will be not performed\n", MODULE_NAME, SECTION);
            return;
        }
        mutex_lock( &logging->m_lock );

        __logging_filp_close( logging );
        res = __logging_filp_open( logging );

        mutex_unlock( &logging->m_lock );

        if (res != SUCCESS){
            pr_err( "ERR %s:%s Failed to create file [%s]\n", MODULE_NAME, SECTION, logging->m_filepath );
        }
    }
}

static int _logging_waiting( logging_t* logging )
{
    int result;

    if (!queue_sl_empty( logging->m_rq_proc_queue ))
        return SUCCESS;

    //return wait_event_interruptible( logging->m_new_rq_event, (!queue_sl_empty( logging->m_rq_proc_queue ) || kthread_should_stop( )) );

    result = wait_event_interruptible_timeout(logging->m_new_rq_event, (!queue_sl_empty(logging->m_rq_proc_queue) || kthread_should_stop()), 10 * HZ); //HZ - ticks per seconds
    if (0 == result)
        return -ETIME; //timeout, condition evaluated to false
    if (result > 0)
        return SUCCESS; //condition evaluated to true

    return result;
}

static inline void _log_prefix( char* timebuff, const size_t buffsize, struct tm* _time, logging_request_t* rq, const char* level_text )
{
    snprintf( timebuff, buffsize, "[%02d.%02d.%04ld %02d:%02d:%02d-%06ld] <%d> %s | %s",
        _time->tm_mday,
        _time->tm_mon + 1,
        _time->tm_year + 1900,

        _time->tm_hour,
        _time->tm_min,
        _time->tm_sec,

        rq->m_time.tv_nsec / 1000,

        rq->m_pid,
        rq->m_section,
        level_text
        );
}

static inline const char* _log_level_to_text( unsigned level )
{
    const char* level_text = "";

    if (level == LOGGING_LEVEL_ERR)
        level_text = "ERR | ";
    else  if (level == LOGGING_LEVEL_WRN)
        level_text = "WRN | ";
    if (level == LOGGING_LEVEL_TR){
        level_text = "";
    }

    return level_text;
}


static int _logging_process( logging_t* logging )
{
    int res = SUCCESS;
    logging_request_t* rq;

    while (NULL != (rq = (logging_request_t*)queue_sl_get_first( &logging->m_rq_proc_queue )) )
    {
        if (rq->m_len == 0){//command received
            if (rq->m_level == LOGGING_LEVEL_CMD){
                if (0 == strcmp( rq->m_section, LOGGING_CHECK_RENEW )){
#ifdef LOGFILE
                    if (logging->m_is_file_logging)
                        _logging_check_renew(logging);
#endif
                }
                else if (0 == strcmp(rq->m_section, LOGGING_MODE_SYS)) {
                    logging->m_is_file_logging = false;
                    _logging_close(logging);
                }
                else if (0 == strcmp(rq->m_section, LOGGING_MODE_FILE)) {
                    logging->m_is_file_logging = true;
                    _logging_open(logging);
                }
            }
        }
        else{

            struct tm _time;
            char timebuff[MAX_TIMESTRING_SIZE];
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
            time_to_tm( rq->m_time.tv_sec, 0, &_time );
#else
            time64_to_tm(rq->m_time.tv_sec, 0, &_time);
#endif
            _log_prefix( timebuff, sizeof( timebuff ), &_time, rq, _log_level_to_text( rq->m_level ) );

#ifdef LOGFILE
            mutex_lock( &logging->m_lock );
            res = __logging_filp_write( logging, timebuff, strlen( timebuff ) );
            if (res == SUCCESS){
                rq->m_buff[rq->m_len - 1] = '\n';
                res = __logging_filp_write( logging, rq->m_buff, rq->m_len );
            }
            mutex_unlock( &logging->m_lock );
#else
            res = -ENOTTY;
#endif
            if (SUCCESS != res){
                //logging->m_state = LOGGING_STATE_ERROR;

                if (rq->m_level == LOGGING_LEVEL_TR){
                    rq->m_buff[rq->m_len - 1] = '\0';
                    _log_kernel_tr( rq->m_section, rq->m_buff );
                }
            }
        }
        queue_content_sl_free( &rq->content );
    }
    return res;
}

int _logging_thread( void *data )
{
    int result = SUCCESS;
    logging_t* logging = (logging_t*)data;
#ifdef LOGFILE
    logging->m_is_file_logging = false;
    {
        int res = SUCCESS;

        res = _logging_filename_create(logging);
        if (res == SUCCESS) {
            logging->m_is_file_logging = true;
            _logging_open(logging);
        }
        else
            pr_warn("WARN %s:%s Logging to file will be not performed\n", MODULE_NAME, SECTION);
    }
#endif
    //priority
    //set_user_nice( current, -20 ); //MIN_NICE

    while (!kthread_should_stop( ))
    {
        int res = _logging_waiting( logging );
        if (res == SUCCESS) {
#ifdef LOGFILE
            if (logging->m_is_file_logging)
                _logging_open(logging);
#endif
            result = _logging_process(logging);
        }
        else if (res == -ETIME) {
#ifdef LOGFILE
            _logging_close(logging);
            continue;
#endif
        }
        else{
            result = res;
            break;
        }

        //if (result != SUCCESS)
        //    break;

        schedule( );
    }

    queue_sl_active( &logging->m_rq_proc_queue, false );
    result = _logging_process( logging );

    //if (queue_sl_is_unactive( logging->rq_proc_queue )){
    //    wake_up_interruptible( &logging->rq_complete_event );
    //}
#ifdef LOGFILE
    _logging_close( logging );
#endif
    if (result != SUCCESS)
        pr_err( "ERR %s:%s Logging thread complete with code %d\n", MODULE_NAME, SECTION, 0-result );

    return result;
}


int logging_init( const char* logdir, unsigned long logmaxsize )
{
    logging_t* logging = &g_logging;

    logging->m_rq_thread = NULL;
    init_waitqueue_head( &logging->m_new_rq_event );
    mutex_init( &logging->m_lock );
    logging->m_logdir = logdir;
    logging->m_logmaxsize = logmaxsize;
    memset(logging->m_filepath, 0, sizeof(logging->m_filepath));
    logging->m_filp = NULL;

    memset( &logging->m_modify_time, 0, sizeof(logging->m_modify_time) );
    logging->m_state = LOGGING_STATE_READY;

    {
        int res = queue_sl_init( &logging->m_rq_proc_queue, sizeof( logging_request_t ) );
        if (res != SUCCESS){
            pr_err( "ERR %s:%s Failed to initialize request processing queue\n", MODULE_NAME, SECTION);
            return res;
        }
    }

    {
        struct task_struct* task = kthread_create( _logging_thread, logging, "veeamsnap_log" );
        if (IS_ERR( task )) {
            pr_err( "ERR %s:%s Failed to create request processing thread\n", MODULE_NAME, SECTION);
            return PTR_ERR( task );
        }
        logging->m_rq_thread = task;
        wake_up_process( logging->m_rq_thread );
    }
    return SUCCESS;
}

void logging_done( void )
{
    logging_t* logging = &g_logging;

    log_tr( "Log process stop" );

    logging->m_state = LOGGING_STATE_DONE;
    if (logging->m_rq_thread != NULL){

        kthread_stop( logging->m_rq_thread );
        logging->m_rq_thread = NULL;
    }

    queue_sl_done( &logging->m_rq_proc_queue );
}

static int _logging_buffer( const char* section, const unsigned level, const char* buff, const size_t len )
{
    logging_t* logging = &g_logging;
    logging_request_t* rq = NULL;

    if (logging->m_state != LOGGING_STATE_READY)
        return -EINVAL;

    rq = ( logging_request_t* )queue_content_sl_new_opt_append( &logging->m_rq_proc_queue, GFP_NOIO, len + 1);
    if (NULL == rq){
        pr_err( "ERR %s:%s Cannot allocate memory for logging\n", MODULE_NAME, SECTION );
        return -ENOMEM;
    }

    rq->m_section = section;
    rq->m_level = level;
    rq->m_pid = get_current( )->pid;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
    getnstimeofday(&rq->m_time);
#else
    ktime_get_real_ts64(&rq->m_time);
#endif
    rq->m_len = len;
    if ((len != 0) && (buff != NULL)) {
        memcpy(rq->m_buff, buff, len);
        rq->m_len += 1;
    }

    if (SUCCESS == queue_sl_push_back( &logging->m_rq_proc_queue, &rq->content )){
        wake_up( &logging->m_new_rq_event );
        return SUCCESS;
    }
    else{
        logging->m_state = LOGGING_STATE_ERROR;

        pr_err( "ERR %s:%s Failed to add logging request to queue\n", MODULE_NAME, SECTION );
        queue_content_sl_free( &rq->content );

        //if (queue_sl_is_unactive( logging->rq_proc_queue )){
        //    wake_up_interruptible( &logging->rq_complete_event );
        //}
        return -EFAULT;
    }
}

void logging_renew_check( void )
{
    _logging_buffer( LOGGING_CHECK_RENEW, LOGGING_LEVEL_CMD, NULL, 0 );
}

void logging_mode_sys(void)
{
    _logging_buffer(LOGGING_MODE_SYS, LOGGING_LEVEL_CMD, NULL, 0);
}

void logging_mode_file(void)
{
    _logging_buffer(LOGGING_MODE_FILE, LOGGING_LEVEL_CMD, NULL, 0);
}

void logging_flush(void)
{
    logging_t* logging = &g_logging;
    while (0 != queue_sl_length(logging->m_rq_proc_queue))
        schedule();
}

static inline const char *log_level_string(const unsigned int level)
{
    switch (level) {
    case LOGGING_LEVEL_ERR:
        return "ERR";
    case LOGGING_LEVEL_WRN:
        return "WRN";
    default:
        return "INFO";
    }
}

void log_s( const char* section, const unsigned level, const char* s )
{
    if (level != LOGGING_LEVEL_TR)
        _log_kernel(section, log_level_string(level), s);

    if (irqs_disabled() || (preempt_count() > 0))
        goto fail;

    if (_logging_buffer(section, level, s, strlen(s)))
        goto fail;

    return;
fail:
    if (level == LOGGING_LEVEL_TR)
        _log_kernel_tr(section, s);
    return;
}

void log_s_s( const char* section, const unsigned level, const char* s1, const char* s2 )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s%s", s1, s2);
    log_s( section, level, _tmp );
}

void log_s_d( const char* section, const unsigned level, const char* s, const int d )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s%d", s, d );
    log_s( section, level, _tmp );
}

void log_s_ld( const char* section, const unsigned level, const char* s, const long d )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s%ld", s, d );
    log_s( section, level, _tmp );
}

void log_s_lld( const char* section, const unsigned level, const char* s, const long long d )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s%lld", s, d );
    log_s( section, level, _tmp );
}

void log_s_sz( const char* section, const unsigned level, const char* s, const size_t d )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s%lu", s, (unsigned long)d );
    log_s( section, level, _tmp );
}

void log_s_x( const char* section, const unsigned level, const char* s, const int d )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s0x%x", s, d );
    log_s( section, level, _tmp );
}

void log_s_lx( const char* section, const unsigned level, const char* s, const long d )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s0x%lx", s, d );
    log_s( section, level, _tmp );
}

void log_s_llx( const char* section, const unsigned level, const char* s, const long long d )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s0x%llx", s, d );
    log_s( section, level, _tmp );
}

void log_s_p( const char* section, const unsigned level, const char* s, const void* p )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s0x%p", s, p );
    log_s( section, level, _tmp );
}

void log_s_dev_id( const char* section, const unsigned level, const char* s, const int major, const int minor )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s[%d:%d]", s, major, minor );
    log_s( section, level, _tmp );
}

void log_s_uuid(const char* section, const unsigned level, const char* s, const veeam_uuid_t* uuid)
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s[%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x]", s, uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4], uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9], uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15] );
    log_s( section, level, _tmp );
}

/*
void log_s_uuid(const char* section, const unsigned level, const char* s, const veeam_uuid_t* uuid)
{
    char _tmp[MAX_LINE_SIZE];

    snprintf(_tmp, sizeof(_tmp), "%s[%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x]", s,
        (unsigned int*)(&uuid->b[0]),
        (unsigned short*)(&uuid->b[4]),
        (unsigned short*)(&uuid->b[6]),
        (unsigned short*)(&uuid->b[8]),
        uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15]);
    log_s(section, level, _tmp);
}
*/
void log_s_range( const char* section, const unsigned level, const char* s, const range_t* range )
{
    char _tmp[MAX_LOGLINE_SIZE];
    snprintf( _tmp, sizeof( _tmp ), "%s ofs=0x%llx, cnt=0x%llx", s, (unsigned long long)range->ofs, (unsigned long long)range->cnt );
    log_s( section, level, _tmp );
}

void log_s_bytes(const char* section, const unsigned level, const unsigned char* bytes, const size_t count)
{
    char _tmp[MAX_LOGLINE_SIZE];
    size_t pos = 0;
    size_t inx = 0;
    while ( (pos < 252) && (inx < count) ){
        snprintf(_tmp + pos, sizeof(_tmp)-pos, "%02x ", bytes[inx]);
        pos += 3;
        ++inx;
    }
    _tmp[pos] = '\0';
    log_s(section, level, _tmp);

}

void log_vformat(const char* section, const int level, const char *frm, va_list args)
{
    char _tmp[MAX_LOGLINE_SIZE];
    vsnprintf( _tmp, sizeof( _tmp ), frm, args );
    log_s( section, level, _tmp );
}

void log_format( const char* section, const int level, const char* frm, ... )
{
    va_list args;
    va_start( args, frm );
    log_vformat( section, level, frm, args );
    va_end( args );
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
void log_s_sec(const char* section, const unsigned level, const char* s, const time_t totalsecs)
#else
void log_s_sec(const char* section, const unsigned level, const char* s, const time64_t totalsecs)
#endif
{
    struct tm _time;
    char _tmp[MAX_LOGLINE_SIZE];

    if (strlen(s) > (MAX_LOGLINE_SIZE - 20))
        return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
    time_to_tm(totalsecs, 0, &_time);
#else
    time64_to_tm(totalsecs, 0, &_time);
#endif

    snprintf(_tmp, MAX_LOGLINE_SIZE, "%s%02d.%02d.%04ld %02d:%02d:%02d",
        s,
        _time.tm_mday,
        _time.tm_mon + 1,
        _time.tm_year + 1900,

        _time.tm_hour,
        _time.tm_min,
        _time.tm_sec);

    log_s(section, level, _tmp);
}


void log_gisto_init(struct log_gisto* gisto, unsigned long min_value)
{
    memset(&gisto->cnt, 0, sizeof(gisto->cnt));
    gisto->min_value = min_value;
}

void log_gisto_add(struct log_gisto* gisto, unsigned long value)
{
    int inx = 0;
    int last = sizeof(gisto->cnt) / sizeof(gisto->cnt[0]) - 1;
    unsigned long test_value = gisto->min_value;

    while (inx <= last) {
        if (value <= test_value)
            break;

        test_value = test_value << 1;
        inx++;
    }
    atomic_inc(&gisto->cnt[inx]);
}

void log_gisto_show(struct log_gisto* gisto)
{
    int inx = 0;
    int last = sizeof(gisto->cnt) / sizeof(gisto->cnt[0]) - 1;
    unsigned long test_value = gisto->min_value;
    unsigned long prev_test_value = 0;

    while (inx <= last) {
        log_tr_format("(%llu : %llu] KiB - %llu",
            prev_test_value / 1024,
            test_value / 1024,
            atomic_read(&gisto->cnt[inx]));

        prev_test_value = test_value;
        test_value = test_value << 1;
        inx++;
    }
}
