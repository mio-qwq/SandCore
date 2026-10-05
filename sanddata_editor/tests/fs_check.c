#include "../src/sanddata_editor.h"
#include <stdio.h>
int wmain(int argc,wchar_t **argv){SfsImage fs={0};if(argc!=5)return 2;
if(!sfs_load(&fs,argv[1]))goto bad;
if(!sfs_add_path(&fs,argv[2],"TMP/EDITORTEST"))goto bad;
if(!sfs_rename(&fs,"TMP/EDITORTEST/keep.bin","TMP/EDITORTEST/renamed.bin"))goto bad;
if(!sfs_remove(&fs,"TMP/EDITORTEST/gone.txt"))goto bad;
if(!sfs_save(&fs,argv[3]))goto bad;
if(!sfs_extract(&fs,"TMP/EDITORTEST",argv[4]))goto bad;
sfs_free(&fs);if(!sfs_load(&fs,argv[3]))goto bad;
printf("PASS count=%u version=%u\n",fs.count,fs.version);sfs_free(&fs);return 0;
bad:fwprintf(stderr,L"%ls\n",sfs_error);sfs_free(&fs);return 1;}
