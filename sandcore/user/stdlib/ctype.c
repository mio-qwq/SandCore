#include "SCCLIBPRIVATE.H"
/* C locale的固定分类表；非ASCII字节不被误当UTF-8码点分类。 */
static const unsigned short __scc_ctype_bits[256]={
    16u,16u,16u,16u,16u,16u,16u,16u,16u,536u,24u,24u,24u,24u,16u,16u,
    16u,16u,16u,16u,16u,16u,16u,16u,16u,16u,16u,16u,16u,16u,16u,16u,
    552u,224u,224u,224u,224u,224u,224u,224u,224u,224u,224u,224u,224u,224u,224u,224u,
    356u,356u,356u,356u,356u,356u,356u,356u,356u,356u,224u,224u,224u,224u,224u,224u,
    224u,353u,353u,353u,353u,353u,353u,97u,97u,97u,97u,97u,97u,97u,97u,97u,
    97u,97u,97u,97u,97u,97u,97u,97u,97u,97u,97u,224u,224u,224u,224u,224u,
    224u,354u,354u,354u,354u,354u,354u,98u,98u,98u,98u,98u,98u,98u,98u,98u,
    98u,98u,98u,98u,98u,98u,98u,98u,98u,98u,98u,224u,224u,224u,224u,16u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u
};
static int __scc_class(int value,unsigned int mask)
{
    return value>=0 && value<=255?(__scc_ctype_bits[value]&mask)!=0:0;
}
int isupper(int value){
    return __scc_class(value,1);
}
int islower(int value){
    return __scc_class(value,2);
}
int isalpha(int value){
    return __scc_class(value,3);
}
int isdigit(int value){
    return __scc_class(value,4);
}
int isalnum(int value){
    return __scc_class(value,7);
}
int isspace(int value){
    return __scc_class(value,8);
}
int iscntrl(int value){
    return __scc_class(value,16);
}
int isprint(int value){
    return __scc_class(value,32);
}
int isgraph(int value){
    return __scc_class(value,64);
}
int ispunct(int value){
    return __scc_class(value,128);
}
int isxdigit(int value){
    return __scc_class(value,256);
}
int isblank(int value){
    return __scc_class(value,512);
}
int tolower(int value){
    return isupper(value)?value+32:value;
}
int toupper(int value){
    return islower(value)?value-32:value;
}

