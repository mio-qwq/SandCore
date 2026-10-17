/* 正常入口只询问位置、名称和项目类型；构建工具、依赖与中间目录自动管理。 */
static ScStatus new_project_dialog(void)
{return file_dialog(S_DIALOG_PROJECT_LOCATION);}
static ScStatus new_project_details(void)
{
    ScConfig *values=0;ScStatus s=ScConfigCreate(&values);if(s<0)return s;
    st.config_error=0;
    config_set(values,"project","name","HelloWorld");
    config_set(values,"project","location",st.project_location[0]?st.project_location:"/HOME");
    config_set(values,"project","template","C 控制台程序");
    config_set(values,"project","template.choices","C 控制台程序;空 C 项目");
    ScUiPropertySheetDesc d;rec(&d,sizeof(d));d.owner=st.root;d.title=text("新建项目：名称和类型");d.initial=values;d.completion=dialog_done;
    ScUiFieldDesc fields[3];memset(fields,0,sizeof(fields));
    const char *keys[]={"name","location","template"},*labels[]={"项目名称","保存位置","项目类型"};
    for(U i=0;i<3;i++){rec(fields+i,sizeof(fields[i]));fields[i].field=i+1;fields[i].kind=i==2?SC_UI_COMBOBOX:SC_UI_TEXTBOX;
        fields[i].section="project";fields[i].key=keys[i];fields[i].label=text(labels[i]);fields[i].flags=SC_UI_FIELD_REQUIRED|(i==1?SC_UI_FIELD_READONLY:0);}
    s=st.config_error<0?st.config_error:ScUiPropertySheet(st.ui,&d,fields,3,&st.dialog);
    if(s>=0)st.dialog_kind=S_DIALOG_PROJECT_CREATE;
    ScConfigRelease(values);return s;
}
static ScStatus new_project_create(ScConfig *values)
{
    char name[32],location[68],kind[64];
    ScStatus s=config_string(values,"name",name,sizeof(name),1);
    if(s>=0)s=config_path(values,"location",location,0);
    if(s>=0)s=config_string(values,"template",kind,sizeof(kind),1);
    if(s<0)return s;
    if(strcmp(kind,"C 控制台程序")&&strcmp(kind,"空 C 项目"))return SC32_E_INVALID;
    U n=(U)strlen(name);if(!n||n>20u)return SC32_E_RANGE;
    for(U i=0;i<n;i++)if(!((name[i]>='A'&&name[i]<='Z')||(name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_'))return SC32_E_INVALID;
    char folder[68],build[68],source[68],project[68],output[68];
    s=join(folder,location,name);
    if(s>=0)s=join(build,folder,"BUILD");
    if(s>=0)s=join(source,folder,"MAIN.C");
    if(s>=0){int a=snprintf(project,sizeof(project),"%s/%s.SCW",folder,name),b=snprintf(output,sizeof(output),"%s/%s.SCX",build,name);if(a<0||a>64||b<0||b>59)s=SC32_E_RANGE;}
    if(s<0)return s;
    /* 目录必须新建成功，不覆盖或接管用户已有项目。后续错误保留已写源码。 */
    s=ScPathCreate(folder,SC32_PATH_DIRECTORY,23);if(s>=0)s=ScPathCreate(build,SC32_PATH_DIRECTORY,23);if(s<0)return s;
    s=project_defaults();if(s<0)return s;
    st.simple=2;strcpy(st.workspace,build);strcpy(st.output_path,output);strcpy(st.project_path,project);strcpy(st.publish_dir,folder);
    config_set(st.config,"project","workspace",build);config_set(st.config,"project","output",output);config_set(st.config,"project","publish_dir",folder);
    if(st.config_error<0)return st.config_error;
    if(!strcmp(kind,"C 控制台程序")){
        const char *initial="#include <stdio.h>\n\nint main(void)\n{\n    printf(\"Hello, world!\\n\");\n    return 0;\n}\n";
        StDoc *created=0;s=doc_new(source,initial,(U)strlen(initial),0,&created);
        if(s>=0)s=project_add(source);
        if(s>=0)s=io_save(created,source);
        if(s<0)return s;
    }
    else{st.active=0;ScUiTabSelect(st.ui,st.tabs,st.welcome);}
    st.config_save_requested=1;project_notify();
    status("项目已创建；构建 F7，运行 Ctrl+F5，调试 F5");return 0;
}
