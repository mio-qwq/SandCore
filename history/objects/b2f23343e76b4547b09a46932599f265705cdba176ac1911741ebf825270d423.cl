/* =====================================================================
 * mio：Welcome to true color / 自研宿主OpenCL光学场景。
 * 全部交点、真实封闭玻璃内外Snell折射、Fresnel反射、台面遮挡和
 * 镜面均由射线计算。可见彩虹/出射谱是世界坐标中的美术发射光场，
 * 使用局部解析高斯积分；不把这部分说成完整光谱多次散射仿真。
 * 没有低清图放大、定格复制或相邻帧插值。每帧实际相机来自宿主
 * 六幕时间轴，像素与亚像素样本独立求值。此文件不进入SandCore。
 * ===================================================================== */
#define PI 3.14159265358979323846f
#define EPS .0002f
typedef struct {float t;float3 n;int material;} Hit;
uint hash32(uint value){value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);}
float random01(uint *state){*state=hash32(*state+0x9e3779b9u);return (float)(*state>>8)*(1.0f/16777216.0f);}
float halton2(uint value){
    value=((value>>1)&0x55555555u)|((value&0x55555555u)<<1);
    value=((value>>2)&0x33333333u)|((value&0x33333333u)<<2);
    value=((value>>4)&0x0f0f0f0fu)|((value&0x0f0f0f0fu)<<4);
    value=((value>>8)&0x00ff00ffu)|((value&0x00ff00ffu)<<8);
    value=(value>>16)|(value<<16);return (float)value*(1.0f/4294967296.0f);
}
float halton3(uint value){float result=0,scale=1.0f/3.0f;while(value){result+=(float)(value%3)*scale;value/=3;scale/=3;}return result;}
float3 rgblinear(float3 encoded){return select(encoded/12.92f,pow((encoded+.055f)/1.055f,(float3)(2.4f)),encoded>.04045f);}
/* 调色端点已在GFX.md的SCENE/影片表登记；线性光参与运算，最后
 * 统一曝光/色调映射和sRGB编码，不能在光照前压到8位造成色带。 */
