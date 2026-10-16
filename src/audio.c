/* audio.c
 * - Efectos: PCM 16 bits 22050 Hz mono (sfx.bin) mezclados en un hilo con tono variable (16 voces).
 * - Musica: MP3 original decodificado por el Media Engine (sceMp3) o AT3 (sceAtrac) si el usuario lo convierte.
 */
#include <pspkernel.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <pspatrac3.h>
#include <psputility.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include "audio.h"
#include "data.h"
#include "vmath.h"

#define MAX_VOICES 16
#define MIX_SAMPLES 512

typedef struct {
    const int16_t *pcm;
    uint32_t n;
    uint32_t pos;          /* 16.16 */
    uint32_t step;         /* 16.16 */
    int vol;               /* 0..256 */
    int loop;
    volatile int active;
    int gen;
} Voice;

static Voice voices[MAX_VOICES];
static int16_t __attribute__((aligned(64))) mixbuf[2][MIX_SAMPLES * 2];
static volatile int audio_running, audio_paused;
static SceUID mix_thid = -1, mus_thid = -1;

static int mixer_thread(SceSize args, void *argp)
{
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, MIX_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    int cur = 0;
    static int acc[MIX_SAMPLES];
    while (audio_running) {
        memset(acc, 0, sizeof(acc));
        if (!audio_paused) {
            for (int vi = 0; vi < MAX_VOICES; vi++) {
                Voice *v = &voices[vi];
                if (!v->active) continue;
                int vol = v->vol;
                uint32_t end = v->n << 16;
                for (int i = 0; i < MIX_SAMPLES; i++) {
                    if (v->pos >= end) {
                        if (v->loop && v->n > 0) v->pos -= end;
                        else { v->active = 0; break; }
                    }
                    uint32_t ip = v->pos >> 16, fr = (v->pos >> 8) & 0xff;
                    int s0 = v->pcm[ip], s1 = ip + 1 < v->n ? v->pcm[ip + 1] : s0;
                    int s = s0 + (((s1 - s0) * (int)fr) >> 8);
                    acc[i] += (s * vol) >> 8;
                    v->pos += v->step;
                }
            }
        }
        int16_t *out = mixbuf[cur];
        for (int i = 0; i < MIX_SAMPLES; i++) {
            int s = acc[i];
            if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
            out[i * 2] = out[i * 2 + 1] = (int16_t)s;
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, out);
        cur ^= 1;
    }
    sceAudioChRelease(ch);
    return 0;
}

int snd_play(int id, float vol, float pitch, int loop)
{
    if (id < 0 || id >= gd.nsfx || gd.sfx[id].n == 0) return -1;
    int best = -1;
    for (int i = 0; i < MAX_VOICES; i++) if (!voices[i].active) { best = i; break; }
    if (best < 0) {
        uint32_t far = 0;
        for (int i = 0; i < MAX_VOICES; i++)
            if (!voices[i].loop && voices[i].pos >= far) { far = voices[i].pos; best = i; }
        if (best < 0) return -1;
    }
    Voice *v = &voices[best];
    v->active = 0;
    v->pcm = gd.sfx[id].pcm;
    v->n = gd.sfx[id].n;
    v->pos = 0;
    if (pitch < 0.05f) pitch = 0.05f;
    v->step = (uint32_t)(pitch * 0.5f * 65536.0f);   /* 22050 -> 44100 */
    v->vol = (int)(clamp01(vol) * 256.0f);
    v->loop = loop;
    v->gen = (v->gen + 1) & 0xffff;
    v->active = 1;
    return best | (v->gen << 8);
}

static Voice *voice_of(int h)
{
    if (h < 0) return NULL;
    Voice *v = &voices[h & 0xff];
    return ((h >> 8) & 0xffff) == v->gen ? v : NULL;
}

void snd_stop(int h) { Voice *v = voice_of(h); if (v) v->active = 0; }
int snd_playing(int h) { Voice *v = voice_of(h); return v && v->active; }
void snd_set(int h, float vol, float pitch)
{
    Voice *v = voice_of(h);
    if (!v) return;
    v->vol = (int)(clamp01(vol) * 256.0f);
    if (pitch < 0.05f) pitch = 0.05f;
    v->step = (uint32_t)(pitch * 0.5f * 65536.0f);
}
void snd_stop_all(void) { for (int i = 0; i < MAX_VOICES; i++) voices[i].active = 0; }
void snd_pause_all(int p) { audio_paused = p; }

