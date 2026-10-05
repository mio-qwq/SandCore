__kernel void film_probe(__global uint *out, uint count) {
    uint i=(uint)get_global_id(0);if(i>=count)return;
    uint h=i*374761393u+668265263u;
    h=(h^(h>>13))*1274126177u;h=h^(h>>16);
    out[i]=0xff000000u|(h&0xffffffu);
}