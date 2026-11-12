/* data.h - formatos de los archivos generados por tools/extract.py */
#ifndef DATA_H
#define DATA_H
#include <stdint.h>
#include "vmath.h"

/* tipos de componente (deben coincidir con tools/extract.py) */
enum {
    C_MESH = 1, C_SKIN, C_BOX, C_SPHERE, C_CAPSULE, C_TRACKCHUNK, C_RANDOMIZER, C_RANDTARGET, C_GROUP,
    C_SPAWNPOINT, C_RANDOFFSET, C_MIRROR, C_MOVINGTRAIN, C_MOVINGCOIN, C_COINPLACE, C_COINLINE,
    C_COINJUMP, C_PICKUP, C_PICKUPROTATE, C_PICKUPDEFAULT, C_TUNNEL, C_TRACKOBJECT, C_RENDACT,
    C_SNAP, C_GLOW, C_TUTORIAL, C_ANIMATION, C_CHARCTRL, C_CAMERA, C_COIN, C_VISIBLE, C_SSGROUP,
    C_ONTRIGGER, C_DAILYLETTER, C_CURVEPOINT, C_JETCLOUD, C_RANDHOLD, C_SPAWNEGG
};

enum { TAG_UNTAGGED, TAG_PLAYER, TAG_HITMOVINGTRAIN, TAG_JUMPTRAIN, TAG_DODGETRAIN, TAG_ROLLBARRIER,
       TAG_JUMPBARRIER, TAG_HITBARRIER, TAG_HITTRAIN, TAG_GROUND, TAG_STATION, TAG_SUBWAY,
       TAG_JUMPHIGHBARRIER, TAG_MAINCAMERA };

enum { PK_COIN, PK_MAGNET, PK_SNEAKERS, PK_JETPACK, PK_DOUBLE, PK_MYSTERY, PK_LETTER, PK_EGG, PK_KEY };

enum { BLEND_OPAQUE, BLEND_ALPHA, BLEND_ADD, BLEND_MUL, BLEND_ADDALPHA };

typedef struct {
    int32_t parent, first_child, next_sibling;
    float pos[3], rot[4], scl[3];
    uint16_t name;
    uint8_t layer, tag, active, ncomp;
    uint16_t pad;
    uint32_t comp_off;
} NodeFile;   /* 64 bytes */

typedef struct {
    char name[32];
    int32_t tex;
    uint32_t blend;
    float st[4];      /* escala u, v; desplazamiento u, v */
    float color[4];
    float distort[2];
    uint32_t sky;
} MatFile;

/* w: pesos para la curvatura exacta en el GE (32768 = 1): 1 y la posicion normalizada a la caja del modelo */
typedef struct {
    uint16_t w[4];
    float u, v, x, y, z;
} MVert;

typedef struct {
    float w[4];
    uint8_t b[4];
} MSkin;

typedef struct {
    int sub, nv, ni;
    V3 center; float radius;
    MVert *verts;
    uint16_t *idx;
    MSkin *skin;
    int lod_ni[2];         /* versiones ligeras para verlo de lejos: otros triangulos sobre los mismos vertices */
    uint16_t *lod_idx[2];
} MeshPart;

typedef struct {
    int sub, nb, nv, ni;
    uint8_t bones[8];
    float *verts;          /* por vertice: nb pesos, u, v, x, y, z */
    uint16_t *idx;
} SkinBatch;

typedef struct {
    char name[32];
    int nparts, nsub, skinned;
    int nbatches;
    SkinBatch *batches;
    V3 center; float radius;
    V3 bmin, bmax;
    MeshPart *parts;
    int nbind;
    M34 *bind;
} Mesh;

typedef struct {
    char name[32];
    int w, h, fmt, alpha;
    uint32_t file_off, palbytes, databytes;
    void *pal, *data;     /* cargados bajo demanda */
    int loaded;
    int src;              /* 0 tex.bin, 1 hw_tex.bin (tema de Halloween) */
} Texture;

enum { TEX_T8, TEX_5650, TEX_4444, TEX_8888 };

