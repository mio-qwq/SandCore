#include "../SCIO.H"
#include "../SCFILE.H"
#include "../SCGLOB.H"
#include "../SCARITH.H"
#define TOKENS 512
#define NODES 256
#define VARIABLES 48
enum { WORD=256,AND,OR,APPEND,ERR_OUT,ERR_APPEND,HEREDOC,ARM_END,END };
enum { COMMAND,SEQUENCE,CONJUNCTION,DISJUNCTION,PIPELINE,BACKGROUND,CONDITION,LOOP,ITERATE,NEGATE,SUBSHELL,FUNCTION,SELECT,ARM };
typedef struct {int type,start,end,heredoc;} shell_token;
typedef struct {char delimiter[64];int begin,end,quoted,strip_tabs;} shell_heredoc;
typedef struct {int kind,a,b,c,first,count,start,end;} shell_node;
typedef struct {char name[32],value[256];int exported;} shell_variable;
typedef struct {char name[32];char *body;} shell_function;
typedef struct {char *name,*value;char old[256];int found;} shell_temporary;
typedef struct {char arena[2048];char *args[32];shell_temporary temporary[32];} shell_command_frame;
static shell_command_frame *command_frames[16];static int command_depth;
static shell_token tokens[TOKENS];
static shell_node nodes[NODES];
static shell_variable variables[VARIABLES];
static shell_function functions[16];static int function_count,function_depth,source_depth,returning,return_status,nested_depth;
static int token_count,node_count,position,parse_error,var_count,last_status,leaving,exit_status,loop_break,loop_continue,loop_depth;
static int nesting,collecting,incomplete,substitution_status;
static const char *positional[32],*script_name="sh";
static int positional_count;
static char source[32768],expanded[1024],launch[1024];
static u8 expansion_mask[32768];static int expansion_quoted,heredoc_expanding;
static shell_heredoc heredocs[16];static int heredoc_count;
static char interactive_source[32768];
static u32 background[16];
static u32 background_pid[16],last_background_pid;
static int background_count;
static char pending_input[1024];
static int pending_count,pending_at;
static const char *syntax_error;
static int expansion_depth,delimiter_depth,expansion_fields_allowed,evaluation_depth;static u32 shell_pid;
static int expand_token(int,char *,int);
static int expand_token_mode(int,char *,int,int);
static int quote_argument(char *,int *,const char *);
static int wait_job(u32,int);
static int substitution(int,int,char *,int);
static int spawn_text(const char *,u32,int *);
static int execute_with_fds(const char *,int *);
static int nested_execute(const char *,int *);
static int evaluate(int,int *);
static int parameter_end(int,int);
static int balanced(int,int);
static int balanced_inner(int start,int end)
{
    int depth=1;char quote=0,quotes[24];quotes[0]=0;for(int at=start;at<end;at++){char c=source[at];
        if(c=='\\' && quote!='\''){at++;continue;}if(c=='\''||c=='"'){if(!quote)quote=c;else if(quote==c)quote=0;continue;}
        if(quote!='\'' && c=='$' && source[at+1]=='('){if(depth==24)return -1;quotes[depth++]=quote;quote=0;at++;continue;}
        if(quote!='\'' && c=='$' && source[at+1]=='{'){int close=parameter_end(at+2,end);if(close<0)return -1;at=close;continue;}
        if(quote)continue;if(c=='('){if(depth==24)return -1;quotes[depth++]=quote;quote=0;}
        else if(c==')'){if(!--depth)return at;quote=quotes[depth];}}
    return -1;
}
static int balanced(int start,int end)
{if(delimiter_depth==24)return -1;delimiter_depth++;int r=balanced_inner(start,end);delimiter_depth--;return r;}
static int parameter_end_inner(int start,int end)
{
    int depth=1;char quote=0,quotes[16];quotes[0]=0;
    for(int at=start;at<end;at++){char c=source[at];if(c=='\\' && quote!='\''){at++;continue;}
        if(c=='\''||c=='"'){if(!quote)quote=c;else if(quote==c)quote=0;continue;}
        if(quote!='\'' && c=='$' && source[at+1]=='('){int close=balanced(at+2,end);if(close<0)return -1;at=close;continue;}
        if(quote!='\'' && c=='$' && source[at+1]=='{'){if(depth==16)return -1;quotes[depth++]=quote;quote=0;at++;continue;}
        if(!quote && c=='}'){if(!--depth)return at;quote=quotes[depth];}}
    return -1;
}
static int parameter_end(int start,int end)
{if(delimiter_depth==24)return -1;delimiter_depth++;int r=parameter_end_inner(start,end);delimiter_depth--;return r;}
static void fail(const char *text){if(!parse_error){parse_error=1;syntax_error=text;}}
static int new_node(int kind,int a,int b,int c)
{
    if(node_count==NODES-1){fail("syntax tree capacity");return 0;}
    int i=++node_count;nodes[i]=(shell_node){kind,a,b,c,0,0,0,0};
    if(kind==SEQUENCE || kind==CONJUNCTION || kind==DISJUNCTION || kind==PIPELINE || kind==BACKGROUND || kind==NEGATE){
        nodes[i].start=nodes[a].start;nodes[i].end=nodes[b?b:a].end;}
    return i;
}
static int emit_token(int type,int start,int end)
{
    if(token_count==TOKENS){fail("token capacity");return -1;}
    tokens[token_count++]=(shell_token){type,start,end,-1};return 0;
}
static void lex(void)
{
    int at=0,waiting=-1,pending=0;heredoc_count=0;while(source[at] && !parse_error){
        char c=source[at];if(c==' ' || c=='\t' || c=='\r'){at++;continue;}
        if(c=='#'){while(source[at] && source[at]!='\n')at++;continue;}
        int start=at;
        if(c=='\n' || c==';'){if(c==';' && source[at+1]==';'){emit_token(ARM_END,at,at+2);at+=2;continue;}emit_token(';',at,at+1);at++;
            if(c=='\n'){if(waiting>=0){fail("here document needs delimiter");break;}
                for(int i=pending;i<heredoc_count;i++){shell_heredoc *h=&heredocs[i];h->begin=at;int found=0,write=at;
                    while(source[at]){int begin=at;while(source[at] && source[at]!='\n')at++;int end=at;if(end>begin && source[end-1]=='\r')end--;
                        if(h->strip_tabs)while(begin<end && source[begin]=='\t')begin++;
                        if(end-begin==length(h->delimiter)){int same=1;for(int j=begin;j<end;j++)if(source[j]!=h->delimiter[j-begin])same=0;
                            if(same){h->end=write;if(source[at])at++;found=1;break;}}
                        int next=source[at]?at+1:at;for(int j=begin;j<next;j++)source[write++]=source[j];at=next;}
                    if(!found){fail("unfinished here document");break;}}
                pending=heredoc_count;
            }continue;}
        if(c=='2' && source[at+1]=='>'){
            at+=2;if(source[at]=='>'){at++;emit_token(ERR_APPEND,start,at);}else emit_token(ERR_OUT,start,at);continue;
        }
        if(c=='|' || c=='&' || c=='>' || c=='<' || c=='(' || c==')'){
            at++;int type=c;
            if(c=='<' && source[at]=='<'){at++;type=HEREDOC;if(heredoc_count==16 || waiting>=0){fail("here document capacity");break;}
                waiting=heredoc_count++;heredocs[waiting]=(shell_heredoc){{0},0,0,0,0};if(source[at]=='-'){at++;heredocs[waiting].strip_tabs=1;}}
            if(source[at]==c && (c=='|' || c=='&' || c=='>')){at++;type=c=='|'?OR:c=='&'?AND:APPEND;}
            emit_token(type,start,at);continue;
        }
        char quote=0;
        while(source[at]){
            c=source[at];
            if(c=='$' && quote!='\'' && source[at+1]=='('){int close=balanced(at+2,length(source));
                if(close<0){fail("unfinished substitution");break;}at=close+1;continue;}
            if(c=='$' && quote!='\'' && source[at+1]=='{'){int close=parameter_end(at+2,length(source));
                if(close<0){fail("unfinished parameter expansion");break;}at=close+1;continue;}
            if(quote){
                if(c==quote){quote=0;at++;continue;}
                if(c=='\\' && quote=='"' && source[at+1])at+=2;else at++;continue;
            }
            if(c=='\'' || c=='"'){quote=c;at++;continue;}
            if(c=='\\'){if(!source[at+1] || (collecting && source[at+1]=='\n' && !source[at+2])){fail("unfinished escape");break;}at+=2;continue;}
            if(c==' ' || c=='\t' || c=='\r' || c=='\n' || c==';' || c=='|' || c=='&' || c=='>' || c=='<' || c=='(' || c==')')break;
            at++;
        }
        if(quote)fail("unfinished quote");if(at==start){fail("unexpected token");break;}
        emit_token(WORD,start,at);
        if(waiting>=0){shell_heredoc *h=&heredocs[waiting];int used=0;char q=0;h->quoted=0;
            for(int i=start;i<at;i++){char k=source[i];if(k=='\''||k=='"'){if(!q){q=k;h->quoted=1;continue;}if(q==k){q=0;continue;}}
                if(k=='\\' && q!='\'' && i+1<at){k=source[++i];h->quoted=1;}if(used==63 || k=='\n'){fail("here delimiter capacity");break;}h->delimiter[used++]=k;}
            h->delimiter[used]=0;tokens[token_count-1].heredoc=waiting;waiting=-1;}
    }
    if(waiting>=0 || pending<heredoc_count)fail("unfinished here document");
    if(token_count<TOKENS)emit_token(END,at,at);else tokens[TOKENS-1]=(shell_token){END,at,at};
}
static int keyword(const char *text)
{
    if(tokens[position].type!=WORD)return 0;int n=length(text);
    if(tokens[position].end-tokens[position].start!=n)return 0;
    for(int i=0;i<n;i++)if(source[tokens[position].start+i]!=text[i])return 0;return 1;
}
static int stop_word(void)
{return keyword("then") || keyword("elif") || keyword("else") || keyword("fi") || keyword("do") || keyword("done") || keyword("}") || keyword("esac") || tokens[position].type==ARM_END || tokens[position].type==')';}
static void separators(void){while(tokens[position].type==';')position++;}
static int list(void);
static int compound(void)
{
    if(++nesting>24){fail("nesting capacity");nesting--;return 0;}
    int start=tokens[position].start,node=0;
    if(tokens[position].type==WORD && position+3<token_count && tokens[position+1].type=='(' && tokens[position+2].type==')'){
        int name=position;position+=3;if(!keyword("{"))fail("function needs {");else position++;
        int begin=tokens[position-1].end;int body=list(),end=tokens[position].start;if(!keyword("}"))fail("function needs }");else position++;
        node=new_node(FUNCTION,body,name,0);nodes[node].first=begin;nodes[node].count=end-begin;
    }else if(keyword("case")){
        position++;int word=position;if(tokens[position].type!=WORD)fail("case needs word");else position++;
        if(!keyword("in"))fail("case needs in");else position++;separators();int first=0,last=0;
        while(!parse_error && !keyword("esac") && tokens[position].type!=END){
            if(tokens[position].type=='(')position++;int patterns=position;
            if(tokens[position].type!=WORD){fail("case needs pattern");break;}position++;
            while(tokens[position].type=='|'){position++;if(tokens[position].type!=WORD){fail("case needs pattern");break;}position++;}
            int amount=position-patterns;if(tokens[position].type!=')')fail("case needs )");else position++;
            int body=list(),arm=new_node(ARM,patterns,body,0);nodes[arm].count=amount;if(last)nodes[last].c=arm;else first=arm;last=arm;
            if(tokens[position].type==ARM_END){position++;separators();}else if(!keyword("esac"))fail("case needs ;;");
        }
        if(!keyword("esac"))fail("case needs esac");else position++;node=new_node(SELECT,word,first,0);
    }else if(keyword("if") || keyword("elif")){
        position++;int check=list();if(!keyword("then"))fail("if needs then");else position++;
        int yes=list(),no=0;if(keyword("elif"))no=compound();else {
            if(keyword("else")){position++;no=list();}if(!keyword("fi"))fail("if needs fi");else position++;}
        node=new_node(CONDITION,check,yes,no);
    }else if(keyword("while") || keyword("until")){
        int until=keyword("until");
        position++;int check=list();if(!keyword("do"))fail("while needs do");else position++;
        int body=list();if(!keyword("done"))fail("while needs done");else position++;
        node=new_node(LOOP,check,body,until);
    }else if(keyword("for")){
        position++;int first=position;if(tokens[position].type!=WORD)fail("for needs variable");else position++;
        int begin=position,count=-1;if(keyword("in")){position++;begin=position;while(tokens[position].type==WORD && !keyword("do"))position++;count=position-begin;}
        separators();if(!keyword("do"))fail("for needs do");else position++;
        int body=list();if(!keyword("done"))fail("for needs done");else position++;
        node=new_node(ITERATE,first,body,begin);nodes[node].count=count;
    }else if(tokens[position].type=='('){
        position++;int body=list();if(tokens[position].type!=')')fail("missing closing parenthesis");else position++;
        node=new_node(SUBSHELL,body,0,0);
    }else{
        int first=position;while(tokens[position].type==WORD || tokens[position].type=='<' || tokens[position].type=='>'
            || tokens[position].type==APPEND || tokens[position].type==ERR_OUT || tokens[position].type==ERR_APPEND || tokens[position].type==HEREDOC){
            int kind=tokens[position++].type;
            if(kind!=WORD){if(tokens[position].type!=WORD){fail("redirection needs a path");break;}position++;}
        }
        if(position==first)fail("expected a command");
        node=new_node(COMMAND,0,0,0);nodes[node].first=first;nodes[node].count=position-first;
    }
    nodes[node].start=start;nodes[node].end=position?tokens[position-1].end:start;nesting--;return node;
}
static int pipeline(void)
{
    int negate=keyword("!");if(negate)position++;
    int node=compound();while(!parse_error && tokens[position].type=='|'){
        position++;int right=compound();int next=new_node(PIPELINE,node,right,0);
        nodes[next].start=nodes[node].start;nodes[next].end=nodes[right].end;node=next;
    }
    if(negate)node=new_node(NEGATE,node,0,0);return node;
}
static int and_or(void)
{
    int node=pipeline();while(!parse_error && (tokens[position].type==AND || tokens[position].type==OR)){
        int kind=tokens[position++].type;node=new_node(kind==AND?CONJUNCTION:DISJUNCTION,node,pipeline(),0);
    }
    return node;
}
static int list(void)
{
    separators();int result=0;
    while(!parse_error && tokens[position].type!=END && !stop_word()){
        int node=and_or();if(tokens[position].type=='&'){position++;node=new_node(BACKGROUND,node,0,0);}
        result=result?new_node(SEQUENCE,result,node,0):node;
        if(tokens[position].type==';')separators();
        else if(tokens[position].type==END || stop_word())break;
        else if(nodes[node].kind!=BACKGROUND){fail("expected separator");break;}
    }
    return result;
}
static int variable_find(const char *name)
{for(int i=0;i<var_count;i++)if(equal(variables[i].name,name))return i;return -1;}
static int variable_set(const char *name,const char *value,int export)
{
    if(!*name || length(name)>31 || length(value)>255)return -1;
    for(int i=0;name[i];i++)if(!((name[i]>='a'&&name[i]<='z') || (name[i]>='A'&&name[i]<='Z') || name[i]=='_' || (i && name[i]>='0'&&name[i]<='9')))return -1;
    int found=variable_find(name),fresh=found<0;if(fresh){if(var_count==VARIABLES)return -4;found=var_count;}
    int exported=export || (!fresh && variables[found].exported);
    if(exported){int r=sc_env_set(name,value,0);if(r<0)return r;}
    if(fresh){var_count++;copy(variables[found].name,name,32);}variables[found].exported=exported;
    copy(variables[found].value,value,256);return 0;
}
static int arithmetic_variable(const char *name)
{char value[256];int found=variable_find(name),n=0;if(found>=0)copy(value,variables[found].value,256);else if(sc_user(4,name,value,256)<0)return 0;return cli_integer(value,&n)<0?0:n;}
static int field_separator(char out[256])
{int found=variable_find("IFS");if(found>=0)copy(out,variables[found].value,256);else {int r=sc_user(4,"IFS",out,256);if(r==-2)copy(out," \t\n",256);else if(r<0)return -1;}return 0;}
static int positional_join(char out[1024])
{char separators[256];if(field_separator(separators)<0)return -1;int used=0;for(int i=0;i<positional_count;i++){
    int n=length(positional[i]);if(used+n+(i && separators[0]?1:0)>=1024)return -1;if(i && separators[0])out[used++]=separators[0];
    for(int j=0;j<n;j++)out[used++]=positional[i][j];}out[used]=0;return 0;}