int snd_clip(const ClipInfo *ci)
{
    if (!ci || ci->sfx < 0) return -1;
    return snd_play(ci->sfx, rnd_rangef(ci->minVol, ci->maxVol), rnd_rangef(ci->minPitch, ci->maxPitch), 0);
}

/* ---------------- musica ---------------- */
static unsigned char __attribute__((aligned(64))) mp3_buf[16 * 1024];
static short __attribute__((aligned(64))) pcm_buf[16 * (1152 / 2)];
static char mus_req_path[256];
static volatile int mus_req, mus_req_loop, mus_stop_req, mus_restart_req, mus_playing, mus_paused;
static volatile float mus_vol = 1.0f;
static int mp3_ok, at3_ok;
static char basedir[200];

static int fill_stream(SceUID fd, int handle)
{
    unsigned char *dst; SceInt32 write, pos;
    if (sceMp3GetInfoToAddStreamData(handle, &dst, &write, &pos) < 0) return -1;
    if (sceIoLseek32(fd, pos, PSP_SEEK_SET) < 0) return -1;
    int rd = sceIoRead(fd, dst, write);
    if (rd < 0) return -1;
    sceMp3NotifyAddStreamData(handle, rd);
    return rd;
}

static int ends_with(const char *s, const char *e)
{
    int a = strlen(s), b = strlen(e);
    return a >= b && !strcmp(s + a - b, e);
}

static int music_thread(SceSize args, void *argp)
{
    SceUID fd = -1;
    int handle = -1, src_ch = -1, last_samples = 0, rate = 44100;
    /* AT3 */
    int at3_id = -1, at3_ch = -1;
    unsigned char *at3_data = NULL;
    int at3_size = 0;
    static short __attribute__((aligned(64))) at3_out[2048 * 2];
    while (audio_running) {
        if (mus_req || mus_stop_req || mus_restart_req) {
            if (mus_restart_req && !mus_req && !mus_stop_req) {
                /* volver al principio: se vuelve a abrir */
                mus_req = 1;
            }
            mus_restart_req = 0;
            if (handle >= 0) { sceMp3ReleaseMp3Handle(handle); handle = -1; }
            if (fd >= 0) { sceIoClose(fd); fd = -1; }
            if (at3_id >= 0) { sceAtracReleaseAtracID(at3_id); at3_id = -1; }
            if (at3_data) { free(at3_data); at3_data = NULL; }
            mus_playing = 0;
            if (mus_stop_req) { mus_stop_req = 0; mus_req = 0; }
            if (mus_req) {
                mus_req = 0;
                if (ends_with(mus_req_path, ".at3") && at3_ok) {
                    SceUID f = sceIoOpen(mus_req_path, PSP_O_RDONLY, 0);
                    if (f >= 0) {
                        at3_size = sceIoLseek32(f, 0, PSP_SEEK_END);
                        sceIoLseek32(f, 0, PSP_SEEK_SET);
                        at3_data = memalign(64, at3_size);
                        if (at3_data) sceIoRead(f, at3_data, at3_size);
                        sceIoClose(f);
                        if (at3_data) {
                            at3_id = sceAtracSetDataAndGetID(at3_data, at3_size);
                            if (at3_id >= 0) {
                                sceAtracSetLoopNum(at3_id, mus_req_loop ? -1 : 0);
                                if (at3_ch < 0) at3_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 2048, PSP_AUDIO_FORMAT_STEREO);
                                mus_playing = 1;
                            }
                        }
                    }
                } else if (mp3_ok) {
                    fd = sceIoOpen(mus_req_path, PSP_O_RDONLY, 0);
                    if (fd >= 0) {
                        SceMp3InitArg a;
                        a.mp3StreamStart = 0;
                        a.mp3StreamEnd = sceIoLseek32(fd, 0, PSP_SEEK_END);
                        a.mp3Buf = mp3_buf; a.mp3BufSize = sizeof(mp3_buf);
                        a.pcmBuf = (SceUChar8 *)pcm_buf; a.pcmBufSize = sizeof(pcm_buf);
                        handle = sceMp3ReserveMp3Handle(&a);
                        if (handle >= 0 && fill_stream(fd, handle) >= 0 && sceMp3Init(handle) >= 0) {
                            sceMp3SetLoopNum(handle, mus_req_loop ? -1 : 0);
                            rate = sceMp3GetSamplingRate(handle);
                            if (rate <= 0) rate = 44100;
                            mus_playing = 1;
                        } else {
                            if (handle >= 0) sceMp3ReleaseMp3Handle(handle);
                            handle = -1; sceIoClose(fd); fd = -1;
                        }
                    }
                }
            }
        }
        int vol = (int)(clamp01(mus_vol) * PSP_AUDIO_VOLUME_MAX);
        if (mus_paused) { sceKernelDelayThread(10000); continue; }
        if (at3_id >= 0) {
            int samples = 0, end = 0, remain = 0;
            memset(at3_out, 0, sizeof(at3_out));
            int r = sceAtracDecodeData(at3_id, (u16 *)at3_out, &samples, &end, &remain);
            if (r < 0 || samples <= 0) {
                if (end && mus_req_loop) { mus_restart_req = 1; continue; }
                sceAtracReleaseAtracID(at3_id); at3_id = -1; mus_playing = 0;
                continue;
            }
            sceAudioOutputBlocking(at3_ch, vol, at3_out);
            continue;
        }
        if (handle < 0) { sceKernelDelayThread(10000); continue; }
        if (sceMp3CheckStreamDataNeeded(handle) > 0) fill_stream(fd, handle);
        short *buf;
        int bytes = sceMp3Decode(handle, &buf);
        if (bytes <= 0) {
            sceMp3ReleaseMp3Handle(handle); handle = -1; sceIoClose(fd); fd = -1; mus_playing = 0;
            continue;
        }
        int samples = bytes / 4;
        if (src_ch < 0 || samples != last_samples) {
            if (src_ch >= 0) sceAudioSRCChRelease();
            src_ch = sceAudioSRCChReserve(samples, rate, 2);
            last_samples = samples;
        }
        sceAudioSRCOutputBlocking(vol, buf);
    }
    if (handle >= 0) sceMp3ReleaseMp3Handle(handle);
    if (fd >= 0) sceIoClose(fd);
    if (src_ch >= 0) sceAudioSRCChRelease();
    if (at3_id >= 0) sceAtracReleaseAtracID(at3_id);
    if (at3_ch >= 0) sceAudioChRelease(at3_ch);
    return 0;
}

