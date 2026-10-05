
__kernel void prism_debug(__global const float4 *input,__global float *output,
                         __constant float *cfg,uint count){
    uint i=get_global_id(0);if(i>=count)return;
    float4 eye=input[i*2],ray=input[i*2+1];uint at=i*192;
    vstore4(eye,0,output+at);vstore4(ray,0,output+at+4);
    float3 p=local_point(eye.xyz,cfg),d=local_point(ray.xyz,cfg);
    vstore3(p,0,output+at+8);vstore3(d,0,output+at+12);
    output[at+16]=cfg[16];output[at+17]=cfg[17];output[at+18]=cfg[31];
    float enter=-1e20f,leave=1e20f;int invalid=0;
    /* 诊断路径没有平面循环内提前退出，所有面写出真实值；
     * 单独的原prism仍原样调用，两结果不能互相覆盖。 */
    for(int j=0;j<32;j++){
        float4 plane=vload4(j,cfg+32);
        float slope=dot(plane.xyz,d),offset=plane.w-dot(plane.xyz,p);
        float t=fabs(slope)<1e-8f?0:offset/slope;
        output[at+32+j*4]=slope;output[at+33+j*4]=offset;
        output[at+34+j*4]=t;output[at+35+j*4]=plane.w;
        if(fabs(slope)<1e-8f){if(offset<0)invalid=1;}
        else if(slope<0){enter=fmax(enter,t);}else leave=fmin(leave,t);
    }
    output[at+19]=enter;output[at+20]=leave;output[at+21]=(float)invalid;
    Hit hit;hit.t=0;hit.n=(float3)(0);hit.material=-1;
    int found=prism(eye.xyz,ray.xyz,eye.w,&hit,cfg);
    output[at+22]=(float)found;output[at+23]=hit.t;vstore3(hit.n,0,output+at+24);
}
