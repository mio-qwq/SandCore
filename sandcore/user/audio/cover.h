#ifndef SAND_AUDIO_COVER_H
#define SAND_AUDIO_COVER_H
#include "../IMAGECLIENT.H"
typedef struct {
    ScImageClient image;
    char title[128],artist[128],temporary[64];
    char source[64],sidecar[64];
    u32 source_generation,sidecar_generation;
    u32 began;
} sc_album_art;
void sc_album_close(sc_album_art *art);
int sc_album_begin(sc_album_art *art,const char *path);
int sc_album_step(sc_album_art *art);
int sc_album_current(sc_album_art *art,const char *path);
#endif
