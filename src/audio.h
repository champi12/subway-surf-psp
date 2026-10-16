/* audio.h - efectos (mezclador propio con tono/volumen como So.playSound) y musica (MP3 por hardware o AT3) */
#ifndef AUDIO_H
#define AUDIO_H

int audio_init(void);
void audio_shutdown(void);

/* efectos: devuelve un identificador (o -1) */
int snd_play(int sfx, float vol, float pitch, int loop);
void snd_stop(int h);
void snd_set(int h, float vol, float pitch);
int snd_playing(int h);
void snd_stop_all(void);
void snd_pause_all(int pause);

/* reproduce un AudioClipInfo del original: volumen y tono aleatorios entre min y max */
typedef struct { int sfx; float minPitch, maxPitch, minVol, maxVol; } ClipInfo;
int snd_clip(const ClipInfo *ci);

/* musica: busca music/<nombre>.at3 y si no existe music/<nombre>.mp3 */
void music_play(const char *name, int loop);
void music_stop(void);
void music_restart(void);
void music_set_volume(float v);   /* 0..1 */
void music_pause(int pause);

#endif
