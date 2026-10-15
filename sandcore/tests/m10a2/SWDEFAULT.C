/* GUI06真实CMD122没有进入default；这里只验证对应机器控制流，不借UI返回值猜。
 * 无syscall/libc，宿主可从实际生成SCX映像的main执行到返回。 */
static int choose(unsigned x)
{
    int result=0;
    switch(x){
    case 100:result=1;break;
    default:if(x>=121 && x<=126)result=7;break;
    }
    return result;
}
static int nested(unsigned x,unsigned y)
{
    int result=0;
    switch(x){
    default:
        switch(y){case 1:result=11;break;default:result=13;break;}
        break;
    case 4:result=17;break;
    }
    return result;
}
int main(void)
{
    if(choose(100)!=1)return 1;
    if(choose(122)!=7)return 2;
    if(choose(50)!=0)return 3;
    if(nested(2,1)!=11)return 4;
    if(nested(2,9)!=13)return 5;
    if(nested(4,9)!=17)return 6;
    return 0;
}
