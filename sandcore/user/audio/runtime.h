#ifndef SAND_AUDIO_RUNTIME_H
#define SAND_AUDIO_RUNTIME_H
#include "../SCAPI.H"
void *mp_memory_copy(void *,const void *,u32);
void *mp_memory_move(void *,const void *,u32);
void *mp_memory_zero(void *,u32);
void *mp_allocate(u32);
void *mp_reallocate(void *,u32);
void mp_release(void *);
void mp_assert(int);
#endif
