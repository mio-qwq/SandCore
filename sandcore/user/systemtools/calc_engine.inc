static CalcValue calc_value(ScU32 bits,ScU32 wide)
{CalcValue value={bits,wide};
    return value;
}
static void calc_skip(CalcParser *p)
{while(p->at<p->bytes && (p->text[p->at]==' ' || p->text[p->at]=='\t'))p->at++;
}
static void calc_fail(CalcParser *p,ScStatus error)
{if(!p->result.status){p->result.status=error;
        p->result.offset=p->at;
}}
static ScU32 calc_digit(char c)
{return c>='0' && c<='9'?(ScU32)(c-'0'):c>='a' && c<='f'?(ScU32)(c-'a'+10):c>='A' && c<='F'?(ScU32)(c-'A'+10):UINT_MAX;
}
static CalcValue calc_expression(CalcParser *p);
static CalcValue calc_number(CalcParser *p)
{
    calc_skip(p);
    ScU32 base=10,start=p->at,value=0,digits=0;
    if(p->at+1<p->bytes && p->text[p->at]=='0' && (p->text[p->at+1]=='x' || p->text[p->at+1]=='X')){base=16;
        p->at+=2;
    }
    while(p->at<p->bytes){ScU32 digit=calc_digit(p->text[p->at]);
        if(digit>=base)break;
        /* 2147483648只暂存幅值，须有真正一元负号才进入有符号域。
        * 先比较再乘，任何长度输入都不能绕回合法小数。 */
        if(value>(0x80000000u-digit)/base){calc_fail(p,CALC_E_OVERFLOW);
            return calc_value(0,0);
        }
        value=value*base+digit;
        p->at++;
        digits++;
    }
    if(!digits){p->at=start;
        calc_fail(p,CALC_E_SYNTAX);
        return calc_value(0,0);
    }
    return calc_value(value,value==0x80000000u);
}
static CalcValue calc_unary(CalcParser *p)
{
    CalcValue value=calc_value(0,0);
    calc_skip(p);
    if(p->result.status)return value;
    if(p->depth>=64){calc_fail(p,CALC_E_DEPTH);
        return value;
    }p->depth++;
    if(p->at<p->bytes && (p->text[p->at]=='-' || p->text[p->at]=='+')){
        char op=p->text[p->at++];
        value=calc_unary(p);
        if(!p->result.status){
            if(op=='-'){if(value.wide)value.wide=0;
                else if(value.bits==0x80000000u)calc_fail(p,CALC_E_OVERFLOW);
                else value.bits=0u-value.bits;
            }
            else if(value.wide)calc_fail(p,CALC_E_OVERFLOW);
        }
    }else if(p->at<p->bytes && p->text[p->at]=='('){p->at++;
        value=calc_expression(p);
        calc_skip(p);
        if(p->at>=p->bytes || p->text[p->at]!=')')calc_fail(p,CALC_E_SYNTAX);
        else p->at++;
    }else value=calc_number(p);
    p->depth--;
    return value;
}
static CalcValue calc_binary(CalcParser *p,CalcValue a,CalcValue b,char op)
{
    if(a.wide || b.wide){calc_fail(p,CALC_E_OVERFLOW);
        return calc_value(0,0);
    }
    ScU32 x=a.bits,y=b.bits,result=0;
    if(op=='+'){result=x+y;
        if((~(x^y)&(x^result))&0x80000000u)calc_fail(p,CALC_E_OVERFLOW);
    }
    else if(op=='-'){result=x-y;
        if(((x^y)&(x^result))&0x80000000u)calc_fail(p,CALC_E_OVERFLOW);
    }
    else if(op=='*'){ScU32 negative=(x>>31)!=(y>>31),left=x>>31?0u-x:x,right=y>>31?0u-y:y;
        ScU32 limit=negative?0x80000000u:0x7fffffffu;
        if(right && left>limit/right)calc_fail(p,CALC_E_OVERFLOW);
        else {result=left*right;
            if(negative)result=0u-result;
    }}
    else if(op=='/' || op=='%'){
        if(!y)calc_fail(p,CALC_E_DIVZERO);
        else if(x==0x80000000u && y==UINT_MAX)calc_fail(p,CALC_E_OVERFLOW);
        else result=(ScU32)(op=='/'?(ScI32)x/(ScI32)y:(ScI32)x%(ScI32)y);
    }else calc_fail(p,CALC_E_SYNTAX);
    return calc_value(result,0);
}
static CalcValue calc_product(CalcParser *p)
{
    CalcValue value=calc_unary(p);
    while(!p->result.status){calc_skip(p);
        if(p->at>=p->bytes)break;
        char op=p->text[p->at];
        if(op!='*' && op!='/' && op!='%')break;
        p->at++;
        CalcValue right=calc_unary(p);
        if(!p->result.status)value=calc_binary(p,value,right,op);
    }
    return value;
}
static CalcValue calc_expression(CalcParser *p)
{
    CalcValue value=calc_product(p);
    while(!p->result.status){calc_skip(p);
        if(p->at>=p->bytes)break;
        char op=p->text[p->at];
        if(op!='+' && op!='-')break;
        p->at++;
        CalcValue right=calc_product(p);
        if(!p->result.status)value=calc_binary(p,value,right,op);
    }
    return value;
}
static CalcResult calc_evaluate(const char *text,ScU32 bytes)
{
    CalcParser p;
    memset(&p,0,sizeof(p));
    p.text=text;
    p.bytes=bytes;
    calc_skip(&p);
    if(p.at==bytes){calc_fail(&p,CALC_E_EMPTY);
        return p.result;
    }
    CalcValue value=calc_expression(&p);
    calc_skip(&p);
    if(!p.result.status && p.at!=bytes)calc_fail(&p,CALC_E_SYNTAX);
    if(!p.result.status && value.wide)calc_fail(&p,CALC_E_OVERFLOW);
    if(!p.result.status)p.result.bits=value.bits;
    return p.result;
}
static const char *calc_error_name(ScStatus status)
{
    if(status==CALC_E_OVERFLOW)return "超出有符号32位范围";
    if(status==CALC_E_DIVZERO)return "不能除以零";
    if(status==CALC_E_DEPTH)return "括号或一元符号嵌套超过64层";
    if(status==CALC_E_EMPTY)return "请输入表达式";
    return "无效数字、运算符或括号";
}
static void calc_format(ScU32 bits,char decimal[24],char hex[24])
{
    snprintf(decimal,24,"%d",(ScI32)bits);
    if(bits>>31)snprintf(hex,24,"-0x%X",0u-bits);
    else snprintf(hex,24,"0x%X",bits);
}