float3 ink(){return rgblinear((float3)(11,16,24)/255.0f);}
float3 metal(){return rgblinear((float3)(120,130,140)/255.0f);}
float3 ivory(){return rgblinear((float3)(244,238,226)/255.0f);}
float3 optical(){return rgblinear((float3)(243,250,252)/255.0f);}
float3 spectrum(float t){
    const float3 colors[7]={(float3)(237,53,72),(float3)(255,139,53),(float3)(244,221,69),
        (float3)(88,189,115),(float3)(75,201,219),(float3)(73,125,221),(float3)(168,107,212)};
    float at=clamp(t,0.0f,1.0f)*6.0f;int i=min(5,(int)at);
    return mix(rgblinear(colors[i]/255.0f),rgblinear(colors[i+1]/255.0f),at-(float)i);
}
float3 local_point(float3 p,__constant float *cfg){return (float3)(cfg[16]*p.x+cfg[17]*p.z,p.y,-cfg[17]*p.x+cfg[16]*p.z);}
float3 global_normal(float3 n,__constant float *cfg){return (float3)(cfg[16]*n.x-cfg[17]*n.z,n.y,cfg[17]*n.x+cfg[16]*n.z);}
__attribute__((noinline)) int prism(float3 origin,float3 direction,float limit,Hit *hit,__constant float *cfg){
    float3 p=local_point(origin,cfg),d=local_point(direction,cfg);
    float enter=-1e20f,leave=1e20f;float3 en=(float3)(0),ex=(float3)(0);
    /* 凸体32个支持半空间一次区间裁剪，含预生成的真正倒角面。
     * 原始面和倒角统一求交，不为边缘的外观修改命中后法线。起点
     * 已在玻璃内时正确返回出面，法线仍向外，为折射率内外判定
     * 提供可靠符号。平行面只检查是否在外，不除近零数。 */
    for(int i=0;i<(int)cfg[31];i++){
        float4 plane=vload4(i,cfg+32);
        float slope=dot(plane.xyz,d),offset=plane.w-dot(plane.xyz,p);
        if(fabs(slope)<1e-8f){if(offset<0)return 0;continue;}
        float t=offset/slope;
        if(slope<0){if(t>enter){enter=t;en=plane.xyz;}}
        else if(t<leave){leave=t;ex=plane.xyz;}
        if(enter>leave)return 0;
    }
    float t=enter>EPS?enter:leave;float3 n=enter>EPS?en:ex;
    if(t<=EPS||t>=limit)return 0;
    hit->t=t;hit->n=global_normal(n,cfg);hit->material=1;return 1;
}
int cylinder(float3 origin,float3 direction,float limit,Hit *hit){
    float a=dot(direction.xz,direction.xz),b=dot(origin.xz,direction.xz);
    float c=dot(origin.xz,origin.xz)-1.18f*1.18f;
    float best=limit;float3 normal=(float3)(0);int found=0;
    if(a>1e-10f){float discriminant=b*b-a*c;
        if(discriminant>=0){float root=sqrt(discriminant);
            for(int side=-1;side<=1;side+=2){float t=(-b+(float)side*root)/a;
                float y=origin.y+direction.y*t;
                if(t>EPS&&t<best&&y>=.035f&&y<=.155f){best=t;normal=normalize((float3)(origin.x+direction.x*t,0,origin.z+direction.z*t));found=1;}}}}
    if(fabs(direction.y)>1e-8f){
        for(int face=0;face<2;face++){float t=((face?.155f:.035f)-origin.y)/direction.y;
            float2 point=origin.xz+direction.xz*t;
            if(t>EPS&&t<best&&dot(point,point)<=1.18f*1.18f){best=t;normal=(float3)(0,face?1:-1,0);found=1;}}}
    if(found){hit->t=best;hit->n=normal;hit->material=2;}return found;
}
int ball(float3 origin,float3 direction,float3 center,float radius,float limit,Hit *hit,int material){
    float3 offset=origin-center;float b=dot(offset,direction),c=dot(offset,offset)-radius*radius;
    float disc=b*b-c;if(disc<0)return 0;float root=sqrt(disc),t=-b-root;
    if(t<=EPS)t=-b+root;if(t<=EPS||t>=limit)return 0;
    hit->t=t;hit->n=normalize(origin+direction*t-center);hit->material=material;return 1;
}
Hit scene(float3 origin,float3 direction,__constant float *cfg,int visible_lamps){
    Hit h;h.t=10000;h.n=(float3)(0);h.material=-1;
    if(direction.y<-1e-8f){float t=-origin.y/direction.y;if(t>EPS){h.t=t;h.n=(float3)(0,1,0);h.material=0;}}
    Hit candidate;
    if(prism(origin,direction,h.t,&candidate,cfg))h=candidate;
    if(cylinder(origin,direction,h.t,&candidate))h=candidate;
    if(ball(origin,direction,(float3)(-1.6f,.28f,.75f),.28f,h.t,&candidate,3))h=candidate;
    if(ball(origin,direction,(float3)(1.45f,.12f,-.85f),.12f,h.t,&candidate,4))h=candidate;
    /* 实体矩形灯箱也参与镜面射线。横向/纵向白色反射条来自真正
     * 的灯面交点，不在玻璃屏幕像素上手绘一个高光矩形。 */
    if(visible_lamps&&fabs(direction.y)>1e-8f){float t=(5.4f-origin.y)/direction.y;float3 p=origin+direction*t;
        if(t>EPS&&t<h.t&&fabs(p.x-.3f)<2.8f&&fabs(p.z+.5f)<.42f){h.t=t;h.n=(float3)(0,-1,0);h.material=8;}}
    if(visible_lamps&&fabs(direction.x)>1e-8f){float t=(-4.0f-origin.x)/direction.x;float3 p=origin+direction*t;
        if(t>EPS&&t<h.t&&fabs(p.y-2.3f)<1.8f&&fabs(p.z-.25f)<.38f){h.t=t;h.n=(float3)(1,0,0);h.material=9;}}
    if(visible_lamps&&fabs(direction.z)>1e-8f){float t=(4.4f-origin.z)/direction.z;float3 p=origin+direction*t;
        if(t>EPS&&t<h.t&&fabs(p.x)<2.1f&&fabs(p.y-2.7f)<.32f){h.t=t;h.n=(float3)(0,0,-1);h.material=10;}}
    return h;
}
float3 environment(float3 d){return mix(ink()*.8f,optical()*.022f,smoothstep(-.3f,.7f,d.y));}
float3 base_color(int material,float3 point){
    if(material==0)return rgblinear((float3)(24,32,42)/255.0f);
    if(material==2||material==3)return metal();
    if(material==4)return spectrum(.70f);
    return optical();
}
float3 cosine_direction(float3 n,uint *seed,float roughness){
    float a=2*PI*random01(seed),r=sqrt(random01(seed))*roughness;
    float3 axis=fabs(n.y)<.9f?(float3)(0,1,0):(float3)(1,0,0);
    float3 tangent=normalize(cross(axis,n)),bitangent=cross(n,tangent);
    return normalize(n+tangent*(r*cos(a))+bitangent*(r*sin(a)));
}
float3 reflected(float3 ray,float3 n){return ray-2*dot(ray,n)*n;}
float3 beam_field(float3 origin,float3 ray,float limit,__constant float *cfg){
    /* 摄影机/反射/折射每段都查询同一个世界光场。彩带是有厚度的
     * 发射体积，积分受真实最近实体limit裁剪。窄高斯沿法线可用
     * erf解析积分，避免每个像素反复采样35个重叠体积。
     * 全局照明的多次介质散射不在本版模型内，不能夸称无偏光谱。 */
    float3 origin_fan=(float3)(.52f,.89f,.015f);
    float3 across=(float3)(0,-.581238f,.813734f),normal=(float3)(0,-.813734f,-.581238f);
    float fan_length=5.78f,width=2.15058f;
    float3 center=(float3)(5.78f,-.085f,1.26f);float drift=dot(center,across),rise=dot(center,normal);
    float3 offset=origin-origin_fan;
    float a=dot(offset,normal)-offset.x*rise/fan_length,b=dot(ray,normal)-ray.x*rise/fan_length;
    float center_t=fabs(b)>1e-7f?-a/b:-1;
    float3 result=(float3)(0);
    if(center_t>0&&center_t<limit){
        float3 point=offset+ray*center_t;float u=point.x/fan_length;
        if(u>0&&u<1){float sigma=.012f+u*.083f,span=.025f+u*(width-.025f);
            float color_t=.5f+(dot(point,across)-u*drift)/span;
            float edge=smoothstep(0.0f,.09f,color_t)*smoothstep(0.0f,.09f,1-color_t);
            float integrated=.5f*sqrt(PI/5.0f)*sigma/fmax(fabs(b),.008f)*
                (erf(sqrt(5.0f)*(a+b*limit)/sigma)-erf(sqrt(5.0f)*a/sigma))*sign(b);
            result+=spectrum(color_t)*edge*integrated*(35.0f/(1+point.x*point.x))*cfg[18];
        }
    }
    /* 白光由同一高斯管体积积分；有限端点截断避免摄像机背后
     * 的光线穿过玻璃后仍无限发亮。真实实体前的长度参与积分。 */
    float3 white_start=(float3)(-4.4f,.90f,-.12f),white_end=(float3)(-.50f,.89f,.015f);
    float3 axis=normalize(white_end-white_start),v=origin-white_start;
    float along=dot(v,axis),speed=dot(ray,axis);float3 perp=v-axis*along,dray=ray-axis*speed;
    float dd=dot(dray,dray);
    if(dd>1e-9f){float t=-dot(perp,dray)/dd;
        float s=along+speed*t;
        if(t>0&&t<limit&&s>0&&s<distance(white_start,white_end)){
            float3 radial=perp+dray*t;float radius=.033f;
            float integral=exp(-dot(radial,radial)*5/(radius*radius))*sqrt(PI/5)*radius/sqrt(dd);
            result+=optical()*integral*38*cfg[19];}}
    /* 彩虹薄体积：局部平面交点确定三维弧位置，再沿薄深度积分。
     * 径向连续颜色，内外缘和两端柔衰减；不是七条硬圆管。 */
    if(cfg[20]>0&&fabs(ray.z)>.001f){float t=(3.15f-origin.z)/ray.z;
        if(t>0&&t<limit){float3 p=origin+ray*t-(float3)(1.10f,.04f,3.15f);
            float radius=length(p.xy),color_t=(3.35f-radius)/.63f;
            float edge=smoothstep(0.0f,.12f,color_t)*smoothstep(0.0f,.12f,1-color_t);
            float ends=smoothstep(.208f,.39f,p.y/fmax(radius,.001f));
            result+=spectrum(color_t)*edge*ends*(7*sqrt(PI/5)*.085f/fabs(ray.z))*cfg[20];}}
    return result;
}
float3 lighting(float3 point,float3 n,float3 base,uint *seed,__constant float *cfg){
    float3 light=(float3)(0);
    /* 三个真实灯面对应三次显式直接光采样；场景常量不按像素建立
     * 光源列表。太阳式硬阴影改为有限面积软阴影，样本走真实挡体。
     * 玻璃直透这里按透射估计，不把它当完全黑色不透明障碍。 */
    for(int i=0;i<3;i++){
        float u=random01(seed)*2-1,v=random01(seed)*2-1;
        float3 position=i==0?(float3)(.3f+2.8f*u,5.4f,-.5f+.42f*v):
            i==1?(float3)(-4,2.3f+1.8f*u,.25f+.38f*v):(float3)(2.1f*u,2.7f+.32f*v,4.4f);
        float3 delta=position-point;float squared=dot(delta,delta),dist=sqrt(squared);float3 direction=delta/dist;
        float cosine=fmax(0.0f,dot(n,direction));if(cosine<=0)continue;
        Hit blocker=scene(point+n*EPS*3,direction,cfg,1);float transmission=1;
        if(blocker.t<dist-.005f)transmission=blocker.material==1?.86f:0;
        light+=base*optical()*cosine*transmission*(i==0?10.0f:i==1?4.2f:6.0f)/fmax(squared,.03f);
    }
    // 出射谱沿三维方向照亮台面，按距离和柔边衰减；颜色连续且
    // 随光能包络同步开关，不能在关白光时凭空出现一条亮色贴图。
    float x=point.x-.52f,u=x/5.78f;
    if(u>0&&u<1.2f){float t=(point.z-.015f-u*.4f)/(u*1.75f+.025f);
        float edge=smoothstep(-.10f,.12f,t)*smoothstep(-.10f,.12f,1-t);
        light+=base*spectrum(t)*edge*cfg[18]*2.8f/(1+x*x*.08f);}
    return light+base*.028f;
}
float3 trace(float3 origin,float3 ray,uint *seed,__constant float *cfg){
    float3 result=(float3)(0),throughput=(float3)(1);int inside=0,spectral_band=-1;
    for(int bounce=0;bounce<10;bounce++){
        /* 摄影棚灯面设置“相机不可见、反射/折射/照明可见”。这是
         * 明确的光线可见性美术合同，避免整块白灯箱压住片名和彩虹；
         * 镜面依然能命中灯面，不删除高光或灯光贡献。 */
        Hit hit=scene(origin,ray,cfg,bounce>0);float maximum=hit.material<0?30.0f:hit.t;
        if(inside)throughput*=exp(-maximum*(float3)(.009f,.004f,.0015f));
        result+=throughput*beam_field(origin,ray,maximum,cfg);
        if(hit.material<0){result+=throughput*environment(ray);break;}
        if(hit.material>=8){result+=throughput*optical()*(hit.material==8?9:hit.material==9?5:7);break;}
        float3 point=origin+ray*hit.t,n=hit.n;float facing=dot(ray,n);
        if(hit.material==1){
            /* 第一次玻璃交点才把RGB能量随机分到一个光学波段，
             * 概率1/3、权重3；三波段用不同折射率产生真实边缘色散。
             * 之前无玻璃的光仍全RGB，不人为给整张画面加色噪点。
             * 此为三波段光学近似，不宣称连续CIE光谱仿真。 */
            if(spectral_band<0){spectral_band=min(2,(int)(random01(seed)*3));
                throughput*=spectral_band==0?(float3)(3,0,0):spectral_band==1?(float3)(0,3,0):(float3)(0,0,3);}
            float ior=spectral_band==0?1.508f:spectral_band==1?1.5168f:1.532f;
            int entering=facing<0;float3 oriented=entering?n:-n;
            float eta=entering?1/ior:ior,cosine=-dot(oriented,ray);
            float discriminant=1-eta*eta*(1-cosine*cosine);
            float f0=(ior-1)/(ior+1);f0*=f0;
            float fresnel=f0+(1-f0)*pown(1-clamp(cosine,0.0f,1.0f),5);
            // 反射/折射随机选择使两条路径都有真实贡献而不指数分叉。
            // 全内反射必走反射，绝不对负判别式开方或产生NaN。
            if(discriminant<0||random01(seed)<fresnel){ray=normalize(reflected(ray,oriented));origin=point+oriented*EPS*3;}
            else{ray=normalize(eta*ray+(eta*cosine-sqrt(discriminant))*oriented);origin=point-oriented*EPS*3;inside=entering;}
            continue;
        }
        float3 base=base_color(hit.material,point);
        float roughness=hit.material==0?.105f:hit.material==2?.045f:hit.material==3?.025f:.15f;
        float reflectivity=hit.material==0?.08f:hit.material==4?.32f:.82f;
        float fresnel=reflectivity+(1-reflectivity)*pown(1-fabs(facing),5);
        result+=throughput*lighting(point,n,base,seed,cfg)*(1-fresnel);
        throughput*=mix(base,(float3)(1),hit.material==0?.84f:.32f)*fresnel;
        ray=cosine_direction(normalize(reflected(ray,n)),seed,roughness);
        if(dot(ray,n)<=0)ray=normalize(reflected(ray,n));
        origin=point+n*EPS*3;
        if(fmax(throughput.x,fmax(throughput.y,throughput.z))<.0001f)break;
    }
    return result;
}
__kernel void trace_band(__global float4 *accum,__global float4 *guide,
                         __global float4 *albedo,__constant float *cfg,
                         uint width,uint height,uint ybegin,uint yend,
                         uint sample_begin,uint sample_count,uint frame,
                         volatile __global uint *errors){
    uint index=get_global_id(0),x=index%width,y=ybegin+index/width;if(y>=yend||y>=height)return;
    uint at=y*width+x;float3 eye=vload3(0,cfg),forward=vload3(0,cfg+3),right=vload3(0,cfg+6),up=vload3(0,cfg+9);
    float focal=cfg[12];float4 sum=sample_begin?accum[at]:(float4)(0);
    for(uint j=0;j<sample_count;j++){
        /* 相邻时间帧共享低差异的像素种子，只改真实相机/物体输入；
         * 不把静止场景噪点重新随机抖动，也不混合过去的最终颜色。
         * 每帧仍重算全部几何和光线，镜头运动/世界变化即时生效。 */
        uint seed=hash32(at^hash32(sample_begin+j+1)^123u);
        float2 rotation=(float2)((float)(hash32(at)>>8),(float)(hash32(at^231u)>>8))*(1.0f/16777216.0f);
        float jitter_x=halton2(sample_begin+j+1)+rotation.x,jitter_y=halton3(sample_begin+j+1)+rotation.y;
        float sx=(float)x+jitter_x-floor(jitter_x),sy=(float)y+jitter_y-floor(jitter_y);
        float3 ray=normalize(forward+right*((sx-width*.5f)/focal)+up*((height*.5f-sy)/focal));
        float3 value=trace(eye,ray,&seed,cfg);
        if(!all(isfinite(value))||any(value<0)){atomic_inc(errors);value=(float3)(0);}
        float luminance=dot(value,(float3)(.2126f,.7152f,.0722f));sum+=(float4)(value,luminance*luminance);
    }
    accum[at]=sum;
    if(sample_begin==0){float3 ray=normalize(forward+right*((x+.5f-width*.5f)/focal)+up*((height*.5f-y-.5f)/focal));
        Hit h=scene(eye,ray,cfg,0);guide[at]=(float4)(h.n,h.material<0?-1:h.t);
        albedo[at]=(float4)(h.material<0?environment(ray):base_color(h.material,eye+ray*h.t),(float)h.material);}
}
__kernel void average(__global float4 *sum,__global float4 *output,uint count,uint samples){
    uint i=get_global_id(0);if(i>=count)return;float4 value=sum[i]/(float)samples;
    float lum=dot(value.xyz,(float3)(.2126f,.7152f,.0722f));value.w=fmax(0.0f,value.w-lum*lum)/(float)samples;
    output[i]=value;
}
__kernel void denoise(__global const float4 *input,__global float4 *output,
    __global const float4 *guide,__global const float4 *albedo,uint width,uint height,uint step){
    uint i=get_global_id(0);if(i>=width*height)return;int x=i%width,y=i/width;
    float4 center=input[i],normal=guide[i],base=albedo[i];float lum=dot(center.xyz,(float3)(.2126f,.7152f,.0722f));
    float3 total=(float3)(0);float weight=0;
    // à-trous只混合相同实体/相近法线深度及亮度的样本。玻璃边界、
    // 彩虹/字幕不使用一张全屏模糊掩盖锯齿；没有时间插值。
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
        int xx=x+dx*(int)step,yy=y+dy*(int)step;if(xx<0||xx>=(int)width||yy<0||yy>=(int)height)continue;
        uint j=yy*width+xx;float4 g=guide[j],a=albedo[j],v=input[j];if(a.w!=base.w)continue;
        float w=(dx==0?2:1)*(dy==0?2:1);
        w*=exp(-fabs(g.w-normal.w)/(fmax(.015f,fabs(normal.w)*.004f)*(float)step));
        w*=pown(fmax(0.0f,dot(g.xyz,normal.xyz)),24);
        if(base.w<0)w=(dx==0?2:1)*(dy==0?2:1);
        float l=dot(v.xyz,(float3)(.2126f,.7152f,.0722f));
        w*=exp(-fabs(l-lum)/(4*sqrt(center.w+v.w)+.009f+.02f*lum));
        total+=v.xyz*w;weight+=w;
    }
    output[i]=(float4)(total/fmax(weight,.00001f),center.w);
}
float3 tonemap(float3 value){
    value*=1.32f;value=clamp((value*(2.51f*value+.03f))/(value*(2.43f*value+.59f)+.14f),0.0f,1.0f);
    return select(value*12.92f,1.055f*pow(value,(float3)(1/2.4f))-.055f,value>.0031308f);
}
__kernel void encode(__global const float4 *hdr,__global uchar *rgb16,
                    __global const uchar4 *overlay,uint count,float opacity){
    uint i=get_global_id(0);if(i>=count)return;float3 color=tonemap(hdr[i].xyz);
    uchar4 text=overlay[i];color=mix(color,convert_float3(text.xyz)/255.0f,(float)text.w/255.0f*opacity);
    uint3 value=convert_uint3_sat_rte(color*65535.0f);uint at=i*6;
    rgb16[at]=value.x>>8;rgb16[at+1]=value.x;rgb16[at+2]=value.y>>8;
    rgb16[at+3]=value.y;rgb16[at+4]=value.z>>8;rgb16[at+5]=value.z;
}
