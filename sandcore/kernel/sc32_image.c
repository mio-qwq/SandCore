#include "sc32_image.h"
#include "sc32_core.h"
#include "image_service.h"
#include "task.h"
#include "paging.h"
#include "../user/imageapi/SC32IMAGEWIRE.H"

int sc32_image_call(int pid,u32 operation,u32 request,u32 output)
{
    if(operation==SC32_I_BEGIN){
        int error=sc32_core_output(pid,output,sizeof(Sc32ImageTicket));
        if(error)return error;
        Sc32ImageBegin input;
        error=sc32_core_input(pid,request,&input,sizeof(input));
        if(error)return error;
        if(input.reserved)return SC32_E_INVALID;
        int ticket=image_service_memory_begin(pid,input.bytes);
        if(ticket<0)return ticket;
        Sc32ImageTicket result={sizeof(result),1,(u32)ticket,0};
        if(paging_user_copy(TASK(pid).pd,output,&result,sizeof(result),1)){
            (void)image_service_cancel(pid,(u32)ticket);
            return SC32_E_INVALID;
        }
        return 0;
    }
    if(operation==SC32_I_WRITE || operation==SC32_I_READ){
        if(output)return SC32_E_INVALID;
        Sc32ImageTransfer input;
        int error=sc32_core_input(pid,request,&input,sizeof(input));
        if(error)return error;
        return operation==SC32_I_WRITE
        ?image_service_memory_write(pid,input.ticket,input.offset,input.address,input.bytes)
        :image_service_memory_read(pid,input.ticket,input.offset,input.address,input.bytes);
    }
    if(operation==SC32_I_SEAL || operation==SC32_I_CLAIM){
        Sc32ImageTicket input;
        int error=sc32_core_input(pid,request,&input,sizeof(input));
        if(error)return error;
        if(input.reserved)return SC32_E_INVALID;
        if(operation==SC32_I_SEAL){if(output)return SC32_E_INVALID;
            return image_service_memory_seal(pid,input.ticket);
        }
        error=sc32_core_output(pid,output,sizeof(Sc32ImageSource));
        if(error)return error;
        Sc32ImageSource result={0};
        result.size=sizeof(result);
        result.version=1;
        error=image_service_source_claim(pid,input.ticket,&result.kind,&result.bytes,result.path);
        if(error)return error;
        return paging_user_copy(TASK(pid).pd,output,&result,sizeof(result),1)?SC32_E_INVALID:0;
    }
    return SC32_E_NOT_SUPPORTED;
}
