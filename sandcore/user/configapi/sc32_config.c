#include "../sc32/SC32PRIVATE.H"
#include "SC32CONFIGWIRE.H"
typedef char config_desc_is_64[(sizeof(ScConfigurationDesc)==64)?1:-1];
typedef char config_result_is_48[(sizeof(ScConfigurationResult)==48)?1:-1];
ScStatus ScConfigurationApply(const ScConfigurationDesc *desc,ScConfigurationResult *out)
{
    ScStatus s=sc32_record_in(desc,sizeof(*desc));if(s<0)return sc32_invalid(s,SC32_C_ERROR_OPERATION);
    s=sc32_record_out(out,sizeof(*out));if(s<0)return sc32_invalid(s,SC32_C_ERROR_OPERATION);
    if(sc32_overlap(desc,sizeof(*desc),out,out->size))return sc32_invalid(SC32_E_INVALID,SC32_C_ERROR_OPERATION);
    Sc32ConfigurationRequest request;sc32_copy(&request,desc,sizeof(request));
    char path[65];if(desc->action!=SC32_CONFIG_RELOAD){s=sc32_absolute(desc->candidate,path,SC32_C_ERROR_OPERATION);if(s<0)return s;request.candidate=(ScU32)path;}
    ScConfigurationResult result;sc32_zero(&result,sizeof(result));result.size=sizeof(result);result.version=1;result.raw=(ScI32)0x80000000u;
    ScI32 status=sc32_call(SC32_WIRE_GATE,SC32_C_CONFIG_APPLY,(ScI32)&request,(ScI32)&result,0,0,0);
    if(result.size==sizeof(result) && result.version==1 && result.raw!=(ScI32)0x80000000u)sc32_copy(out,&result,sizeof(result));
    return sc32_native(status,SC32_C_ERROR_OPERATION);
}
ScStatus ScConfigurationCheck(ScU32 kind,const char *path)
{
    if(kind>SC32_CHECK_WALLPAPER)return sc32_invalid(SC32_E_INVALID,SC32_C_ERROR_OPERATION+1);
    char absolute[65];ScStatus s=sc32_absolute(path,absolute,SC32_C_ERROR_OPERATION+1);if(s<0)return s;
    return sc32_legacy(sc32_call(0x7B,(ScI32)kind,(ScI32)absolute,0,0,0,0),SC32_C_ERROR_OPERATION+1);
}
ScStatus ScDesktopReload(void)
{return sc32_legacy(sc32_call(0x24,0,0,0,0,0,0),SC32_C_ERROR_OPERATION+2);}
