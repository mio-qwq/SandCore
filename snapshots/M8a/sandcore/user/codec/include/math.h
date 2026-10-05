#ifndef SANDCORE_CODEC_MATH_H
#define SANDCORE_CODEC_MATH_H
/* mio：libwebp同一yuv文件还包含编码端gamma函数，这里只提供其
 * 声明让源文件可编译。解码SCX用--gc-sections剔除全部编码路径；
 * 不提供pow实现，误链接浮点编码路径必须报未定义符号，不能静默
 * 给它一个错误的近似结果。PNG/JPEG也关闭HDR/linear浮点路径。 */
double pow(double base,double exponent);
double ldexp(double value,int exponent);
#endif
