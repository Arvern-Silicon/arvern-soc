#ifndef STUB_SDL_MIXER_H
#define STUB_SDL_MIXER_H
#include <SDL.h>
typedef struct Mix_Chunk { int allocated; Uint8 *abuf; Uint32 alen; Uint8 volume; } Mix_Chunk;
typedef void (*Mix_EffectFunc_t)(int, void *, int, void *);
typedef void (*Mix_EffectDone_t)(int, void *);
#define MIX_DEFAULT_FORMAT AUDIO_S16LSB
#define MIX_CHANNEL_POST (-2)
#define MIX_CHANNELS 8
int Mix_OpenAudioDevice(int, Uint16, int, int, const char *, int);
int Mix_OpenAudio(int, Uint16, int, int);
void Mix_CloseAudio(void);
int Mix_QuerySpec(int *, Uint16 *, int *);
int Mix_AllocateChannels(int);
int Mix_ReserveChannels(int);
int Mix_GroupChannels(int, int, int);
int Mix_GroupAvailable(int);
int Mix_GroupOldest(int);
int Mix_PlayChannel(int, Mix_Chunk *, int);
int Mix_HaltChannel(int);
int Mix_Playing(int);
int Mix_SetPanning(int, Uint8, Uint8);
int Mix_SetPosition(int, Sint16, Uint8);
Mix_Chunk *Mix_QuickLoad_WAV(Uint8 *);
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *, int);
void Mix_FreeChunk(Mix_Chunk *);
void Mix_SetPostMix(void (*)(void *, Uint8 *, int), void *);
void Mix_HookMusic(void (*)(void *, Uint8 *, int), void *);
void Mix_ChannelFinished(void (*)(int));
int Mix_RegisterEffect(int, Mix_EffectFunc_t, Mix_EffectDone_t, void *);
const char *Mix_GetError(void);
int Mix_Volume(int, int);
#endif
