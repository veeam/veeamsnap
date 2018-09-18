#pragma once

//#define GET_FILE	get_file_name(__FILE__)

static inline char* get_file_name(char* path)
{
	size_t namestart;
	size_t pathlen = strlen(path);
	if (0==pathlen)
		return path;

	namestart = pathlen;
	while ( (namestart>0) ){
		if ( (path[namestart]== '/') || (path[namestart]== '\\') ){
			++namestart;
			break;
		}
		--namestart;
	}
	return &path[namestart];
}

///////////////////////////////////////////////////////////////////////////////
#define log_traceln(StringMessage) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage ); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage ); break; \
	default:          pr_warn("    %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage ); \
    }}
#define log_traceln_s(StringMessage, StringValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); break; \
	default:          pr_warn("    %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); \
	}}
#define log_traceln_d(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue ); break; \
	default:          pr_warn("    %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_traceln_ld(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue ); break; \
	default:          pr_warn("    %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
    }}
#define log_traceln_sz(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue ); break; \
	default:          pr_warn("    %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue); \
    }}
#define log_traceln_lld(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue ); break; \
	default:	      pr_warn("    %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_traceln_x(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue ); break; \
	default:	      pr_warn("    %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}
#define log_traceln_p(StringMessage, Pointer) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer ); break; \
	default:	      pr_warn("    %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer); \
	}}
#define log_traceln_llx(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("    %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}
#define log_traceln_lx(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("    %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}
#define log_traceln_dev_id_s(StringMessage, devid) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, devid.major, devid.minor); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, devid.major, devid.minor ); break; \
	default:	      pr_warn("    %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, devid.major, devid.minor); \
	}}
#define log_traceln_dev_t(StringMessage, Device) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); break; \
	default:	      pr_warn("    %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); \
	}}
#define log_traceln_sect(StringMessage, SectorValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue ); break; \
	default:	      pr_warn("    %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue); \
    }}
#define log_traceln_uuid(StringMessage, uuid) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x\n", MODULE_NAME,  __FUNCTION__, StringMessage, uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4], uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9], uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15]); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x\n", MODULE_NAME,  __FUNCTION__, StringMessage, uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4], uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9], uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15]); break; \
	default:	      pr_warn("    %s:%s %s%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x\n", MODULE_NAME,  __FUNCTION__, StringMessage, uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4], uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9], uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15]); \
	}}
#define log_traceln_range(StringMessage, range) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("    %s:%s %s ofs=0x%llx cnt=0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)range.ofs, (unsigned long long)range.cnt); break; \
	case VEEAM_LL_LO: pr_info("    %s:%s %s ofs=0x%llx cnt=0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)range.ofs, (unsigned long long)range.cnt); break; \
	default:	      pr_warn("    %s:%s %s ofs=0x%llx cnt=0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)range.ofs, (unsigned long long)range.cnt); \
    }}

///////////////////////////////////////////////////////////////////////////////
#define log_errorln(StringMessage) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage); break; \
	default:	      pr_warn("ERR %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage); \
	}}
#define log_errorln_s(StringMessage, StringValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); break; \
	default:	      pr_warn("ERR %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); \
	}}
#define log_errorln_d(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue ); break; \
	default:	      pr_warn("ERR %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_errorln_ld(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue ); break; \
	default:	      pr_warn("ERR %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_errorln_sz(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue ); break; \
	default:	      pr_warn("ERR %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue); \
	}}
#define log_errorln_lld(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue ); break; \
	default:	      pr_warn("ERR %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_errorln_x(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("ERR %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}
#define log_errorln_p(StringMessage, Pointer) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer ); break; \
	default:	      pr_warn("ERR %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer); \
	}}
#define log_errorln_llx(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("ERR %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}\

#define log_errorln_lx(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("ERR %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}
#define log_errorln_dev_t(StringMessage, Device) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); break; \
	default:	      pr_warn("ERR %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); \
	}}
#define log_errorln_sect(StringMessage, SectorValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue); break; \
	default:	      pr_warn("ERR %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue);\
	}}
#define log_errorln_uuid(StringMessage, uuid) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x\n", MODULE_NAME,  __FUNCTION__, StringMessage, uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4], uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9], uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15]); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x\n", MODULE_NAME,  __FUNCTION__, StringMessage, uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4], uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9], uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15]); break; \
	default:	      pr_warn("ERR %s:%s %s%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x\n", MODULE_NAME,  __FUNCTION__, StringMessage, uuid->b[0], uuid->b[1], uuid->b[2], uuid->b[3], uuid->b[4], uuid->b[5], uuid->b[6], uuid->b[7], uuid->b[8], uuid->b[9], uuid->b[10], uuid->b[11], uuid->b[12], uuid->b[13], uuid->b[14], uuid->b[15]); \
	}}
#define log_errorln_range(StringMessage, range) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("ERR %s:%s %s ofs=0x%llx cnt=0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)range.ofs, (unsigned long long)range.cnt); break; \
	case VEEAM_LL_LO: pr_info("ERR %s:%s %s ofs=0x%llx cnt=0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)range.ofs, (unsigned long long)range.cnt); break; \
	default:	      pr_warn("ERR %s:%s %s ofs=0x%llx cnt=0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)range.ofs, (unsigned long long)range.cnt); \
    }}
///////////////////////////////////////////////////////////////////////////////
#define log_warnln(StringMessage) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage); break; \
	default:	      pr_warn("WRN %s:%s %s\n", MODULE_NAME,  __FUNCTION__, StringMessage); \
	}}
#define log_warnln_s(StringMessage, StringValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); break; \
	default:	      pr_warn("WRN %s:%s %s%s\n", MODULE_NAME,  __FUNCTION__, StringMessage, StringValue); \
	}}
#define log_warnln_d(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	default:	      pr_warn("WRN %s:%s %s%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_warnln_ld(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	default:	      pr_warn("WRN %s:%s %s%ld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_warnln_sz(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue); break; \
	default:	      pr_warn("WRN %s:%s %s%lu\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long)DecimalValue); \
	}}
#define log_warnln_lld(StringMessage, DecimalValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); break; \
	default:	      pr_warn("WRN %s:%s %s%lld\n", MODULE_NAME,  __FUNCTION__, StringMessage, DecimalValue); \
	}}
#define log_warnln_x(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("WRN %s:%s %s0x%x\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}
#define log_warnln_p(StringMessage, Pointer) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer); break; \
	default:	      pr_warn("WRN %s:%s %s0x%p\n", MODULE_NAME,  __FUNCTION__, StringMessage, Pointer); \
	}}
#define log_warnln_llx(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("WRN %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
	}}
#define log_warnln_lx(StringMessage, HexValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); break; \
	default:	      pr_warn("WRN %s:%s %s0x%lx\n", MODULE_NAME,  __FUNCTION__, StringMessage, HexValue); \
    }}
#define log_warnln_dev_t(StringMessage, Device) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); break; \
	default:	      pr_warn("WRN %s:%s %s%d:%d\n", MODULE_NAME,  __FUNCTION__, StringMessage, MAJOR(Device), MINOR(Device)); \
	}}
#define log_warnln_sect(StringMessage, SectorValue) \
	{switch(get_debuglogging()){ \
	case VEEAM_LL_HI: pr_err ("WRN %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue); break; \
	case VEEAM_LL_LO: pr_info("WRN %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue); break; \
	default:	      pr_warn("WRN %s:%s %s0x%llx\n", MODULE_NAME,  __FUNCTION__, StringMessage, (unsigned long long)SectorValue); \
	}}
///////////////////////////////////////////////////////////////////////////////

//void log_dump(void* p, size_t size);
