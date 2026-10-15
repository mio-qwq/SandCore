static ScU32 sh_crc(const void *data,ScU32 bytes) {
    const unsigned char *p=(const unsigned char *)data;
    ScU32 crc=0xFFFFFFFFu,i,j;
    /* 这是本库磁盘/消息格式的校验，不将CRC当认证或持久提交凭据。 */ for(i=0;i<bytes;++i) {
        crc^=p[i];
        for(j=0;j<8;++j)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));
    }
    return ~crc;
}
static ScStatus sh_application_encode(const ScShellApplication *a,ScBuffer **out) {
    ScShellWireApplication h;
    ScShellWireVerb *verbs=0;
    ScBuffer *b=0;
    ScStatus s;
    ScU32 i,n;
    const unsigned char *data;
    if(!sh_valid(a,SH_APP))return SC_SHELL_E_STALE;
    if(a->desc.verb_count>(0xFFFFFFFFu-sizeof(h))/sizeof(*verbs))return SC_SHELL_E_RANGE;
    n=sizeof(h)+a->desc.verb_count*sizeof(*verbs);
    s=ScBufferCreate(n,&b);
    if(s>=0)s=ScBufferResize(b,n);
    if(s<0)goto fail;
    memset(&h,0,sizeof(h));
    h.size=sizeof(h);
    h.version=1;
    h.token=a->info.token;
    h.revision=a->info.revision;
    h.layer=a->info.layer;
    h.flags=a->info.flags;
    h.owner_pid=a->info.owner_pid;
    h.owner_generation=a->info.owner_generation;
    h.uid=a->info.uid;
    h.gid=a->info.gid;
    h.profile=a->desc.profile;
    h.verb_count=a->desc.verb_count;
    h.verbs_offset=sizeof(h);
    verbs=(ScShellWireVerb *)calloc(h.verb_count,sizeof(*verbs));
    if(!verbs) {
        s=SC_SHELL_E_MEMORY;
        goto fail;
    }
    s=sh_append_string(b,a->desc.id,&h.id_offset,&h.id_bytes);
    if(s<0)goto fail;
    s=sh_append_string(b,a->desc.label,&h.label_offset,&h.label_bytes);
    if(s<0)goto fail;
    s=sh_append_string(b,a->desc.executable,&h.executable_offset,&h.executable_bytes);
    if(s<0)goto fail;
    s=sh_append_string(b,a->desc.arguments,&h.arguments_offset,&h.arguments_bytes);
    if(s<0)goto fail;
    s=sh_append_string(b,a->desc.icon,&h.icon_offset,&h.icon_bytes);
    if(s<0)goto fail;
    s=sh_append_string(b,a->desc.category,&h.category_offset,&h.category_bytes);
    if(s<0)goto fail;
    for(i=0;i<h.verb_count;++i) {
        ScShellWireVerb *w=verbs+i;
        const ScShellVerbDesc *v=a->verbs+i;
        w->size=sizeof(*w);
        w->version=1;
        w->flags=v->flags;
        w->profile=v->profile;
        s=sh_append_string(b,v->verb,&w->verb_offset,&w->verb_bytes);
        if(s<0)goto fail;
        s=sh_append_string(b,v->label,&w->label_offset,&w->label_bytes);
        if(s<0)goto fail;
        s=sh_append_string(b,v->executable,&w->executable_offset,&w->executable_bytes);
        if(s<0)goto fail;
        s=sh_append_string(b,v->arguments,&w->arguments_offset,&w->arguments_bytes);
        if(s<0)goto fail;
        s=sh_append_string(b,v->types,&w->types_offset,&w->types_bytes);
        if(s<0)goto fail;
        s=sh_append_string(b,v->icon,&w->icon_offset,&w->icon_bytes);
        if(s<0)goto fail;
    }
    s=sh_buffer_data(b,&data,&n);
    if(s<0)goto fail;
    h.record_bytes=n;
    s=ScBufferWrite(b,0,&h,sizeof(h));
    if(s>=0)s=ScBufferWrite(b,h.verbs_offset,verbs,h.verb_count*sizeof(*verbs));
    if(s<0)goto fail;
    free(verbs);
    *out=b;
    return 0;
    fail:free(verbs);
    ScBufferRelease(b);
    return sh_cause(s,20,0);
}
static ScStatus sh_decode_string(const unsigned char *p,ScU32 bytes,ScU32 *cursor,ScU32 offset,ScU32 count,ScU32 max,const char **out) {
    ScStatus s;
    if(offset!=*cursor)return SC_SHELL_E_PROTOCOL;
    s=sh_string_at(p,bytes,*cursor,offset,count,max,out);
    if(s<0)return s;
    *cursor+=(count+3)&~3u;
    return 0;
}
static ScStatus sh_application_decode(const void *bytes,ScU32 count,ScShellApplication **out) {
    ScShellWireApplication h;
    ScShellApplicationDesc d;
    ScShellVerbDesc *verbs=0;
    ScShellApplication *a=0;
    const unsigned char *p=(const unsigned char *)bytes;
    ScStatus s;
    ScU32 i,cursor;
    if(!p||count<sizeof(h))return SC_SHELL_E_PROTOCOL;
    memcpy(&h,p,sizeof(h));
    if(h.size!=sizeof(h)||h.version!=1||h.record_bytes!=count||h.reserved[0]||h.reserved[1]||h.verbs_offset!=sizeof(h)|| h.verb_count>(count-sizeof(h))/sizeof(ScShellWireVerb)||!h.verb_count)return SC_SHELL_E_PROTOCOL;
    cursor=sizeof(h)+h.verb_count*sizeof(ScShellWireVerb);
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.layer=h.layer;
    d.flags=h.flags;
    d.profile=h.profile;
    d.verb_count=h.verb_count;
    s=sh_decode_string(p,count,&cursor,h.id_offset,h.id_bytes,255,&d.id);
    if(s<0)goto fail;
    s=sh_decode_string(p,count,&cursor,h.label_offset,h.label_bytes,1023,&d.label);
    if(s<0)goto fail;
    s=sh_decode_string(p,count,&cursor,h.executable_offset,h.executable_bytes,65,&d.executable);
    if(s<0)goto fail;
    s=sh_decode_string(p,count,&cursor,h.arguments_offset,h.arguments_bytes,1023,&d.arguments);
    if(s<0)goto fail;
    s=sh_decode_string(p,count,&cursor,h.icon_offset,h.icon_bytes,127,&d.icon);
    if(s<0)goto fail;
    s=sh_decode_string(p,count,&cursor,h.category_offset,h.category_bytes,255,&d.category);
    if(s<0)goto fail;
    verbs=(ScShellVerbDesc *)calloc(h.verb_count,sizeof(*verbs));
    if(!verbs) {
        s=SC_SHELL_E_MEMORY;
        goto fail;
    }
    d.verbs=verbs;
    for(i=0;i<h.verb_count;++i) {
        ScShellWireVerb w;
        ScShellVerbDesc *v=verbs+i;
        memcpy(&w,p+sizeof(h)+i*sizeof(w),sizeof(w));
        if(w.size!=sizeof(w)||w.version!=1) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        v->size=sizeof(*v);
        v->version=1;
        v->flags=w.flags;
        v->profile=w.profile;
        s=sh_decode_string(p,count,&cursor,w.verb_offset,w.verb_bytes,255,&v->verb);
        if(s<0)goto fail;
        s=sh_decode_string(p,count,&cursor,w.label_offset,w.label_bytes,1023,&v->label);
        if(s<0)goto fail;
        s=sh_decode_string(p,count,&cursor,w.executable_offset,w.executable_bytes,65,&v->executable);
        if(s<0)goto fail;
        s=sh_decode_string(p,count,&cursor,w.arguments_offset,w.arguments_bytes,1023,&v->arguments);
        if(s<0)goto fail;
        s=sh_decode_string(p,count,&cursor,w.types_offset,w.types_bytes,4095,&v->types);
        if(s<0)goto fail;
        s=sh_decode_string(p,count,&cursor,w.icon_offset,w.icon_bytes,127,&v->icon);
        if(s<0)goto fail;
    }
    if(cursor!=count||(!!h.owner_pid!=!!h.owner_generation)) {
        s=SC_SHELL_E_PROTOCOL;
        goto fail;
    }
    s=sh_application_new(&d,&a);
    if(s<0)goto fail;
    a->info.token=h.token;
    a->info.revision=h.revision;
    a->info.owner_pid=h.owner_pid;
    a->info.owner_generation=h.owner_generation;
    a->info.uid=h.uid;
    a->info.gid=h.gid;
    free(verbs);
    *out=a;
    return 0;
    fail:free(verbs);
    ScShellApplicationRelease(a);
    return s;
}
static ScStatus sh_association_encode(const ShAssociation *a,ScBuffer **out) {
    ScShellWireAssociation h;
    ScBuffer *b=0;
    ScStatus s;
    const unsigned char *p;
    ScU32 n;
    memset(&h,0,sizeof(h));
    h.size=sizeof(h);
    h.version=1;
    h.layer=a->layer;
    h.flags=a->flags;
    h.revision=a->revision;
    s=ScBufferCreate(sizeof(h),&b);
    if(s>=0)s=ScBufferResize(b,sizeof(h));
    if(s<0)goto fail;
    s=sh_append_string(b,a->type,&h.type_offset,&h.type_bytes);
    if(s<0)goto fail;
    s=sh_append_string(b,a->application,&h.application_offset,&h.application_bytes);
    if(s<0)goto fail;
    s=sh_append_string(b,a->verb,&h.verb_offset,&h.verb_bytes);
    if(s<0)goto fail;
    s=sh_buffer_data(b,&p,&n);
    if(s<0)goto fail;
    h.record_bytes=n;
    s=ScBufferWrite(b,0,&h,sizeof(h));
    if(s<0)goto fail;
    *out=b;
    return 0;
    fail:ScBufferRelease(b);
    return sh_cause(s,20,0);
}
static ScStatus sh_association_decode(const void *bytes,ScU32 count,ShAssociation **out) {
    const unsigned char *p=(const unsigned char *)bytes;
    ScShellWireAssociation h;
    ShAssociation *a=0;
    const char *type,*app,*verb;
    ScU32 cursor=sizeof(h);
    ScStatus s;
    if(!p||count<sizeof(h))return SC_SHELL_E_PROTOCOL;
    memcpy(&h,p,sizeof(h));
    if(h.size!=sizeof(h)||h.version!=1||h.record_bytes!=count||h.layer>2||h.flags)return SC_SHELL_E_PROTOCOL;
    s=sh_decode_string(p,count,&cursor,h.type_offset,h.type_bytes,255,&type);
    if(s<0)return s;
    s=sh_decode_string(p,count,&cursor,h.application_offset,h.application_bytes,255,&app);
    if(s<0)return s;
    s=sh_decode_string(p,count,&cursor,h.verb_offset,h.verb_bytes,255,&verb);
    if(s<0)return s;
    if(cursor!=count||!sh_name(app)||(*verb&&!sh_name(verb)))return SC_SHELL_E_PROTOCOL;
    a=(ShAssociation *)calloc(1,sizeof(*a));
    if(!a)return SC_SHELL_E_MEMORY;
    a->references=1;
    a->revision=h.revision;
    a->layer=h.layer;
    a->flags=h.flags;
    s=sh_type(type,&a->type);
    if(s<0)goto fail;
    s=sh_copy_text(app,255,0,&a->application);
    if(s<0)goto fail;
    s=sh_copy_text(verb,255,1,&a->verb);
    if(s<0)goto fail;
    *out=a;
    return 0;
    fail:sh_association_release(a);
    return s;
}
static ScStatus sh_layout_encode(const ScShellLayout *l,ScBuffer **out) {
    ScShellWireLayoutSet set;
    ScBuffer *b=0;
    ScU32 i;
    ScStatus s;
    memset(&set,0,sizeof(set));
    set.size=sizeof(set);
    set.version=1;
    set.count=l->info.count;
    set.mode=l->info.mode;
    set.grid_width=l->info.grid_width;
    set.grid_height=l->info.grid_height;
    set.revision=l->info.revision;
    s=ScBufferCreate(sizeof(set),&b);
    if(s>=0)s=ScBufferAppend(b,&set,sizeof(set));
    if(s<0)goto fail;
    for(i=0;i<l->info.count;++i) {
        ScShellWireLayoutItem w;
        ScBuffer *row=0;
        const unsigned char *p;
        ScU32 n;
        memset(&w,0,sizeof(w));
        w.size=sizeof(w);
        w.version=1;
        w.item=l->items[i].item;
        w.flags=l->items[i].flags;
        w.x=l->items[i].x;
        w.y=l->items[i].y;
        w.width=l->items[i].width;
        w.height=l->items[i].height;
        w.order=l->items[i].order;
        s=ScBufferCreate(sizeof(w),&row);
        if(s>=0)s=ScBufferResize(row,sizeof(w));
        if(s>=0)s=sh_append_string(row,l->items[i].identity,&w.identity_offset,&w.identity_bytes);
        if(s>=0)s=sh_buffer_data(row,&p,&n);
        if(s>=0) {
            w.record_bytes=n;
            s=ScBufferWrite(row,0,&w,sizeof(w));
        }
        if(s>=0)s=sh_buffer_data(row,&p,&n);
        if(s>=0)s=ScBufferAppend(b,p,n);
        ScBufferRelease(row);
        if(s<0)goto fail;
    }
    *out=b;
    return 0;
    fail:ScBufferRelease(b);
    return sh_cause(s,20,0);
}
static ScStatus sh_layout_decode(const void *bytes,ScU32 count,ScShellLayout **out) {
    const unsigned char *p=(const unsigned char *)bytes;
    ScShellWireLayoutSet h;
    ScShellLayoutItem *items=0;
    ScShellLayout *l=0;
    ScU32 i,pos;
    ScStatus s;
    if(!p||count<sizeof(h))return SC_SHELL_E_PROTOCOL;
    memcpy(&h,p,sizeof(h));
    if(h.size!=sizeof(h)||h.version!=1||h.reserved||h.count>(count-sizeof(h))/sizeof(ScShellWireLayoutItem)||h.count>0xFFFFFFFFu/sizeof(*items))return SC_SHELL_E_PROTOCOL;
    if(h.count) {
        items=(ScShellLayoutItem *)calloc(h.count,sizeof(*items));
        if(!items)return SC_SHELL_E_MEMORY;
    }
    pos=sizeof(h);
    for(i=0;i<h.count;++i) {
        ScShellWireLayoutItem w;
        ScU32 cursor=sizeof(w);
        const char *id;
        if(pos>count||count-pos<sizeof(w)) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        memcpy(&w,p+pos,sizeof(w));
        if(w.size!=sizeof(w)||w.version!=1||w.record_bytes<sizeof(w)||w.record_bytes>count-pos) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        s=sh_decode_string(p+pos,w.record_bytes,&cursor,w.identity_offset,w.identity_bytes,1023,&id);
        if(s<0)goto fail;
        if(cursor!=w.record_bytes) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        items[i].size=sizeof(*items);
        items[i].version=1;
        items[i].item=w.item;
        items[i].flags=w.flags;
        items[i].identity=id;
        items[i].x=w.x;
        items[i].y=w.y;
        items[i].width=w.width;
        items[i].height=w.height;
        items[i].order=w.order;
        pos+=w.record_bytes;
    }
    if(pos!=count) {
        s=SC_SHELL_E_PROTOCOL;
        goto fail;
    }
    s=ScShellLayoutCreate(items,h.count,h.mode,h.grid_width,h.grid_height,&l);
    if(s<0)goto fail;
    if(!h.revision) {
        s=SC_SHELL_E_PROTOCOL;
        goto fail;
    }
    l->info.revision=h.revision;
    free(items);
    *out=l;
    return 0;
    fail:free(items);
    ScShellLayoutRelease(l);
    return s;
}
ScStatus ScShellCatalogExport(const ScShellCatalog *c,ScBuffer **out) {
    ScShellWireCatalog h;
    ScBuffer *b=0,*part=0;
    ScU32 i,n;
    const unsigned char *p;
    ScStatus s;
    if(!out)return SC_SHELL_E_INVALID;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    memset(&h,0,sizeof(h));
    h.magic=SHW_CATALOG_MAGIC;
    h.version=1;
    h.header_bytes=sizeof(h);
    h.epoch=c->epoch;
    h.next_token=c->next_token;
    h.applications=c->app_count;
    h.associations=c->association_count;
    h.layout_items=c->layout->info.count;
    h.layout_mode=c->layout->info.mode;
    h.grid_width=c->layout->info.grid_width;
    h.grid_height=c->layout->info.grid_height;
    h.layout_revision=c->layout->info.revision;
    s=ScBufferCreate(sizeof(h),&b);
    if(s>=0)s=ScBufferResize(b,sizeof(h));
    if(s<0)goto fail;
    for(i=0;i<c->app_count;++i) {
        s=sh_application_encode(c->apps[i],&part);
        if(s>=0)s=sh_buffer_data(part,&p,&n);
        if(s>=0)s=ScBufferAppend(b,p,n);
        ScBufferRelease(part);
        part=0;
        if(s<0)goto fail;
    }
    for(i=0;i<c->association_count;++i) {
        s=sh_association_encode(c->associations[i],&part);
        if(s>=0)s=sh_buffer_data(part,&p,&n);
        if(s>=0)s=ScBufferAppend(b,p,n);
        ScBufferRelease(part);
        part=0;
        if(s<0)goto fail;
    }
    s=sh_layout_encode(c->layout,&part);
    if(s>=0)s=sh_buffer_data(part,&p,&n);
    if(s>=0)s=ScBufferAppend(b,p+sizeof(ScShellWireLayoutSet),n-sizeof(ScShellWireLayoutSet));
    if(s<0)goto fail;
    s=sh_buffer_data(b,&p,&n);
    if(s<0)goto fail;
    h.body_bytes=n-sizeof(h);
    h.crc=sh_crc(p+sizeof(h),h.body_bytes);
    s=ScBufferWrite(b,0,&h,sizeof(h));
    if(s<0)goto fail;
    ScBufferRelease(part);
    *out=b;
    return 0;
    fail:ScBufferRelease(part);
    ScBufferRelease(b);
    return sh_cause(s,21,0);
}
static ScStatus sh_catalog_decode(const void *bytes,ScU32 count,ScShellCatalog **out) {
    const unsigned char *p=(const unsigned char *)bytes;
    ScShellWireCatalog h;
    ScShellCatalog *c=0;
    ScBuffer *lb=0;
    ScU32 pos,i,j;
    ScStatus s;
    if(!p||count<sizeof(h))return SC_SHELL_E_PROTOCOL;
    memcpy(&h,p,sizeof(h));
    if(h.magic!=SHW_CATALOG_MAGIC||h.version!=1||h.header_bytes!=sizeof(h)||h.body_bytes!=count-sizeof(h)||h.reserved[0]||h.reserved[1]|| !h.epoch||!h.next_token||h.crc!=sh_crc(p+sizeof(h),h.body_bytes)||h.applications>h.body_bytes/sizeof(ScShellWireApplication)|| h.associations>h.body_bytes/sizeof(ScShellWireAssociation)||h.applications>0xFFFFFFFFu/sizeof(ScShellApplication *)||h.associations>0xFFFFFFFFu/sizeof(ShAssociation *))return SC_SHELL_E_PROTOCOL;
    s=ScShellCatalogCreate(&c);
    if(s<0)return s;
    c->epoch=h.epoch;
    c->next_token=h.next_token;
    if(h.applications) {
        c->apps=(ScShellApplication **)calloc(h.applications,sizeof(*c->apps));
        if(!c->apps) {
            s=SC_SHELL_E_MEMORY;
            goto fail;
        }
    }
    if(h.associations) {
        c->associations=(ShAssociation **)calloc(h.associations,sizeof(*c->associations));
        if(!c->associations) {
            s=SC_SHELL_E_MEMORY;
            goto fail;
        }
    }
    pos=sizeof(h);
    for(i=0;i<h.applications;++i) {
        ScShellWireApplication w;
        ScShellApplication *a=0;
        if(count-pos<sizeof(w)) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        memcpy(&w,p+pos,sizeof(w));
        if(w.record_bytes>count-pos) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        s=sh_application_decode(p+pos,w.record_bytes,&a);
        if(s<0)goto fail;
        if(!a->info.token||!a->info.revision||a->info.token>=h.next_token||(i&&a->info.token<=c->apps[i-1]->info.token)) {
            ScShellApplicationRelease(a);
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        for(j=0;j<i;++j)if(c->apps[j]->info.layer==a->info.layer&&!strcmp(c->apps[j]->desc.id,a->desc.id)) {
            ScShellApplicationRelease(a);
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        a->info.epoch=c->epoch;
        c->apps[c->app_count++]=a;
        pos+=w.record_bytes;
    }
    for(i=0;i<h.associations;++i) {
        ScShellWireAssociation w;
        ShAssociation *a=0;
        if(count-pos<sizeof(w)) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        memcpy(&w,p+pos,sizeof(w));
        if(w.record_bytes>count-pos) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        s=sh_association_decode(p+pos,w.record_bytes,&a);
        if(s<0)goto fail;
        if(!a->revision) {
            sh_association_release(a);
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        for(j=0;j<i;++j)if(c->associations[j]->layer==a->layer&&!strcmp(c->associations[j]->type,a->type)&&!strcmp(c->associations[j]->verb,a->verb)) {
            sh_association_release(a);
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        c->associations[c->association_count++]=a;
        pos+=w.record_bytes;
    }
    {
        ScShellWireLayoutSet set;
        ScShellLayout *l=0;
        const unsigned char *bp;
        ScU32 bn;
        memset(&set,0,sizeof(set));
        set.size=sizeof(set);
        set.version=1;
        set.count=h.layout_items;
        set.mode=h.layout_mode;
        set.grid_width=h.grid_width;
        set.grid_height=h.grid_height;
        set.revision=h.layout_revision;
        s=ScBufferCreate(sizeof(set),&lb);
        if(s>=0)s=ScBufferAppend(lb,&set,sizeof(set));
        if(s>=0)s=ScBufferAppend(lb,p+pos,count-pos);
        if(s>=0)s=sh_buffer_data(lb,&bp,&bn);
        if(s>=0)s=sh_layout_decode(bp,bn,&l);
        if(s<0)goto fail;
        ScShellLayoutRelease(c->layout);
        c->layout=l;
    }
    ScBufferRelease(lb);
    *out=c;
    return 0;
    fail:ScBufferRelease(lb);
    ScShellCatalogRelease(c);
    return s;
}
static ScU32 sh_application_key(ScU32 layer,const char *id,unsigned char key[260]) {
    ScU32 n=(ScU32)strlen(id);
    memcpy(key,&layer,4);
    memcpy(key+4,id,n);
    return 4+n;
}
static ScU32 sh_association_key(ScU32 layer,const char *type,const char *verb,unsigned char key[516]) {
    ScU32 n=(ScU32)strlen(type)+1,m=(ScU32)strlen(verb);
    memcpy(key,&layer,4);
    memcpy(key+4,type,n);
    memcpy(key+4+n,verb,m);
    return 4+n+m;
}
static ScStatus sh_catalog_merge(ScShellCatalog *candidate,ScShellCatalog *loaded) {
    ScHash *app_ids=0,*associations=0,*effective=0;
    ScHashDesc hd;
    ScHashEntry e;
    unsigned char key[516];
    ScU32 i,index,n,capacity;
    ScStatus status;
    memset(&hd,0,sizeof(hd));
    hd.size=sizeof(hd);
    hd.version=1;
    hd.value_bytes=4;
    status=ScHashCreate(&hd,&app_ids);
    if(status>=0)status=ScHashCreate(&hd,&associations);
    if(status>=0)status=ScHashCreate(&hd,&effective);
    if(status<0)goto done;
    if(loaded->app_count>0xFFFFFFFFu-candidate->app_count) {
        status=SC_SHELL_E_RANGE;
        goto done;
    }
    capacity=candidate->app_count+loaded->app_count;
    if(capacity>0xFFFFFFFFu/sizeof(*candidate->apps)) {
        status=SC_SHELL_E_RANGE;
        goto done;
    }
    if(capacity) {
        ScShellApplication **array=(ScShellApplication **)realloc(candidate->apps,capacity*sizeof(*array));
        if(!array) {
            status=SC_SHELL_E_MEMORY;
            goto done;
        }
        candidate->apps=array;
    }
    for(i=0;i<candidate->app_count;++i) {
        n=sh_application_key(candidate->apps[i]->info.layer,candidate->apps[i]->desc.id,key);
        status=ScHashSet(app_ids,key,n,&i,SC_HASH_INSERT_ONLY);
        if(status<0)goto done;
    }
    for(i=0;i<loaded->app_count;++i) {
        ScShellApplication *app=loaded->apps[i];
        ScU32 token,revision;
        if(app->info.layer==SC_SHELL_LAYER_SESSION)continue;
        n=sh_application_key(app->info.layer,app->desc.id,key);
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        status=ScHashGet(app_ids,key,n,&e);
        if(status>=0) {
            memcpy(&index,e.value,4);
            token=candidate->apps[index]->info.token;
            if(candidate->apps[index]->info.revision==0xFFFFFFFFu) {
                status=SC_SHELL_E_RANGE;
                goto done;
            }
            revision=candidate->apps[index]->info.revision+1;
        } else if(status==SC_UTIL_E_NOT_FOUND) {
            if(candidate->next_token==0xFFFFFFFFu) {
                status=SC_SHELL_E_RANGE;
                goto done;
            }
            index=candidate->app_count;
            token=candidate->next_token;
            revision=1;
            status=ScHashSet(app_ids,key,n,&index,SC_HASH_INSERT_ONLY);
            if(status<0)goto done;
            ++candidate->next_token;
        } else goto done;
        app->info.token=token;
        app->info.revision=revision;
        app->info.owner_pid=app->info.owner_generation=0;
        app->info.epoch=candidate->epoch+1;
        loaded->apps[i]=0;
        if(index<candidate->app_count)ScShellApplicationRelease(candidate->apps[index]);
        else ++candidate->app_count;
        candidate->apps[index]=app;
    }
    for(i=0;i<candidate->app_count;++i) {
        ScShellApplication *app=candidate->apps[i];
        n=(ScU32)strlen(app->desc.id);
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        status=ScHashGet(effective,app->desc.id,n,&e);
        if(status==SC_UTIL_E_NOT_FOUND||(status>=0&&(*(ScShellApplication *const *)e.value)->info.layer<app->info.layer)) status=ScHashSet(effective,app->desc.id,n,&app,0);
        if(status<0)goto done;
    }
    if(loaded->association_count>0xFFFFFFFFu-candidate->association_count) {
        status=SC_SHELL_E_RANGE;
        goto done;
    }
    capacity=candidate->association_count+loaded->association_count;
    if(capacity>0xFFFFFFFFu/sizeof(*candidate->associations)) {
        status=SC_SHELL_E_RANGE;
        goto done;
    }
    if(capacity) {
        ShAssociation **array=(ShAssociation **)realloc(candidate->associations,capacity*sizeof(*array));
        if(!array) {
            status=SC_SHELL_E_MEMORY;
            goto done;
        }
        candidate->associations=array;
    }
    for(i=0;i<candidate->association_count;++i) {
        ShAssociation *a=candidate->associations[i];
        n=sh_association_key(a->layer,a->type,a->verb,key);
        status=ScHashSet(associations,key,n,&i,SC_HASH_INSERT_ONLY);
        if(status<0)goto done;
    }
    for(i=0;i<loaded->association_count;++i) {
        ShAssociation *association=loaded->associations[i];
        ScShellApplication *app;
        ScU32 verb,revision;
        if(association->layer==SC_SHELL_LAYER_SESSION)continue;
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        status=ScHashGet(effective,association->application,(ScU32)strlen(association->application),&e);
        if(status<0)goto done;
        app=*(ScShellApplication *const *)e.value;
        if(!sh_verb_index(app,association->verb,&verb)) {
            status=SC_SHELL_E_NOT_FOUND;
            goto done;
        }
        n=sh_association_key(association->layer,association->type,association->verb,key);
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        status=ScHashGet(associations,key,n,&e);
        if(status>=0) {
            memcpy(&index,e.value,4);
            if(candidate->associations[index]->revision==0xFFFFFFFFu) {
                status=SC_SHELL_E_RANGE;
                goto done;
            }
            revision=candidate->associations[index]->revision+1;
        } else if(status==SC_UTIL_E_NOT_FOUND) {
            index=candidate->association_count;
            revision=1;
            status=ScHashSet(associations,key,n,&index,SC_HASH_INSERT_ONLY);
            if(status<0)goto done;
        } else goto done;
        association->revision=revision;
        association->owner_pid=association->owner_generation=0;
        loaded->associations[i]=0;
        if(index<candidate->association_count)sh_association_release(candidate->associations[index]);
        else ++candidate->association_count;
        candidate->associations[index]=association;
    }
    status=0;
    done:ScHashRelease(app_ids);
    ScHashRelease(associations);
    ScHashRelease(effective);
    return sh_cause(status,22,0);
}
ScStatus ScShellCatalogImport(ScShellCatalog *c,const void *bytes,ScU32 count,ScU32 epoch,ScU32 flags) {
    ScShellCatalog *loaded=0,*candidate=0;
    ScStatus status;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    if(c->epoch!=epoch)return SC_SHELL_E_STALE;
    if(c->commit_lock)return SC_SHELL_E_BUSY;
    if(flags&~SC_SHELL_IMPORT_REPLACE)return SC_SHELL_E_INVALID;
    if(c->epoch==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    status=sh_catalog_decode(bytes,count,&loaded);
    if(status<0)return sh_cause(status,22,0);
    if(flags&SC_SHELL_IMPORT_REPLACE) {
        status=ScShellCatalogCreate(&candidate);
        if(status<0)goto done;
        candidate->next_token=c->next_token;
        candidate->epoch=c->epoch;
    } else {
        status=sh_catalog_clone(c,&candidate);
        if(status<0)goto done;
    }
    /* 索引一次构造，批量merge不反复线性Register，保持O(bytes+items)。 */ status=sh_catalog_merge(candidate,loaded);
    if(status<0)goto done;
    status=ScShellCatalogApplyLayout(candidate,loaded->layout,candidate->epoch);
    if(status<0)goto done;
    candidate->epoch=c->epoch+1;
    candidate->store_revision=c->store_revision;
    sh_catalog_swap(c,candidate);
    done:ScShellCatalogRelease(loaded);
    ScShellCatalogRelease(candidate);
    return sh_cause(status,22,0);
}
static ScStatus sh_read_file_revision(const char *path,ScBuffer **out,ScU32 *revision) {
    ScLeaseOpenDesc d;
    ScLeaseInfo lease;
    ScLeaseControl control;
    ScPathInfo metadata;
    ScFile file=0;
    ScFileInfo info;
    ScBuffer *b=0;
    ScU32 offset=0;
    ScStatus s;
    memset(&metadata,0,sizeof(metadata));
    metadata.size=sizeof(metadata);
    metadata.version=1;
    s=ScPathQuery(path,&metadata);
    if(s<0)return sh_cause(s,23,0);
    if(metadata.kind!=SC32_PATH_FILE)return SC_SHELL_E_INVALID;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.path=path;
    d.object_kind=SC32_PATH_FILE;
    d.lease_kind=SC32_LEASE_READ_STABLE;
    d.expected_revision=metadata.generation;
    d.open_flags=SC32_FILE_READ;
    memset(&lease,0,sizeof(lease));
    lease.size=sizeof(lease);
    lease.version=1;
    s=ScFileLeaseOpenPinned(&d,&lease);
    if(s<0)return sh_cause(s,23,0);
    file=lease.file;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScFileQuery(file,&info);
    if(s<0)goto done;
    s=ScBufferCreate(info.length,&b);
    if(s>=0)s=ScBufferResize(b,info.length);
    if(s<0)goto done;
    while(offset<info.length) {
        const unsigned char *p;
        ScU32 n,chunk=info.length-offset;
        ScIoResult io;
        if(chunk>65536)chunk=65536;
        s=sh_buffer_data(b,&p,&n);
        if(s<0)goto done;
        memset(&io,0,sizeof(io));
        io.size=sizeof(io);
        io.version=1;
        s=ScFileRead(file,(void *)(p+offset),chunk,&io);
        if(s<0)goto done;
        if(!io.transferred||io.transferred>chunk) {
            s=SC_SHELL_E_IO;
            goto done;
        }
        offset+=io.transferred;
    }
    done:memset(&control,0,sizeof(control));
    control.size=sizeof(control);
    control.version=1;
    control.lease=lease.lease;
    control.generation=lease.generation;
    {
        ScLeaseInfo released=lease;
        ScStatus close=ScFileLeaseRelease(&control,&released);
        if(s>=0&&close<0)s=close;
        close=ScFileClose(file);
        if(s>=0&&close<0)s=close;
    }
    if(s<0) {
        ScBufferRelease(b);
        return sh_cause(s,23,0);
    }
    if(revision)*revision=metadata.generation;
    *out=b;
    return 0;
}
static ScStatus sh_read_file(const char *path,ScBuffer **out) {
    return sh_read_file_revision(path,out,0);
}
static ScStatus sh_lease_finish(ScStatus s,ScLeaseInfo *info) {
    while(s==SC32_PENDING) {
        ScLeaseControl c;
        memset(&c,0,sizeof(c));
        c.size=sizeof(c);
        c.version=1;
        c.lease=info->lease;
        c.generation=info->generation;
        c.operation_sequence=info->operation_sequence;
        ScProcessYield();
        s=ScFileLeaseAdvance(&c,info);
    }
    return s;
}
static void sh_lease_drop(ScLeaseInfo *info,ScU32 cancel) {
    ScLeaseControl c;
    ScStatus s;
    if(!info->lease)return;
    memset(&c,0,sizeof(c));
    c.size=sizeof(c);
    c.version=1;
    c.lease=info->lease;
    c.generation=info->generation;
    c.operation_sequence=info->operation_sequence;
    if(cancel) {
        s=ScFileLeaseCancel(&c,info);
        s=sh_lease_finish(s,info);
        (void)s;
        c.operation_sequence=info->operation_sequence;
    }
    s=ScFileLeaseRelease(&c,info);
    s=sh_lease_finish(s,info);
    (void)s;
}
static ScStatus sh_temporary(const char *path,ScU32 attempt,char **out) {
    const char *slash=strrchr(path,'/');
    ScU32 parent=slash?(ScU32)(slash-path)+1:0,room,width,i,value;
    static const char digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char *p;
    if(parent>=64)return SC_SHELL_E_RANGE;
    room=64-parent;
    width=room<8?room:8;
    if(!width)return SC_SHELL_E_RANGE;
    p=(char *)malloc(parent+width+1);
    if(!p)return SC_SHELL_E_MEMORY;
    memcpy(p,path,parent);
    value=attempt;
    for(i=0;i<width;++i) {
        p[parent+width-1-i]=digits[value%36];
        value/=36;
    }
    p[parent+width]=0;
    if(value||!strcmp(p,path)) {
        free(p);
        return SC_SHELL_E_EXISTS;
    }
    *out=p;
    return 0;
}
static ScStatus sh_store_file(const char *path,const ScBuffer *buffer,ScU32 expected,ScU32 *revision) {
    ScShellStore *store=0;
    ScStatus s;
    ScShellErrorInfo error;
    if(!revision)return SC_SHELL_E_INVALID;
    s=sh_store_begin_buffer(0,(ScBuffer *)buffer,path,expected,0,&store);
    while(s>=0&&store) {
        s=ScShellStoreStep(store,0);
        if(s!=SC_SHELL_PENDING)break;
        ScProcessYield();
    }
    if(s>=0)*revision=store->info.new_revision;
    error=sh_error;
    ScShellStoreClose(store);
    if(s<0)sh_error=error;
    return s;
}
ScStatus ScShellCatalogLoad(ScShellCatalog *c,const char *path,ScU32 epoch) {
    ScBuffer *b=0;
    const unsigned char *p;
    ScU32 n,i,revision=0;
    ScShellCatalog *loaded=0;
    ScStatus s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    s=sh_read_file_revision(path,&b,&revision);
    if(s>=0)s=sh_buffer_data(b,&p,&n);
    if(s>=0)s=sh_catalog_decode(p,n,&loaded);
    if(s>=0) {
        for(i=0;i<loaded->app_count;++i)if(loaded->apps[i]->info.layer!=SC_SHELL_LAYER_ACCOUNT||loaded->apps[i]->info.owner_pid||loaded->apps[i]->info.owner_generation) {
            s=SC_SHELL_E_ACCESS;
            break;
        }
    }
    if(s>=0) {
        for(i=0;i<loaded->association_count;++i)if(loaded->associations[i]->layer!=SC_SHELL_LAYER_ACCOUNT) {
            s=SC_SHELL_E_ACCESS;
            break;
        }
    }
    if(s>=0)s=ScShellCatalogImport(c,p,n,epoch,0);
    if(s>=0)c->store_revision=revision;
    ScShellCatalogRelease(loaded);
    ScBufferRelease(b);
    return s;
}
static ScStatus sh_catalog_account_buffer(const ScShellCatalog *c,ScBuffer **out) {
    ScShellCatalog *account=0;
    ScStatus s;
    ScU32 i;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    s=sh_catalog_clone(c,&account);
    if(s<0)return s;
    /* 持久文件只保存账户覆盖；SID PID/进程引用和系统发行种子不落入用户配置。 */ for(i=account->app_count;i;--i)if(account->apps[i-1]->info.layer!=SC_SHELL_LAYER_ACCOUNT) {
        ScShellApplicationRelease(account->apps[i-1]);
        memmove(account->apps+i-1,account->apps+i,(account->app_count-i)*sizeof(*account->apps));
        --account->app_count;
    }
    for(i=account->association_count;i;--i)if(account->associations[i-1]->layer!=SC_SHELL_LAYER_ACCOUNT) {
        sh_association_release(account->associations[i-1]);
        memmove(account->associations+i-1,account->associations+i,(account->association_count-i)*sizeof(*account->associations));
        --account->association_count;
    }
    s=ScShellCatalogExport(account,out);
    ScShellCatalogRelease(account);
    return s;
}
ScStatus ScShellCatalogSave(ScShellCatalog *c,const char *path,ScU32 expected,ScU32 *revision) {
    ScShellStore *store=0;
    ScStatus s;
    ScShellErrorInfo error;
    if(!revision)return SC_SHELL_E_INVALID;
    s=ScShellStoreBegin(c,path,expected,&store);
    while(s>=0&&store) {
        s=ScShellStoreStep(store,0);
        if(s!=SC_SHELL_PENDING)break;
        ScProcessYield();
    }
    if(s>=0)*revision=store->info.new_revision;
    error=sh_error;
    ScShellStoreClose(store);
    if(s<0)sh_error=error;
    return s;
}
