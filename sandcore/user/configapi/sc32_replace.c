#include "../SC32REPLACE.H"
#include "SC32REPLACEWIRE.H"

/* 在SC32API.C的leaseapi.c之后组合，应用不接触私有gate。先验证
 * caller原record再构造本库record，防止拷贝越界或吞掉reserved。 */
ScStatus ScFileLeasePublishPreserving(const ScLeasePublishDesc *desc,ScLeaseInfo *out)
{
    ScStatus checked=sc32_lease_records(desc,sizeof(*desc),out,SC32_L_PUBLISH);
    if(checked<0)return checked;
    if(desc->flags!=SC32_LEASE_EXPECT_EXACT || desc->final_mode || !desc->target_lease
       || !desc->target_generation || !desc->expected_target_revision)
        return sc32_invalid(SC32_E_INVALID,SC32_OP_LEASE_BASE+SC32_L_PUBLISH-SC32_L_ACQUIRE);
    ScLeasePublishDesc request;
    sc32_copy(&request,desc,sizeof(request));
    request.flags|=SC32_LEASE_PRESERVE_TARGET_METADATA;
    return sc32_lease_publish(&request,out,SC32_L_PUBLISH);
}
