/* 放在br_close_handle真实正文之后。只有可信内核服务错误回滚调用，
 * 仍核本人PID/gen/资源种类，不允许按另一个进程的裸编号关闭端点。 */
int sc32_broker_kernel_close(int pid,u32 id)
{
    br_handle *handle=br_handle_get(pid,id,0);
    if(!handle || (handle->kind!=BR_ENDPOINT && handle->kind!=BR_CHANNEL))return SC32_E_STALE;
    return br_close_handle(handle);
}