typedef struct {
    uint16_t path, flags;    /* 1 pos, 2 rot, 4 escala */
    float *data;             /* por frame: [pos3][rot4][scl3] segun flags */
    int stride;
} AnimTrack;

typedef struct { float time; char fn[32]; } AnimEvent;

typedef struct {
    char name[48];
    float length, sample_rate;
    int wrap, nframes, ntracks, nevents;
    AnimEvent *events;
    AnimTrack *tracks;
} Clip;

typedef struct {
    char name[32];
    uint32_t off, n;    /* muestras s16 22050 Hz */
    int16_t *pcm;
} SfxData;

typedef struct {
    int nnodes;
    NodeFile *nodes;
    uint32_t *blob;
    int nstrings;
    char **strings;
    int nmats; MatFile *mats;
    int nmeshes; Mesh *meshes;
    int ntex; Texture *tex;
    int nclips; Clip *clips;
    int nsfx; SfxData *sfx;
} GameData;

extern GameData gd;

int data_load(const char *dir);
#define DATA_STEPS 6
int data_load_step(const char *dir, int step);   /* 0..DATA_STEPS-1 archivos, DATA_STEPS comprobacion final */
void data_tex_path(int i, char *out, int n);
void *data_load_world_opt(const char *name);
int data_xmas_meshes_init(void);   /* mallas de Navidad (xm_mesh.bin) */
extern int data_xm_ok;            /* hay xm_tex.bin (Navidad de la 1.5.2) */   /* archivo opcional de la carpeta del mundo */
int data_tex_read(int i, void *pal, void *data);
/* tema de Halloween */
int data_theme_init_tex(void);      /* tras leer tex.bin, antes de r_init: 1 si esta hw_tex.bin */
extern int data_hw_ok;              /* hay datos de Halloween (hw_tex.bin, del APK de la 1.4) */
int data_theme_init(int need_meshes);   /* con todo cargado: 1 si hay tema de Halloween */
int data_mesh_find(const char *name);
int data_mesh_acquire(int mesh);     /* personajes de la 1.4: lee la malla si no esta */
void data_mesh_release(int mesh);    /* y la libera */
int data_tex_find(const char *name);

/* tablas de la 1.5.2 (boards.bin): acciones en el orden de HoverboardRendering */
enum { BA_RUN, BA_LAND, BA_JUMP, BA_HANG, BA_ROLL, BA_LEFT, BA_RIGHT, BA_GRIND, BA_GRINDLAND, BA_GETON, BOARD_NACT };
enum { BOARD_ALWAYS, BOARD_FREE, BOARD_COINS, BOARD_SEASON, BOARD_SEASON_RIO, BOARD_SEASON_ROME, BOARD_EGGS, BOARD_SEASON_SYDNEY, BOARD_SEASON_TOKYO, BOARD_HUNT };   /* SEASON: de monedas, solo en su temporada (Nueva York / Rio) */
enum { BOARD_FX_NONE, BOARD_FX_SUPERJUMP, BOARD_FX_LOW };
#define BOARD_MAX_CLIPS 32
typedef struct {
    char name[32], mesh[32];       /* mesh vacio: la tabla de la 1.0 */
    int price, unlock, effect;
    int nact[BOARD_NACT];
    const char (*act[BOARD_NACT])[32];
} BoardDef;
extern BoardDef *gd_boards;
extern int gd_nboards;
int data_boards_load(void);                                   /* 0 si no hay boards.bin */
int data_board_clips_load(const char *const *names, int n);   /* de boardanim.bin, las que falten */
void data_board_clips_free(void);
int data_mat_clone(const char *base, int tex);
int data_theme_has_tex(int t);
void data_theme_set(int on);
const char *data_err(void);
int data_tex_load(int i);
const uint32_t *node_comp(int node, int type, int *nwords);    /* primer componente de ese tipo */
int node_find_child(int node, const char *name);
int node_find_root(const char *name);
int node_find_path(int from, const char *path);                 /* "a/b/c" */
int clip_find(const char *name);
int sfx_find(const char *name);
static inline float u2f(uint32_t u) { union { uint32_t u; float f; } c; c.u = u; return c.f; }

#endif