static int positional_expand(char *out,int used,int capacity)
{
    /* 引号中的$@保留每个参数，包括空串。内部NUL是显式字段边界，
     * 不写入argv；前缀贴第一项、后缀贴最后一项，避免先拼接再猜分隔。 */
    if(positional_count>1 && !expansion_fields_allowed)return -1;
    for(int i=0;i<positional_count;i++){int n=length(positional[i]);if(used+n+(i?1:0)>=capacity)return -1;
        if(i){out[used]=0;expansion_mask[used++]=4;}for(int j=0;j<n;j++){out[used]=positional[i][j];expansion_mask[used++]=0;}}
    return used;
}
static int parameter_value(int *cursor,int end,char name[32],char value[1024],int *set)
{
    int at=*cursor,n=0;*set=1;name[0]=0;char first=at<end?source[at]:0;
    if(first=='?'){decimal(value,last_status);at++;}
    else if(first=='#'){decimal(value,positional_count);at++;}
    else if(first=='$'){decimal(value,(int)shell_pid);at++;}
    else if(first=='!'){if(last_background_pid)decimal(value,(int)last_background_pid);else {*set=0;value[0]=0;}at++;}
    else if(first=='@' || first=='*'){at++;if(positional_join(value)<0)return -1;}
    else if(first>='0' && first<='9'){u32 index=0;while(at<end && source[at]>='0' && source[at]<='9'){if(index>32)return -1;index=index*10+(u32)(source[at++]-'0');}
        *set=index==0 || index<=(u32)positional_count;const char *word=index==0?script_name:*set?positional[index-1]:"";if(length(word)>=1024)return -1;copy(value,word,1024);}
    else {while(at<end){char c=source[at];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(n && c>='0'&&c<='9')))break;
            if(n==31)return -1;name[n++]=c;at++;}name[n]=0;if(!n)return -1;
        int found=variable_find(name);if(found>=0)copy(value,variables[found].value,1024);else {int r=sc_user(4,name,value,256);if(r==-2){*set=0;value[0]=0;}else if(r<0)return -1;}}
    *cursor=at;return 0;
}
static int parameter_operand(int token_index,int begin,int end,char out[1024],u8 *value_mask,int mask_size,int pattern_mode,int inherited_quote)
{
    if(expansion_depth==16)return -1;u8 *mask=mask_size?sc_alloc((u32)mask_size):0;if(mask_size && !mask)return -1;
    for(int i=0;i<mask_size;i++)mask[i]=expansion_mask[i];shell_token saved=tokens[token_index];int quoted=expansion_quoted;
    tokens[token_index].start=begin;tokens[token_index].end=end;int fields=expansion_fields_allowed;expansion_fields_allowed=0;
    expansion_depth++;int r=expand_token_mode(token_index,out,1024,inherited_quote);expansion_depth--;expansion_fields_allowed=fields;
    tokens[token_index]=saved;expansion_quoted=quoted || expansion_quoted;
    if(r>=0 && value_mask)for(int i=0;i<r;i++){if(expansion_mask[i]==4){r=-1;break;}value_mask[i]=expansion_mask[i]?1:0;}
    if(r>=0 && pattern_mode){char literal[1024];int n=0;
        for(int i=0;i<r;i++){char c=out[i];if(c=='\\' || (!(expansion_mask[i]&3) && (c=='*'||c=='?'||c=='['))){if(n==1023){r=-1;break;}literal[n++]='\\';}
            if(n==1023){r=-1;break;}literal[n++]=c;}if(r>=0){literal[n]=0;copy(out,literal,1024);r=n;}}
    for(int i=0;i<mask_size;i++)expansion_mask[i]=mask[i];if(mask)sc_free(mask);return r;
}
static int parameter_expand(int token_index,int start,int close,char value[1024],u8 value_mask[1024],int mask_size,int outer_quote)
{
    for(int i=0;i<1024;i++)value_mask[i]=1;
    int at=start,measure=source[at]=='#' && at+1<close;if(measure)at++;char name[32];int set;
    if(parameter_value(&at,close,name,value,&set)<0)return -1;
    if(measure){if(at!=close)return -1;decimal(value,length(value));return 0;}if(at==close)return 0;
    char op=source[at++];int colon=op==':';if(colon){if(at==close)return -1;op=source[at++];}
    if(op=='-'||op=='+'||op=='='||op=='?'){
        int absent=!set || (colon && !value[0]),expand=op=='+'?!absent:absent;
        /* 未选中的operand不得展开，否则${set:-$(side_effect)}也会
         * 偷偷运行命令。嵌套展开临时保存外层分词mask与token边界。 */
        if(!expand){if(op=='+')value[0]=0;return 0;}char operand[1024];if(parameter_operand(token_index,at,close,operand,value_mask,mask_size,0,outer_quote=='"'?'"':0)<0)return -1;
        if(op=='?'){cli_text(2,"sh: ");cli_text(2,name);cli_text(2,": ");cli_text(2,operand[0]?operand:"parameter unset");cli_text(2,"\n");return -1;}
        if(op=='=' && (!name[0] || variable_set(name,operand,0)<0))return -1;copy(value,operand,1024);return 0;
    }
    if(!colon && (op=='%' || op=='#')){int longest=at<close && source[at]==op;if(longest)at++;char pattern[1024];
        if(parameter_operand(token_index,at,close,pattern,0,mask_size,1,0)<0)return -1;int size=length(value),found=-1;
        for(int n=0;n<=size;n++){int cut=longest?size-n:n;char saved=value[cut];int match;
            if(op=='%')match=glob_match(value+size-cut,pattern);else {value[cut]=0;match=glob_match(value,pattern);value[cut]=saved;}
            if(match){found=cut;break;}}
        if(found>=0){if(op=='%')value[size-found]=0;else {for(int i=found;i<=size;i++)value[i-found]=value[i];}}return 0;
    }return -1;
}
static int expand_token_mode(int index,char *out,int capacity,int inherited_quote)
{
    shell_token *t=&tokens[index];int used=0;char quote=(char)inherited_quote;expansion_quoted=!!quote;if(capacity>32768)capacity=32768;
    for(int at=t->start;at<t->end;at++){
        char c=source[at];
        if(!heredoc_expanding && (c=='\'' || c=='"')){if(!quote){quote=c;expansion_quoted=1;continue;}if(quote==c){quote=0;continue;}}
        int escaped=0;if(c=='\\' && quote!='\'' && (quote!='"' || source[at+1]=='$' || source[at+1]=='"' || source[at+1]=='\\' || source[at+1]=='\n')
            && (!heredoc_expanding || source[at+1]=='$' || source[at+1]=='\\' || source[at+1]=='\n')){if(++at>=t->end)return -1;c=source[at];escaped=1;expansion_quoted=1;}
        else if(c=='$' && quote!='\''){
            if(source[at+1]=='{'){int close=parameter_end(at+2,t->end);char value[1024];u8 value_mask[1024];if(close<0)return -1;
                if(quote=='"' && close==at+3 && source[at+2]=='@'){used=positional_expand(out,used,capacity);if(used<0)return -1;at=close;continue;}
                if(parameter_expand(index,at+2,close,value,value_mask,used,quote)<0)return -1;
                int n=length(value);if(used+n>=capacity)return -1;for(int i=0;i<n;i++){expansion_mask[used]=!quote && value_mask[i];out[used++]=value[i];}at=close;continue;}
            if(source[at+1]=='('){int close=balanced(at+2,t->end);if(close<0)return -1;
                if(source[at+2]=='('){if(close<=at+3 || source[close-1]!=')')return -1;char expression[1024];int n=close-at-4;
                    if(n<0 || n>=1024)return -1;for(int i=0;i<n;i++)expression[i]=source[at+3+i];expression[n]=0;int answer;
                    if(arithmetic_value(expression,arithmetic_variable,&answer)<0)return -1;char number[12];decimal(number,answer);n=length(number);
                    if(used+n>=capacity)return -1;for(int i=0;i<n;i++){expansion_mask[used]=!quote;out[used++]=number[i];}
                }else {int n=substitution(at+2,close,out+used,capacity-used);if(n<0)return -1;for(int i=0;i<n;i++)expansion_mask[used+i]=!quote;used+=n;}
                at=close;continue;
            }
            if(source[at+1]=='@' && quote=='"'){used=positional_expand(out,used,capacity);if(used<0)return -1;at++;continue;}
            char name[32],value[1024];int n=0;
            if(source[at+1]=='?'){at++;decimal(value,last_status);}
            else if(source[at+1]=='#'){at++;decimal(value,positional_count);}
            else if(source[at+1]=='$'){at++;decimal(value,(int)shell_pid);}
            else if(source[at+1]=='!'){at++;if(last_background_pid)decimal(value,(int)last_background_pid);else value[0]=0;}
            else if(source[at+1]>='0' && source[at+1]<='9'){
                int index=source[++at]-'0';const char *word=index==0?script_name:index<=positional_count?positional[index-1]:"";
                if(length(word)>=1024)return -1;copy(value,word,1024);
            }
            else if(source[at+1]=='@' || source[at+1]=='*'){
                at++;if(positional_join(value)<0)return -1;
            }
            else {
                while(at+1<t->end){char k=source[at+1];if(!((k>='a'&&k<='z') || (k>='A'&&k<='Z') || k=='_' || (k>='0'&&k<='9')))break;
                    if(n==31)return -1;name[n++]=k;at++;}
                name[n]=0;if(!n){if(used+1>=capacity)return -1;expansion_mask[used]=!quote;out[used++]='$';continue;}
                int found=variable_find(name);if(found>=0)copy(value,variables[found].value,1024);
                else if(sc_user(4,name,value,256)<0)value[0]=0;
            }
            int size=length(value);if(used+size>=capacity)return -1;for(int i=0;i<size;i++){expansion_mask[used]=!quote;out[used++]=value[i];}continue;
        }
        if(c=='\n' && at>t->start && source[at-1]=='\\')continue;
        if(c=='~' && at==t->start && !quote && !escaped && (at+1==t->end || source[at+1]=='/')){char home[64];if(sc_user(1,"",home,64)<0)return -1;int n=length(home);
            if(n+used>=capacity)return -1;for(int i=0;i<n;i++){expansion_mask[used]=0;out[used++]=home[i];}continue;}
        if(used+1>=capacity)return -1;expansion_mask[used]=!quote&&!escaped?2:0;out[used++]=c;
    }
    out[used]=0;return used;
}
static int expand_token(int index,char *out,int capacity){return expand_token_mode(index,out,capacity,0);}
static int quote_argument(char *out,int *used,const char *word)
{
    if(*used){if(*used==1023)return -1;out[(*used)++]=' ';}
    /* 简单非空字段原样传递，只有空字段/分隔符/引号/反斜杠才编码。
     * 新CLI仍能无损解析字段；旧GETARGS只返回原文本，老M7探针/GUI
     * 不会替调用者去引号，强制给handler加双引号会悄悄改变其行为。
     * 这里修Shell序列化，旧调用及内核128B参数视图均不改语义。 */
    int quoted=!word[0];for(int i=0;word[i];i++)
        if((u8)word[i]<33 || word[i]=='\'' || word[i]=='"' || word[i]=='\\')quoted=1;
    if(!quoted){for(int i=0;word[i];i++){if(*used==1023)return -1;out[(*used)++]=word[i];}out[*used]=0;return 0;}
    if(*used==1023)return -1;out[(*used)++]='"';
    for(int i=0;word[i];i++){
        if(word[i]=='"' || word[i]=='\\'){if(*used==1023)return -1;out[(*used)++]='\\';}
        if(*used==1023)return -1;out[(*used)++]=word[i];
    }
    if(*used==1023)return -1;out[(*used)++]='"';out[*used]=0;return 0;
}
static int take_input(void)
{
    if(pending_at<pending_count)return (u8)pending_input[pending_at++];pending_at=pending_count=0;
    u8 byte;int count=sc_stream_read(0,&byte,1);return count==1?byte:count==0?-2:count==-6?-1:-3;
}
static int wait_job(u32 job,int allow_input)
{
    int event=allow_input?sc_stream_event(0,1):-1,result_code=1;
    u32 status[8];for(;;){int result=sc_wait2(job,status);if(result<0)break;
        if(!result){result_code=(int)status[2];break;}
        if(event>=0 && sc_stream_event(0,0)!=event){if(status[3])sc_kill_generation((int)status[3],status[4]);event=sc_stream_event(0,0);}
        sc_yield();
    }if(allow_input && event>=0)sc_stream_event(0,2);return result_code;
}
static int substitution(int begin,int end,char *out,int capacity)
{
    if(end-begin>32767 || capacity<1)return -1;
    int pipe[2];if(sc_stream_pipe(pipe)<0)return -1;int fds[3]={0,pipe[1],2};int job=spawn_text(source+begin,(u32)(end-begin),fds);sc_stream_close(pipe[1],0);
    if(job<0){sc_stream_close(pipe[0],0);return -1;}int n=0,error=0,event=sc_stream_event(0,1);
    /* 一边读一边让子程序推进，不能先wait把有限管道写满后互等。
     * 超过命令行容量就取消作业，绝不截断替换结果继续执行。 */
    for(;;){u8 bytes[256];int got=sc_stream_read(pipe[0],bytes,256);
        if(event>=0 && sc_stream_event(0,0)!=event){error=1;break;}if(got==-6){sc_yield();continue;}if(got<0){error=1;break;}if(!got)break;
        if(got>capacity-1-n){error=1;break;}for(int i=0;i<got;i++)if(bytes[i])out[n++]=(char)bytes[i];else {error=1;break;}if(error)break;
    }
    if(error){u32 state[8];if(sc_job_info2((u32)job,state)>0 && state[3])sc_kill_generation((int)state[3],state[4]);}
    sc_stream_close(pipe[0],0);if(event>=0)sc_stream_event(0,2);substitution_status=wait_job((u32)job,0);
    if(error)return -1;while(n && out[n-1]=='\n')n--;out[n]=0;return n;
}
static int builtin(char **args,int count,int fds[3],int *handled)
{
    *handled=1;if(!count)return 0;
    if(equal(args[0],"cd")){char home[64];if(count>2)return 2;if(count==1){if(sc_user(1,"",home,64)<0)return 1;
            char rooted[65];rooted[0]='/';copy(rooted+1,home,64);return sc_chdir(rooted)<0?1:0;}return sc_chdir(args[1])<0?1:0;}
    if(equal(args[0],"exit")){exit_status=last_status;if(count==2 && cli_integer(args[1],&exit_status)<0)return 2;if(count>2)return 2;leaving=1;return exit_status;}
    if(equal(args[0],"return")){if(!function_depth && !source_depth)return 2;return_status=last_status;
        if(count>2 || (count==2 && cli_integer(args[1],&return_status)<0))return 2;returning=1;return return_status;}
    if(equal(args[0],"break") || equal(args[0],"continue")){
        int levels=1;if(!loop_depth || count>2 || (count==2 && cli_integer(args[1],&levels)<0) || levels<=0)return 2;
        if(levels>loop_depth)levels=loop_depth;if(equal(args[0],"break"))loop_break=levels;else loop_continue=levels;return 0;}
    if(equal(args[0],":"))return 0;
    if(equal(args[0],"shift")){
        int n=1;if(count>2 || (count==2 && cli_integer(args[1],&n)<0) || n<0 || n>positional_count)return 2;
        for(int i=n;i<positional_count;i++)positional[i-n]=positional[i];positional_count-=n;return 0;
    }
    if(equal(args[0],"read")){
        if(count!=2)return 2;char value[256];int used=0;
        for(;;){u8 byte;int n=cli_read(fds[0],&byte,1);if(n<=0)return 1;if(byte=='\n')break;if(byte!='\r'){if(used==255)return 1;value[used++]=(char)byte;}}
        value[used]=0;return variable_set(args[1],value,0)<0?1:0;
    }
    if(equal(args[0],"export")){
        if(count==1){char values[8192];int n=sc_env_list(values,sizeof(values));return n<0?1:cli_write(fds[1],values,(u32)n)<0?1:0;}
        for(int i=1;i<count;i++){char *p=args[i];while(*p && *p!='=')p++;
            if(*p){*p++=0;if(variable_set(args[i],p,1)<0)return 1;}
            else {int index=variable_find(args[i]);char value[256];
                if(index>=0)copy(value,variables[index].value,256);else if(sc_user(4,args[i],value,256)<0)value[0]=0;
                if(variable_set(args[i],value,1)<0)return 1;}
        }return 0;
    }
    if(equal(args[0],"unset")){
        for(int i=1;i<count;i++){int found=variable_find(args[i]);if(found>=0){if(variables[found].exported && sc_env_set(args[i],0,1)<0)return 1;
                variables[found]=variables[--var_count];}else if(sc_env_set(args[i],0,1)<0)return 1;}return 0;
    }
    if(equal(args[0],"jobs")){
        if(count!=1)return 2;for(int i=0;i<background_count;i++){u32 status[8];int r=sc_job_info2(background[i],status);
            cli_number(fds[1],(int)background_pid[i]);cli_text(fds[1],r>0?" running\n":" finished\n");}return 0;
    }
    if(equal(args[0],"wait")){if(count==1){for(int i=0;i<background_count;i++)wait_job(background[i],0);background_count=0;return 0;}
        int result=0;for(int a=1;a<count;a++){int pid;if(cli_integer(args[a],&pid)<0 || pid<=0)return 2;int found=-1;
            for(int i=background_count-1;i>=0;i--)if(background_pid[i]==(u32)pid){found=i;break;}
            if(found<0){result=127;continue;}result=wait_job(background[found],0);for(int i=found;i+1<background_count;i++){background[i]=background[i+1];background_pid[i]=background_pid[i+1];}background_count--;}
        return result;}
    if(equal(args[0],".") || equal(args[0],"source")){
        if(count<2)return 2;int fd=sc_stream_open(args[1],1,0);if(fd<0)return 1;u32 bytes;char *text=cli_slurp(fd,32767,&bytes);sc_stream_close(fd,0);if(!text)return 1;
        for(u32 i=0;i<bytes;i++)if(!text[i]){sc_free(text);return 2;}
        const char *saved[32];int old=positional_count;for(int i=0;i<old;i++)saved[i]=positional[i];
        if(count>2){positional_count=count-2;for(int i=0;i<positional_count;i++)positional[i]=args[i+2];}
        source_depth++;int result=nested_execute(text,fds);source_depth--;if(returning){result=return_status;returning=0;}
        if(count>2){positional_count=old;for(int i=0;i<old;i++)positional[i]=saved[i];}sc_free(text);return result;
    }
    if(equal(args[0],"eval")){
        char *text=sc_alloc(32768);if(!text)return 1;int used=0;for(int i=1;i<count;i++){int n=length(args[i]);if(n+used+2>32768){sc_free(text);return 2;}if(i>1)text[used++]=' ';for(int j=0;j<n;j++)text[used++]=args[i][j];}
        text[used]=0;int result=nested_execute(text,fds);sc_free(text);return result;
    }
    for(int i=0;i<function_count;i++)if(equal(args[0],functions[i].name)){
        if(function_depth==16)return 2;const char *saved[32];int old=positional_count;for(int j=0;j<old;j++)saved[j]=positional[j];
        positional_count=count-1;for(int j=0;j<positional_count;j++)positional[j]=args[j+1];function_depth++;
        int n=length(functions[i].body);char *body=sc_alloc((u32)n+1);if(!body){function_depth--;positional_count=old;for(int j=0;j<old;j++)positional[j]=saved[j];return 1;}
        copy(body,functions[i].body,n+1);int result=nested_execute(body,fds);sc_free(body);function_depth--;if(returning){result=return_status;returning=0;}
        positional_count=old;for(int j=0;j<old;j++)positional[j]=saved[j];return result;
    }
    *handled=0;return 0;
}
static int add_argument(char *arena,int *used,char **args,int *count,const char *word)
{int n=length(word);if(*count==32 || n+1>2048-*used)return -1;args[(*count)++]=arena+*used;copy(arena+*used,word,n+1);*used+=n+1;return 0;}
static int glob_arguments(const char *pattern,const char *prefix,int depth,char *arena,int *used,char **args,int *count)
{
    if(depth>32)return -1;char component[128];int n=0,wild=0;const char *next=pattern;
    while(*next && *next!='/'){if(n==127)return -1;if(*next=='\\' && next[1]){component[n++]=*next++;if(n==127)return -1;component[n++]=*next++;}
        else {if(*next=='*'||*next=='?'||*next=='[')wild=1;component[n++]=*next++;}}component[n]=0;
    int last=!*next;if(!last)next++;char parent[64];if(cli_path(*prefix?prefix:".",parent)<0)return -1;int matches=0;
    if(!wild){char literal[64];int l=0;for(int i=0;i<n;i++){char c=component[i];if(c=='\\' && component[i+1])c=component[++i];if(l==63)return -1;literal[l++]=c;}literal[l]=0;
        char child[64];if(length(prefix)+l+2>64)return -1;copy(child,prefix,64);if(*child && child[length(child)-1]!='/')append(child,"/",64);append(child,literal,64);
        if(last){char resolved[64];u32 info[2];if(cli_path(child,resolved)<0 || sc_stat(resolved,info)<0)return 0;return add_argument(arena,used,args,count,child)<0?-1:1;}
        return glob_arguments(next,child,depth+1,arena,used,args,count);
    }
    char *list=sc_alloc(65536);if(!list)return -1;int got=sc_dir(parent,list,65536);if(got<0){sc_free(list);return 0;}
    const char *p=list;while(*p){char kind=*p++;if(*p++!=' '){matches=-1;break;}char name[64];int size=0;
        while(*p && *p!=' ' && *p!='\n'){if(size==63){matches=-1;break;}name[size++]=*p++;}name[size]=0;while(*p && *p!='\n')p++;if(*p)p++;if(matches<0)break;
        if((name[0]=='.' && component[0]!='.') || !glob_match(name,component) || (!last && kind!='D'))continue;
        char child[64];if(length(prefix)+size+2>64){matches=-1;break;}copy(child,prefix,64);if(*child && child[length(child)-1]!='/')append(child,"/",64);append(child,name,64);
        int r=last?(add_argument(arena,used,args,count,child)<0?-1:1):glob_arguments(next,child,depth+1,arena,used,args,count);
        if(r<0){matches=-1;break;}matches+=r;
    }sc_free(list);return matches;
}
static int assignment_token(int index)
{int n=0;for(int at=tokens[index].start;at<tokens[index].end;at++){char c=source[at];if(c=='=')return n>0;if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(n && c>='0'&&c<='9')))return 0;n++;}return 0;}
static int word_field(int begin,int end,char *arena,int *used,char **args,int *count)
{
    char word[1024],pattern[2048];int pn=0,wild=0;for(int i=begin;i<end;i++){word[i-begin]=expanded[i];char c=expanded[i];
        if(c=='*'||c=='?'||c=='['){if(expansion_mask[i]&3)wild=1;else pattern[pn++]='\\';}else if(c=='\\')pattern[pn++]='\\';pattern[pn++]=c;}
    word[end-begin]=0;pattern[pn]=0;int previous=*count;
    if(wild){const char *part=pattern,*prefix="";if(*part=='/'){part++;prefix="/";}int r=glob_arguments(part,prefix,0,arena,used,args,count);if(r<0)return -1;
        for(int i=previous+1;i<*count;i++){char *v=args[i];int j=i;while(j>previous && text_compare(args[j-1],(u32)length(args[j-1]),v,(u32)length(v),0)>0){args[j]=args[j-1];j--;}args[j]=v;}}
    return previous==*count?add_argument(arena,used,args,count,word):0;
}
static int field_kind(int at,const char *ifs)
{if(expansion_mask[at]!=1)return 0;char c=expanded[at];for(int i=0;ifs[i];i++)if(ifs[i]==c)return c==' '||c=='\t'||c=='\n'?1:2;return 0;}
static int word_arguments(int index,char *arena,int *used,char **args,int *count,int assignment)
{
    shell_token *t=&tokens[index];int bytes=t->end-t->start;
    if((bytes==4 && source[t->start]=='"' && source[t->start+1]=='$' && source[t->start+2]=='@' && source[t->start+3]=='"')
        || (bytes==6 && source[t->start]=='"' && source[t->start+1]=='$' && source[t->start+2]=='{' && source[t->start+3]=='@' && source[t->start+4]=='}' && source[t->start+5]=='"')){
        for(int i=0;i<positional_count;i++)if(add_argument(arena,used,args,count,positional[i])<0)return -1;return 0;}
    int fields=expansion_fields_allowed;expansion_fields_allowed=!assignment;int n=expand_token(index,expanded,sizeof(expanded));expansion_fields_allowed=fields;
    if(n<0)return -1;if(assignment)return add_argument(arena,used,args,count,expanded);if(!n)return expansion_quoted?add_argument(arena,used,args,count,""):0;
    char ifs[256];if(field_separator(ifs)<0)return -1;int previous=*count,start=0;
    for(int boundary=0;boundary<=n;boundary++)if(boundary==n || expansion_mask[boundary]==4){
        int at=start;while(at<boundary && field_kind(at,ifs)==1)at++;
        if(at==boundary){if((start || boundary<n) && word_field(at,at,arena,used,args,count)<0)return -1;}
        while(at<boundary){int end=at;while(end<boundary && !field_kind(end,ifs))end++;
            if(word_field(at,end,arena,used,args,count)<0)return -1;at=end;
            while(at<boundary && field_kind(at,ifs)==1)at++;
            if(at<boundary && field_kind(at,ifs)==2)at++;
            while(at<boundary && field_kind(at,ifs)==1)at++;}
        start=boundary+1;
    }
    return previous==*count && expansion_quoted?add_argument(arena,used,args,count,""):0;
}
static int command(int index,int defaults[3],int asynchronous)
{
    if(command_depth==16)return cli_error("sh: command nesting",-1);
    /* 每个同时活跃的命令层一个有界缓存，第一次才分配；函数递归
     * 必须保留外层args/旧环境，普通循环则复用同一层，不能每次
     * 都申请10KiB或把32条旧环境放在每个递归栈上。退出由任务页
     * 回收；缓存只属于本进程，槽复用不会继承这份地址空间。 */
    if(!command_frames[command_depth]){command_frames[command_depth]=sc_alloc(sizeof(shell_command_frame));if(!command_frames[command_depth])return 1;}
    shell_command_frame *frame=command_frames[command_depth++];char *arena=frame->arena,**args=frame->args;
    shell_node *node=&nodes[index];int count=0,used=0;
    int fds[3]={defaults[0],defaults[1],defaults[2]},opened[3]={-1,-1,-1},output[3]={0,0,0};
    shell_temporary *temporary=frame->temporary;int temporary_count=0;
    int result=0,assignments_only=1;substitution_status=0;
    for(int at=node->first;at<node->first+node->count;at++){
        int type=tokens[at].type;
        if(type!=WORD){
            int target=type=='<' || type==HEREDOC?0:type==ERR_OUT || type==ERR_APPEND?2:1;
            if(type==HEREDOC){int delimiter=++at,h=tokens[delimiter].heredoc;if(h<0 || h>=heredoc_count){result=2;goto done;}
                shell_heredoc *body=&heredocs[h];char *text=sc_alloc(32768);if(!text){result=1;goto done;}int n=body->end-body->begin;
                if(body->quoted){for(int i=0;i<n;i++)text[i]=source[body->begin+i];}
                else {shell_token saved=tokens[delimiter];tokens[delimiter]=(shell_token){WORD,body->begin,body->end,-1};heredoc_expanding=1;
                    n=expand_token(delimiter,text,32768);heredoc_expanding=0;tokens[delimiter]=saved;}
                if(n<0){sc_free(text);result=2;goto done;}if(opened[0]>=0)sc_stream_close(opened[0],0);
                opened[0]=sc_stream_memory(text,(u32)n);sc_free(text);if(opened[0]<0){result=1;goto done;}fds[0]=opened[0];continue;}
            if(expand_token(++at,expanded,sizeof(expanded))<0){result=2;goto done;}
            /* 增长上限由Shell的REDIRECT_LIMIT变量指定，默认1MiB；满时
             * 返回错误且不发布。追加先复制旧正文再运行子任务，仍COW。 */
            u32 limit=1048576;char value[256];int configured;
            if(sc_user(4,"REDIRECT_LIMIT",value,sizeof(value))>=0 && !cli_integer(value,&configured) && configured>=0)limit=(u32)configured;
            if(opened[target]>=0)sc_stream_close(opened[target],0);
            if(type==APPEND || type==ERR_APPEND){
                int input=sc_stream_open(expanded,1,0);int fd=sc_stream_open(expanded,2,limit);
                if(fd<0){if(input>=0)sc_stream_close(input,0);result=1;goto done;}
                if(input>=0){result=cli_copy_fd(input,fd);sc_stream_close(input,0);if(result<0){sc_stream_close(fd,0);result=1;goto done;}}
                opened[target]=fd;
            }else opened[target]=sc_stream_open(expanded,type=='<'?1:2,type=='<'?0:limit);
            if(opened[target]<0){result=1;goto done;}fds[target]=opened[target];output[target]=target!=0;continue;
        }
        int assignment=assignments_only && assignment_token(at);if(!assignment)assignments_only=0;
        if(word_arguments(at,arena,&used,args,&count,assignment)<0){result=2;goto done;}
    }
    int first=0;
    while(first<count){char *equal_sign=args[first];while(*equal_sign && *equal_sign!='=')equal_sign++;
        if(!*equal_sign)break;*equal_sign++=0;temporary[first].name=args[first];temporary[first].value=equal_sign;first++;}
    if(first==count){for(int i=0;i<first;i++)if(variable_set(temporary[i].name,temporary[i].value,0)<0){result=2;goto done;}result=substitution_status;goto done;}
    else for(int i=0;i<first;i++){
        temporary[i].found=sc_user(4,temporary[i].name,temporary[i].old,256)>=0;
        if(sc_env_set(temporary[i].name,temporary[i].value,0)<0){result=2;goto done;}temporary_count++;
    }
    int handled;result=builtin(args+first,count-first,fds,&handled);
    if(handled)goto done;
    if(asynchronous && (opened[1]>=0 || opened[2]>=0)){result=cli_error("background redirection needs a subshell",-1);goto done;}
    /* 程序名不带引号交给内核PATH解析；后续参数保留引号和转义。 */
    int length_used=0;copy(launch,args[first],sizeof(launch));length_used=length(launch);
    for(int i=first+1;i<count;i++)if(quote_argument(launch,&length_used,args[i])<0){result=2;goto done;}
    int job=sc_spawn2(launch,fds,0);if(job<0){result=cli_error(args[first],job);goto done;}
    if(asynchronous){u32 state[8];if(sc_job_info2((u32)job,state)<0){result=1;goto done;}last_background_pid=state[5];
        if(background_count==16){result=wait_job((u32)job,0);}else{background_pid[background_count]=state[5];background[background_count++]=(u32)job;result=0;}}
    else result=wait_job((u32)job,defaults[0]==0);
done:
    /* exec创建时环境快照已归子进程；前缀赋值不污染父Shell下一条命令。
     * 恢复只动当前任务的环境，子进程保留其独立引用/快照。 */
    for(int i=temporary_count-1;i>=0;i--)sc_env_set(temporary[i].name,temporary[i].found?temporary[i].old:0,!temporary[i].found);
    for(int i=0;i<3;i++)if(opened[i]>=0){int r=sc_stream_close(opened[i],output[i] && result>=0);
        if(r<0){sc_stream_close(opened[i],0);result=1;}}
    command_depth--;return result;
}
static int flatten(int node,int stages[8],int *count)
{
    if(nodes[node].kind==PIPELINE){if(flatten(nodes[node].a,stages,count)<0)return -1;return flatten(nodes[node].b,stages,count);}
    if(*count==8)return -1;stages[(*count)++]=node;return 0;
}
static int spawn_text(const char *text,u32 bytes,int *fds)
{
    if(bytes>=32768)return -1;char *script=sc_alloc(32768);if(!script)return -1;int used=0;
    /* 子Shell继承未导出的本地变量。通过自己的代码快照还原，不能
     * 暂时污染内核环境、更不能跨UID传递另一个账户的打开文件。 */
    for(int i=0;i<var_count;i++){int n=length(variables[i].name),v=length(variables[i].value);if(used+n+v*4+5>=32768){sc_free(script);return -1;}
        for(int j=0;j<n;j++)script[used++]=variables[i].name[j];script[used++]='=';script[used++]='\'';
        for(int j=0;j<v;j++){char c=variables[i].value[j];if(c=='\''){script[used++]='\'';script[used++]='\\';script[used++]='\'';script[used++]='\'';}else script[used++]=c;}
        script[used++]='\'';script[used++]='\n';}
    for(int i=0;i<function_count;i++){int name=length(functions[i].name),body=length(functions[i].body);if(name+body+9>32767-used){sc_free(script);return -1;}
        for(int j=0;j<name;j++)script[used++]=functions[i].name[j];const char *open="() {\n";for(int j=0;open[j];j++)script[used++]=open[j];
        for(int j=0;j<body;j++)script[used++]=functions[i].body[j];script[used++]='\n';script[used++]='}';script[used++]='\n';}
    if(bytes>32767u-(u32)used){sc_free(script);return -1;}for(u32 i=0;i<bytes;i++)script[used++]=text[i];script[used]=0;
    char arguments[1024];arguments[0]=0;int arg_used=0;if(quote_argument(arguments,&arg_used,script_name)<0){sc_free(script);return -1;}
    for(int i=0;i<positional_count;i++)if(quote_argument(arguments,&arg_used,positional[i])<0){sc_free(script);return -1;}
    int job=sc_spawn_buffer(script,(u32)used,fds,arguments);sc_free(script);return job;
}
static int spawn_fragment(int node,int fds[3])
{
    int begin=nodes[node].start,end=nodes[node].end,n=end-begin;if(n<0 || n>32767)return -1;
    char *fragment=sc_alloc(32768);if(!fragment)return -1;for(int i=0;i<n;i++)fragment[i]=source[begin+i];int appended=0;
    for(int i=0;i<token_count;i++)if(tokens[i].heredoc>=0 && tokens[i].start>=begin && tokens[i].end<=end){
        shell_heredoc *h=&heredocs[tokens[i].heredoc];if(h->begin<end)continue;int body=h->end-h->begin,d=length(h->delimiter);
        if(n+body+d+3>32767){sc_free(fragment);return -1;}if(!appended){fragment[n++]='\n';appended=1;}for(int j=0;j<body;j++)fragment[n++]=source[h->begin+j];
        for(int j=0;j<d;j++)fragment[n++]=h->delimiter[j];fragment[n++]='\n';}
    fragment[n]=0;int job=spawn_text(fragment,(u32)n,fds);sc_free(fragment);return job;
}
static int run_pipeline(int node,int defaults[3])
{
    int stages[8],count=0;if(flatten(node,stages,&count)<0)return 2;
    int links[7][2];u32 jobs[8];int launched=0,created=0,result=0;
    for(int i=0;i<count-1;i++){if(sc_stream_pipe(links[i])<0){result=1;goto cleanup;}created++;}
    for(int i=0;i<count;i++){
        int fds[3]={i?links[i-1][0]:defaults[0],i+1<count?links[i][1]:defaults[1],defaults[2]};
        int job=spawn_fragment(stages[i],fds);if(job<0){result=1;goto cleanup;}jobs[launched++]=(u32)job;
    }
cleanup:
    for(int i=0;i<created;i++){sc_stream_close(links[i][0],0);sc_stream_close(links[i][1],0);}
    for(int i=0;i<launched;i++){int r=wait_job(jobs[i],defaults[0]==0);
        if(r==130)for(int j=i+1;j<launched;j++){u32 status[8];if(sc_job_info2(jobs[j],status)>0 && status[3])sc_kill_generation((int)status[3],status[4]);}
        if(!result || i==launched-1)result=r;}
    return result;
}
static int evaluate_select(shell_node *n,int fds[3])
{
    char *storage=sc_alloc(3072);if(!storage)return 1;char *word=storage,*pattern=storage+1024;int result=0;
    if(expand_token(n->a,word,1024)<0){sc_free(storage);return 2;}
    for(int arm=n->b;arm;arm=nodes[arm].c){int chosen=0;for(int i=0;i<nodes[arm].count;i+=2){int bytes=expand_token(nodes[arm].a+i,expanded,1024),used=0;if(bytes<0){result=2;goto done;}
            for(int j=0;j<bytes;j++){char c=expanded[j];if(c=='\\' || (!expansion_mask[j] && (c=='*'||c=='?'||c=='[')))pattern[used++]='\\';pattern[used++]=c;}pattern[used]=0;
            if(glob_match(word,pattern)){chosen=1;break;}}
        if(chosen){result=evaluate(nodes[arm].b,fds);break;}}
done:sc_free(storage);return result;
}
static int evaluate_iterate(shell_node *n,int fds[3])
{
    char name[32];if(expand_token(n->a,name,sizeof(name))<0)return 2;char *arena=sc_alloc(2048),*words[32];if(!arena)return 1;int used=0,count=0,result=0;
    /* 词列只展开一次，缓冲归当前循环；放堆而非每个evaluate栈帧
     * 都留2KiB，嵌套循环仍保有独立词列，结束统一回收。 */
    if(n->count<0){for(int i=0;i<positional_count;i++)if(add_argument(arena,&used,words,&count,positional[i])<0){result=2;goto done;}}
    else for(int i=0;i<n->count;i++)if(word_arguments(n->c+i,arena,&used,words,&count,0)<0){result=2;goto done;}
    loop_depth++;for(int i=0;i<count && !leaving && !returning;i++){
        if(variable_set(name,words[i],0)<0){result=2;break;}result=evaluate(n->b,fds);if(loop_break){loop_break--;break;}if(loop_continue && --loop_continue)break;sc_yield();}loop_depth--;
done:sc_free(arena);return result;
}
static int evaluate_inner(int node,int fds[3])
{
    if(!node || leaving || returning || loop_break || loop_continue)return last_status;
    shell_node *n=&nodes[node];int result=0;
    if(n->kind==COMMAND)result=command(node,fds,0);
    else if(n->kind==SEQUENCE || n->kind==CONJUNCTION || n->kind==DISJUNCTION){int pending[NODES],count=0,at=node;
        /* 长平铺脚本形成左深AST，与语言嵌套不是同一回事。迭代
         * 展开左链，再按原顺序短路执行；不能为256个顺序命令占
         * 256个大evaluate栈帧，超过SCX的128KiB栈。 */
        while(at && (nodes[at].kind==SEQUENCE || nodes[at].kind==CONJUNCTION || nodes[at].kind==DISJUNCTION)){if(count==NODES)return 2;pending[count++]=at;at=nodes[at].a;}
        result=evaluate(at,fds);while(count && !leaving && !returning && !loop_break && !loop_continue){shell_node *part=nodes+pending[--count];
            if(part->kind==SEQUENCE || (part->kind==CONJUNCTION?!result:result))result=evaluate(part->b,fds);}}
    else if(n->kind==NEGATE)result=!evaluate(n->a,fds);
    else if(n->kind==PIPELINE)result=run_pipeline(node,fds);
    else if(n->kind==BACKGROUND){
        int child_fds[3]={fds[0],fds[1],fds[2]},null_input=-1;
        if(fds[0]==0){null_input=sc_stream_open("/DEV/NULL",4,0);if(null_input<0)return 1;child_fds[0]=null_input;}
        int job=spawn_fragment(n->a,child_fds);if(null_input>=0)sc_stream_close(null_input,0);if(job<0)result=1;
        else {u32 state[8];if(sc_job_info2((u32)job,state)<0)result=1;else {last_background_pid=state[5];
            if(background_count==16)result=wait_job((u32)job,0);else {background_pid[background_count]=state[5];background[background_count++]=(u32)job;}}}
    }else if(n->kind==SUBSHELL){int job=spawn_fragment(n->a,fds);result=job<0?1:wait_job((u32)job,fds[0]==0);}
    else if(n->kind==CONDITION)result=evaluate(n->a,fds)?evaluate(n->c,fds):evaluate(n->b,fds);
    else if(n->kind==FUNCTION){char name[32];if(expand_token(n->b,name,32)<=0)return 2;
        for(int i=0;name[i];i++)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')||name[i]=='_'||(i && name[i]>='0'&&name[i]<='9')))return 2;
        int index=-1;for(int i=0;i<function_count;i++)if(equal(name,functions[i].name))index=i;if(index<0 && function_count==16)return 2;
        u32 bytes=(u32)n->count;char *body=sc_alloc(bytes+1);if(!body)return 1;for(u32 i=0;i<bytes;i++)body[i]=source[n->first+(int)i];body[bytes]=0;
        if(index<0){index=function_count++;functions[index].body=0;}if(functions[index].body)sc_free(functions[index].body);functions[index].body=body;copy(functions[index].name,name,32);}
    else if(n->kind==SELECT)result=evaluate_select(n,fds);
    else if(n->kind==LOOP){
        loop_depth++;while(!leaving && !returning){int test=evaluate(n->a,fds);
            if(loop_break){loop_break--;break;}if(loop_continue){if(--loop_continue)break;continue;}
            if(leaving || returning || (n->c?test==0:test!=0))break;result=evaluate(n->b,fds);
            if(loop_break){loop_break--;break;}if(loop_continue && --loop_continue)break;sc_yield();}loop_depth--;
    }else if(n->kind==ITERATE)result=evaluate_iterate(n,fds);
    last_status=result;return result;
}
static int evaluate(int node,int fds[3])
{if(evaluation_depth==32)return cli_error("sh: execution nesting",-1);evaluation_depth++;int result=evaluate_inner(node,fds);evaluation_depth--;return result;}
static int execute_with_fds(const char *text,int *fds)
{
    if(length(text)>=32768)return 2;if(text!=source)copy(source,text,sizeof(source));
    token_count=node_count=position=parse_error=nesting=0;if(!nested_depth)loop_break=loop_continue=loop_depth=0;lex();
    int root=list();if(tokens[position].type!=END)fail("unexpected closing keyword");
    incomplete=0;
    if(parse_error){
        if(collecting && (tokens[position].type==END || equal(syntax_error,"unfinished quote") || equal(syntax_error,"unfinished escape") || equal(syntax_error,"unfinished substitution") || equal(syntax_error,"unfinished parameter expansion") || equal(syntax_error,"unfinished here document"))){incomplete=1;return 2;}
        cli_text(2,"sh: ");cli_text(2,syntax_error);cli_text(2,"\n");return 2;
    }
    return evaluate(root,fds);
}
static int execute_source(const char *text){int fds[3]={0,1,2};return execute_with_fds(text,fds);}
typedef struct {char source[32768];shell_token tokens[TOKENS];shell_node nodes[NODES];shell_heredoc heredocs[16];
    int token_count,node_count,position,parse_error,nesting,incomplete,collecting,heredoc_count;const char *syntax_error;} shell_frame;
