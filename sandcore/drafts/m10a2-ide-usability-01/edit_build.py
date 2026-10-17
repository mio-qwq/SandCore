from pathlib import Path
p=Path(__file__).resolve().parents[2]/'user/studio2/studio_build.c'
s=p.read_text(encoding='utf-8')
def swap(a,b):
    global s
    assert a in s,a[:100]
    s=s.replace(a,b,1)
swap('    c->pending[stream][n]=0;', '''    c->pending[stream][n]=0;
    if(build && !st.build.first_error[0] && (!strncmp(c->pending[stream],"SCCC ERROR ",11)||!strncmp(c->pending[stream],"SCLD ERROR ",11))){
        U keep=n;if(keep>=sizeof(st.build.first_error))keep=sizeof(st.build.first_error)-1u;
        while(keep&&((unsigned char)c->pending[stream][keep]&192u)==128u)keep--;
        memcpy(st.build.first_error,c->pending[stream],keep);st.build.first_error[keep]=0;
    }''')
swap('    ScStatus s=ScPathQuery(st.manifest_path,&q);', '    if(st.simple&&!b->automatic_manifest[0])return 0;\n    ScStatus s=ScPathQuery(st.simple?b->automatic_manifest:st.manifest_path,&q);')
swap('''static void build_fail(ScStatus s){
    if(!st.build.error)st.build.error=s;
    st.build.cancel=1;
    st.success=0;
    status("构建未完成，旧产物不取得运行资格");
}''','''static void build_fail(ScStatus s){
    StBuild *b=&st.build;
    if(!b->error){
        b->error=s;
        const char *stage=b->phase==S_BUILD_SAVE?"保存源码/创建构建目录":b->phase==S_BUILD_PIN?"读取源码或SDK头文件":b->phase==S_BUILD_COMPILE?"编译":b->phase==S_BUILD_LINK?"链接":b->phase==S_BUILD_SETTLE?"检查源码和写入程序":b->phase==S_BUILD_COPY?"保存构建结果":"构建";
        const char *cause=s==SC32_E_ACCESS?"没有访问权限":s==SC32_E_NOT_FOUND?"找不到所需文件或符号":s==SC32_E_STALE?"源码或文件在构建期间发生变化":s==SC32_E_NO_SPACE?"磁盘空间不足":s==SC32_E_MEMORY?"内存不足":s==SC32_E_CANCELLED?"操作已取消":"请查看下方工具的原始报错";
        snprintf(b->failure,sizeof(b->failure),"%s失败：%s（%d）\\n%s",stage,cause,s,b->first_error);
        output_append(1,b->failure,(U)strlen(b->failure));output_append(1,"\\n",1);
    }
    b->cancel=1;st.success=0;st.after_build=0;
    output_reveal();status("构建失败；下方输出保留了具体错误");
}''')
swap('    if(!st.member_count)return SC32_E_INVALID;', '''    if(st.simple==1u&&st.active){ScStatus selected=project_single(st.active->path);if(selected<0)return selected;}
    if(!st.member_count){error(SC32_E_INVALID,"请先新建或打开一个 C 源文件，再选择构建");return 0;}
    ScPathInfo tool;rec(&tool,sizeof(tool));ScStatus available=ScPathQuery(st.compiler,&tool);
    if(available>=0)available=ScPathQuery(st.linker,&tool);
    if(available<0){char missing[256];snprintf(missing,sizeof(missing),"找不到编译或链接工具。\\n编译器：%s\\n链接器：%s",st.compiler,st.linker);error(available,missing);return 0;}''')
swap('    ScUiTabSelect(st.ui,st.out_tabs,st.output_page);', '''    ScUiTabSelect(st.ui,st.out_tabs,st.output_page);output_reveal();
    char beginning[256];snprintf(beginning,sizeof(beginning),"开始构建 %s\\n输出文件：%s\\n",st.simple?"当前 C 程序":"项目",st.output_path);
    output_append(1,beginning,(U)strlen(beginning));''')
swap('        if(!strcmp(system,"/SYS/INC/")){','        if(!strcmp(system,"/SYS/INC/")||st.simple){')
swap('''        ScPathInfo q;
        rec(&q,sizeof(q));
        ScStatus s=ScPathQuery(st.manifest_path,&q);
        if(s>=0)s=pin_open(&b->manifest,st.manifest_path,q.generation,0);
        if(s<0){
            build_fail(s);
            return;
        }
        b->manifest_revision=q.generation;''','''        if(!st.simple){
            ScPathInfo q;rec(&q,sizeof(q));ScStatus s=ScPathQuery(st.manifest_path,&q);
            if(s>=0)s=pin_open(&b->manifest,st.manifest_path,q.generation,0);
            if(s<0){snprintf(b->first_error,sizeof(b->first_error),"链接清单不存在或不可读取：%s；在项目设置中检查路径。",st.manifest_path);build_fail(s);return;}
            b->manifest_revision=q.generation;
        }''')
swap('''            b->phase=S_BUILD_LINK;
            b->linking=0;''','''            b->phase=S_BUILD_LINK;
            if(st.simple){ScStatus s=automatic_manifest();if(s<0){build_fail(s);return;}}
            b->linking=0;''')
swap('st.target_scl?"--scl":"--dynamic",st.manifest_path,b->response','st.target_scl?"--scl":"--dynamic",st.simple?b->automatic_manifest:st.manifest_path,b->response')
swap('            error(b->error,"构建结束：未取得当前产物资格");','''            st.after_build=0;output_reveal();
            error(b->error,b->failure[0]?b->failure:"构建已取消或清理失败，具体错误见下方输出");''')
swap('status("本次SCX/SCL、map与SDBG已发布")','status("构建成功。可运行、调试；生成文件已保存")')
p.write_bytes(s.encode('utf-8'))
print('BUILD_FLOW_EDITED')
