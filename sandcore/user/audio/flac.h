#ifndef SAND_AUDIO_FLAC_H
#define SAND_AUDIO_FLAC_H
#include "../SCAUDIO.H"
int sc_flac_open(sc_audio_file *file);
int sc_flac_read(sc_audio_file *file,short *stereo,u32 frames);
int sc_flac_seek(sc_audio_file *file,u32 frame);
void sc_flac_close(sc_audio_file *file);
void sc_flac_rebind(sc_audio_file *file);
#endif