static void frame_copy(void *out,const void *in,u32 n){for(u32 i=0;i<n;i++)((u8 *)out)[i]=((const u8 *)in)[i];}
static int nested_execute(const char *text,int *fds)
{
    if(nested_depth==16)return 2;shell_frame *frame=sc_alloc(sizeof(shell_frame));if(!frame)return 1;nested_depth++;
    frame_copy(frame->source,source,sizeof(source));frame_copy(frame->tokens,tokens,sizeof(tokens));frame_copy(frame->nodes,nodes,sizeof(nodes));frame_copy(frame->heredocs,heredocs,sizeof(heredocs));
    frame->token_count=token_count;frame->node_count=node_count;frame->position=position;frame->parse_error=parse_error;frame->nesting=nesting;frame->incomplete=incomplete;frame->collecting=collecting;frame->heredoc_count=heredoc_count;frame->syntax_error=syntax_error;
    collecting=0;int result=execute_with_fds(text,fds);
    frame_copy(source,frame->source,sizeof(source));frame_copy(tokens,frame->tokens,sizeof(tokens));frame_copy(nodes,frame->nodes,sizeof(nodes));frame_copy(heredocs,frame->heredocs,sizeof(heredocs));
    token_count=frame->token_count;node_count=frame->node_count;position=frame->position;parse_error=frame->parse_error;nesting=frame->nesting;incomplete=frame->incomplete;collecting=frame->collecting;heredoc_count=frame->heredoc_count;syntax_error=frame->syntax_error;
    sc_free(frame);nested_depth--;return result;
}
static void prompt(void)
{
    char user[32],cwd[64];u32 identity[8];sc_user(0,"",user,32);sc_user(2,"",cwd,64);sc_auth_info(identity);
    cli_text(1,user);cli_text(1," ");cli_text(1,cwd);cli_text(1,(int)identity[1]==-1?" [SYSTEM] # ":identity[1]==0?" # ":" $ ");
}
int main(void)
{
    if(cli_parse()<0)return 2;
    u32 self[8];if(sc_process_self(self)<0)return 1;shell_pid=self[1];
    if(cli_argc && equal(cli_argv[0],"--buffer3")){
        if(cli_argc>1)script_name=cli_argv[1];positional_count=cli_argc>2?cli_argc-2:0;for(int i=0;i<positional_count;i++)positional[i]=cli_argv[i+2];
        u32 bytes;char *script=cli_slurp(3,32767,&bytes);sc_stream_close(3,0);if(!script)return 1;for(u32 i=0;i<bytes;i++)if(!script[i]){sc_free(script);return 2;}
        int result=execute_source(script);sc_free(script);return leaving?exit_status:result;
    }
    if(cli_argc && equal(cli_argv[0],"-c")){
        if(cli_argc<2)return 2;if(cli_argc>2)script_name=cli_argv[2];
        positional_count=cli_argc>3?cli_argc-3:0;for(int i=0;i<positional_count;i++)positional[i]=cli_argv[i+3];
        int result=execute_source(cli_argv[1]);return leaving?exit_status:result;
    }
    if(cli_argc && !equal(cli_argv[0],"--serial")){
        script_name=cli_argv[0];positional_count=cli_argc-1;for(int i=0;i<positional_count;i++)positional[i]=cli_argv[i+1];
        int fd=sc_stream_open(cli_argv[0],1,0);if(fd<0)return cli_error("sh",fd);
        u32 n;char *text=cli_slurp(fd,32767,&n);sc_stream_close(fd,0);if(!text)return cli_error("sh: script size/read",-1);
        for(u32 i=0;i<n;i++)if(!text[i]){sc_free(text);return cli_error("sh: script NUL",-1);}
        int result=execute_source(text);sc_free(text);return leaving?exit_status:result;
    }
    u8 probe;int input=sc_stream_read(0,&probe,0);
    if(input<0){int window=sc_open_rgb("SandShell",720,430);if(window<0)return 1;
        if(sc_terminal(window,0)<0 || sc_tty(window)<0)return 1;}
    cli_text(1,"SandShell M9\n");char line[1024];int used=0,source_used=0;collecting=1;prompt();
    while(!leaving){
        int c=take_input();if(c==-1){sc_yield();continue;}if(c<0)break;
        if(c==3 || c==27){used=source_used=0;cli_text(1,"^C\n");prompt();continue;}
        if(c==4 && !used)break;
        if(c==8 || c==127){if(used){used--;cli_text(1,"\b \b");}continue;}
        if(c=='\r')continue;
        if(c=='\n'){
            line[used]=0;cli_text(1,"\n");if(source_used+used+2>=(int)sizeof(interactive_source)){
                cli_text(2,"sh: script capacity\n");used=source_used=0;prompt();continue;}
            for(int i=0;i<used;i++)interactive_source[source_used++]=line[i];interactive_source[source_used++]='\n';interactive_source[source_used]=0;
            int result=execute_source(interactive_source);used=0;
            if(incomplete){cli_text(1,"> ");continue;}last_status=result;source_used=0;if(!leaving)prompt();continue;
        }
        if(c>=32 || c=='\t'){if(used==1023){cli_text(2,"\nsh: line capacity\n");used=0;prompt();continue;}
            line[used++]=(char)c;u8 byte=(u8)c;cli_write(1,&byte,1);}
    }
    return leaving?exit_status:last_status;
}
