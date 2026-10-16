/* vmath.h - vectores, cuaterniones y matrices afines (convenio de Unity: x derecha, y arriba, z adelante) */
#ifndef VMATH_H
#define VMATH_H
#include <math.h>

typedef struct { float x, y, z; } V3;
typedef struct { float x, y, z, w; } Q4;
/* matriz afin 3x4 por columnas: p' = c0*x + c1*y + c2*z + c3 */
typedef struct { float m[12]; } M34;

#define DEG2RAD 0.017453292519943295f
#define RAD2DEG 57.29577951308232f

static inline V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static inline V3 v3add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline V3 v3mul(V3 a, V3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline float v3dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3len(V3 a) { return sqrtf(v3dot(a, a)); }
static inline V3 v3norm(V3 a) { float l = v3len(a); return l > 1e-9f ? v3scale(a, 1.0f / l) : v3(0, 0, 0); }
static inline V3 v3lerp(V3 a, V3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float clamp01(float x) { return clampf(x, 0.0f, 1.0f); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * clamp01(t); }
static inline float lerpu(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstepf(float from, float to, float t)
{   /* Mathf.SmoothStep de Unity */
    t = clamp01(t);
    t = -2.0f * t * t * t + 3.0f * t * t;
    return to * t + from * (1.0f - t);
}
static inline float signf(float x) { return x >= 0.0f ? 1.0f : -1.0f; }

static inline Q4 q4(float x, float y, float z, float w) { Q4 r = {x, y, z, w}; return r; }
static inline Q4 qident(void) { return q4(0, 0, 0, 1); }
static inline Q4 qmul(Q4 a, Q4 b)
{
    return q4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
              a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
              a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
static inline Q4 qnorm(Q4 q)
{
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-9f) return qident();
    l = 1.0f / l;
    return q4(q.x * l, q.y * l, q.z * l, q.w * l);
}
/* Quaternion.Euler de Unity (orden Z, X, Y) */
static inline Q4 qeuler(float ex, float ey, float ez)
{
    float hx = ex * DEG2RAD * 0.5f, hy = ey * DEG2RAD * 0.5f, hz = ez * DEG2RAD * 0.5f;
    Q4 qx = q4(sinf(hx), 0, 0, cosf(hx));
    Q4 qy = q4(0, sinf(hy), 0, cosf(hy));
    Q4 qz = q4(0, 0, sinf(hz), cosf(hz));
    return qmul(qy, qmul(qx, qz));
}
static inline V3 qrot(Q4 q, V3 v)
{
    V3 u = v3(q.x, q.y, q.z);
    V3 t = v3scale(v3cross(u, v), 2.0f);
    return v3add(v3add(v, v3scale(t, q.w)), v3cross(u, t));
}

static inline void m34_identity(M34 *m)
{
    static const float id[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (int i = 0; i < 12; i++) m->m[i] = id[i];
}
static inline void m34_trs(M34 *m, V3 p, Q4 q, V3 s)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m->m[0] = (1 - 2 * (yy + zz)) * s.x; m->m[1] = (2 * (xy + wz)) * s.x;     m->m[2] = (2 * (xz - wy)) * s.x;
    m->m[3] = (2 * (xy - wz)) * s.y;     m->m[4] = (1 - 2 * (xx + zz)) * s.y; m->m[5] = (2 * (yz + wx)) * s.y;
    m->m[6] = (2 * (xz + wy)) * s.z;     m->m[7] = (2 * (yz - wx)) * s.z;     m->m[8] = (1 - 2 * (xx + yy)) * s.z;
    m->m[9] = p.x; m->m[10] = p.y; m->m[11] = p.z;
}
static inline void m34_mul(M34 *r, const M34 *a, const M34 *b)
{   /* r = a * b */
    M34 t;
    for (int c = 0; c < 4; c++) {
        float bx = b->m[c * 3], by = b->m[c * 3 + 1], bz = b->m[c * 3 + 2];
        t.m[c * 3 + 0] = a->m[0] * bx + a->m[3] * by + a->m[6] * bz;
        t.m[c * 3 + 1] = a->m[1] * bx + a->m[4] * by + a->m[7] * bz;
        t.m[c * 3 + 2] = a->m[2] * bx + a->m[5] * by + a->m[8] * bz;
    }
    t.m[9] += a->m[9]; t.m[10] += a->m[10]; t.m[11] += a->m[11];
    *r = t;
}
static inline V3 m34_point(const M34 *m, V3 p)
{
    return v3(m->m[0] * p.x + m->m[3] * p.y + m->m[6] * p.z + m->m[9],
              m->m[1] * p.x + m->m[4] * p.y + m->m[7] * p.z + m->m[10],
              m->m[2] * p.x + m->m[5] * p.y + m->m[8] * p.z + m->m[11]);
}
static inline V3 m34_dir(const M34 *m, V3 p)
{
    return v3(m->m[0] * p.x + m->m[3] * p.y + m->m[6] * p.z,
              m->m[1] * p.x + m->m[4] * p.y + m->m[7] * p.z,
              m->m[2] * p.x + m->m[5] * p.y + m->m[8] * p.z);
}
static inline V3 m34_col(const M34 *m, int c) { return v3(m->m[c * 3], m->m[c * 3 + 1], m->m[c * 3 + 2]); }
static inline V3 m34_pos(const M34 *m) { return v3(m->m[9], m->m[10], m->m[11]); }
/* inversa de una matriz afin general */
static inline void m34_inverse(M34 *r, const M34 *a)
{
    const float *m = a->m;
    float det = m[0] * (m[4] * m[8] - m[7] * m[5]) - m[3] * (m[1] * m[8] - m[7] * m[2]) + m[6] * (m[1] * m[5] - m[4] * m[2]);
    float id = fabsf(det) > 1e-12f ? 1.0f / det : 0.0f;
    M34 t;
    t.m[0] = (m[4] * m[8] - m[7] * m[5]) * id;
    t.m[1] = (m[7] * m[2] - m[1] * m[8]) * id;
    t.m[2] = (m[1] * m[5] - m[4] * m[2]) * id;
    t.m[3] = (m[6] * m[5] - m[3] * m[8]) * id;
    t.m[4] = (m[0] * m[8] - m[6] * m[2]) * id;
    t.m[5] = (m[3] * m[2] - m[0] * m[5]) * id;
    t.m[6] = (m[3] * m[7] - m[6] * m[4]) * id;
    t.m[7] = (m[6] * m[1] - m[0] * m[7]) * id;
    t.m[8] = (m[0] * m[4] - m[3] * m[1]) * id;
    V3 p = v3(m[9], m[10], m[11]);
    t.m[9] = -(t.m[0] * p.x + t.m[3] * p.y + t.m[6] * p.z);
    t.m[10] = -(t.m[1] * p.x + t.m[4] * p.y + t.m[7] * p.z);
    t.m[11] = -(t.m[2] * p.x + t.m[5] * p.y + t.m[8] * p.z);
    *r = t;
}

/* Mathf.SmoothDamp de Unity */
static inline float smoothdamp(float current, float target, float *vel, float smoothTime, float dt)
{
    if (smoothTime < 0.0001f) smoothTime = 0.0001f;
    float omega = 2.0f / smoothTime;
    float x = omega * dt;
    float ex = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float orig = target;
    target = current - change;
    float temp = (*vel + omega * change) * dt;
    *vel = (*vel - omega * temp) * ex;
    float out = target + (change + temp) * ex;
    if ((orig - current > 0.0f) == (out > orig)) {
        out = orig;
        *vel = (out - orig) / dt;
    }
    return out;
}

/* Generador aleatorio (xorshift) */
unsigned int rnd_u32(void);
static inline float rnd01(void) { return (rnd_u32() >> 8) * (1.0f / 16777216.0f); }
/* Random.Range(int,int): [a, b) */
static inline int rnd_range(int a, int b) { return b <= a ? a : a + (int)(rnd_u32() % (unsigned)(b - a)); }
/* Random.Range(float,float) */
static inline float rnd_rangef(float a, float b) { return a + (b - a) * rnd01(); }

#endif