void music_play(const char *name, int loop)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/music/%s.at3", basedir, name);
    SceUID f = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (f >= 0 && at3_ok) sceIoClose(f);
    else {
        if (f >= 0) sceIoClose(f);
        snprintf(path, sizeof(path), "%s/music/%s.mp3", basedir, name);
    }
    while (mus_req) sceKernelDelayThread(1000);
    snprintf(mus_req_path, sizeof(mus_req_path), "%s", path);
    mus_req_loop = loop;
    mus_req = 1;
}
void music_stop(void) { mus_stop_req = 1; }
void music_restart(void) { mus_restart_req = 1; }
void music_set_volume(float v) { mus_vol = v; }
void music_pause(int p) { mus_paused = p; }

int audio_init(void)
{
    /* los hilos no tienen directorio actual en la PSP: ruta absoluta */
    if (!getcwd(basedir, sizeof(basedir))) strcpy(basedir, "ms0:/PSP/GAME/SUBWAY");
    strncat(basedir, "/data", sizeof(basedir) - strlen(basedir) - 1);
    sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    at3_ok = sceUtilityLoadModule(PSP_MODULE_AV_ATRAC3PLUS) >= 0;
    mp3_ok = sceMp3InitResource() >= 0;
    audio_running = 1;
    mix_thid = sceKernelCreateThread("sfx_mixer", mixer_thread, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (mix_thid >= 0) sceKernelStartThread(mix_thid, 0, NULL);
    mus_thid = sceKernelCreateThread("music", music_thread, 0x12, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (mus_thid >= 0) sceKernelStartThread(mus_thid, 0, NULL);
    return 0;
}

void audio_shutdown(void)
{
    audio_running = 0;
    if (mix_thid >= 0) { sceKernelWaitThreadEnd(mix_thid, NULL); sceKernelDeleteThread(mix_thid); }
    if (mus_thid >= 0) { sceKernelWaitThreadEnd(mus_thid, NULL); sceKernelDeleteThread(mus_thid); }
    if (mp3_ok) sceMp3TermResource();
}
