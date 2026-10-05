
/* mio：直接调用被测工程prism，未替换几何或导入预算命中。 */
__kernel void verify_prism(__global const float4 *rays,__global float *output,
                          __constant float *cfg,uint count){
    uint i=get_global_id(0);if(i>=count)return;
    float4 eye=rays[i*2],ray=rays[i*2+1];
    Hit h;h.t=0;h.n=(float3)(0);h.material=-1;
    int found=prism(eye.xyz,ray.xyz,eye.w,&h,cfg);
    uint at=8+i*8;output[at]=(float)found;output[at+1]=h.t;
    output[at+2]=h.n.x;output[at+3]=h.n.y;output[at+4]=h.n.z;
    output[at+5]=(float)h.material;output[at+6]=0;output[at+7]=0;
}
