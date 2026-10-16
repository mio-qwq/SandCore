#ifndef SANDCORE_SOUND_COVER_IMPL_H
#define SANDCORE_SOUND_COVER_IMPL_H
#include "SOUNDCOVER.H"
#include <SC32PATH.H>
#include <SC32LEASE.H>
#include <SCLIB.H>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
enum {SCOV_COVER_LIMIT=1048576u,SCOV_FLAC_LIMIT=16777216u,SCOV_READ_BATCH=65536u};
typedef struct {ScLeaseInfo lease;ScU32 length,revision;} ScovSource;
typedef struct {ScU32 offset,bytes,type;} ScovPicture;
static ScStatus scov_source_open(const char *,ScU32,ScovSource *);
static void scov_source_close(ScovSource *);
static ScStatus scov_read(ScovSource *,ScU32,void *,ScU32,ScU32);
static ScStatus scov_current(ScovSource *,const char *);
static void scov_text(char [128],const unsigned char *,ScU32,ScU32);
static ScStatus scov_extract(SndCover *,ScovSource *,ScU32,ScU32,ScU32,ScU32);
static ScStatus scov_flac(SndCover *,ScovSource *,int);
static ScStatus scov_mp3(SndCover *,ScovSource *,const unsigned char *,int);
#endif
