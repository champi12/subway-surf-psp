#!/usr/bin/env python3
"""Convierte el APK de Subway Surfers 1.0 (Android, Unity 3.5) en los datos del port de PSP.

Uso:
    python3 tools/extract.py com.kiloo.subwaysurf_1.0.apk data/ [--apk14 subway-surfers-1.4.apk]

Con --apk14 (o --halloween) se saca ademas, del APK de la 1.4 clasica (Halloween de 2012), lo que cambia su tema de Halloween
(ThemeAssets): las texturas del escenario, los trenes, el tren del logo y los enemigos, y las mallas del inspector
y el perro zombis. Van a data/hw_tex.bin y data/hw_mesh.bin y el juego las usa en lugar de las normales del 24 de
octubre al 4 de noviembre (segun el reloj de la consola).

Genera en data/:
    world.bin   jerarquia de la escena (tramos de via, obstaculos, monedas, personaje...)
    mesh.bin    modelos (pistas, trenes, personajes) con triangulos grandes subdivididos
    tex.bin     texturas reducidas a 512x512 maximo, con paleta de 256 colores o 16 bits, swizzled
    anim.bin    animaciones muestreadas a 30 fps
    sfx.bin     efectos de sonido (PCM 16 bits 22050 Hz mono)
    music/      musica original (MP3, la PSP la decodifica por hardware)
    wav/        todos los sonidos en WAV para convertirlos a AT3 si se quiere

Nada de esto se guarda en el repositorio: cada uno lo genera con su propia copia del juego.
"""
import atexit
import io
import math
import re
import os
import shutil
import struct
import sys
import tempfile
import wave
import zipfile

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unityscene import Scene  # noqa: E402
from UnityPy.helpers.MeshHelper import MeshHandler  # noqa: E402

# ---------------------------------------------------------------- formato
# Debe coincidir con src/worldfmt.h
C_MESH, C_SKIN, C_BOX, C_SPHERE, C_CAPSULE, C_TRACKCHUNK, C_RANDOMIZER, C_RANDTARGET, C_GROUP, \
    C_SPAWNPOINT, C_RANDOFFSET, C_MIRROR, C_MOVINGTRAIN, C_MOVINGCOIN, C_COINPLACE, C_COINLINE, \
    C_COINJUMP, C_PICKUP, C_PICKUPROTATE, C_PICKUPDEFAULT, C_TUNNEL, C_TRACKOBJECT, C_RENDACT, \
    C_SNAP, C_GLOW, C_TUTORIAL, C_ANIMATION, C_CHARCTRL, C_CAMERA, C_COIN, C_VISIBLE, C_SSGROUP, \
    C_ONTRIGGER, C_DAILYLETTER, C_CURVEPOINT, C_JETCLOUD = range(1, 37)

TAG_IDS = {'Untagged': 0, 'Player': 1, 'HitMovingTrain': 2, 'JumpTrain': 3, 'DodgeTrain': 4, 'RollBarrier': 5,
           'JumpBarrier': 6, 'HitBarrier': 7, 'HitTrain': 8, 'Ground': 9, 'Station': 10, 'Subway': 11,
           'JumpHighBarrier': 12, 'MainCamera': 13}

PICKUP_KIND = {'Coin': 0, 'CoinMagnetPickup': 1, 'SuperSneakersPickup': 2, 'JetpackPickup': 3,
               'DoubleScoreMultiplierPickup': 4, 'MysteryBoxPickup': 5, 'DailyLetterPickup': 6}

BLEND_OPAQUE, BLEND_ALPHA, BLEND_ADD, BLEND_MUL, BLEND_ADDALPHA = range(5)

MAX_EDGE = 0.0       # >0: subdivide triangulos grandes (no hace falta: el motor recorta por software)
ANIM_FPS = 30.0


def f2u(x):
    return struct.unpack('<I', struct.pack('<f', float(x)))[0]


class Strings:
    def __init__(self):
        self.lst = ['']
        self.idx = {'': 0}

    def get(self, s):
        if s not in self.idx:
            self.idx[s] = len(self.lst)
            self.lst.append(s)
        return self.idx[s]

    def blob(self):
        out = bytearray(struct.pack('<I', len(self.lst)))
        for s in self.lst:
            b = s.encode('utf-8')[:255]
            out += struct.pack('<B', len(b)) + b
        while len(out) % 4:
            out += b'\0'
        return bytes(out)


def key(obj):
    return (os.path.basename(obj.assets_file.name), obj.path_id)


# ---------------------------------------------------------------- escena
class Exporter:
    def __init__(self, sc):
        self.sc = sc
        self.strings = Strings()
        self.nodes = []          # dicts
        self.objmap = {}         # key(GO o componente) -> nodo
        self.meshes = []         # objetos Mesh
        self.mesh_idx = {}
        self.mesh_world = []     # True si es escenario (subdividir)
        self.materials = []      # dicts
        self.mat_idx = {}
        self.textures = []
        self.tex_idx = {}
        self.clips = []
        self.clip_idx = {}
        self.audio = []
        self.audio_idx = {}
        self.extra_anim = {}     # nombre del GameObject -> clips que se le anaden (poses de la 1.4)
        self.pending = []        # (nodo, comp, campo) a resolver despues

    # -- recursos --
    def mesh_id(self, obj, world=True):
        if obj is None:
            return -1
        k = key(obj)
        if k not in self.mesh_idx:
            self.mesh_idx[k] = len(self.meshes)
            self.meshes.append(obj)
            self.mesh_world.append(world)
        elif not world:
            self.mesh_world[self.mesh_idx[k]] = False
        return self.mesh_idx[k]

    def tex_id(self, obj):
        if obj is None:
            return -1
        k = key(obj)
        if k not in self.tex_idx:
            self.tex_idx[k] = len(self.textures)
            self.textures.append(obj)
        return self.tex_idx[k]

    def mat_id(self, obj):
        if obj is None:
            return -1
        k = key(obj)
        if k in self.mat_idx:
            return self.mat_idx[k]
        d = obj.read_typetree()
        sh = self.sc.resolve(obj.assets_file, d['m_Shader']['m_FileID'], d['m_Shader']['m_PathID'])
        shname = ''
        if sh is not None:
            import re
            s = sh.read_typetree().get('m_Script', '')
            m = re.search(r'Shader "([^"]+)"', s)
            shname = m.group(1) if m else ''
        blend = BLEND_OPAQUE
        if 'Additive' in shname:
            blend = BLEND_ADDALPHA if 'Particles' in shname else BLEND_ADD
        elif 'Multiply' in shname:
            blend = BLEND_MUL
        elif 'Transparent' in shname or 'AlphaBlended' in shname:
            blend = BLEND_ALPHA
        tex = -1
        st = (1.0, 1.0, 0.0, 0.0)
        for name, t in d['m_SavedProperties']['m_TexEnvs']:
            nm = name['name'] if isinstance(name, dict) else name
            if nm in ('_MainTex', '_ShadowTex'):
                to = self.sc.resolve(obj.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                if to is not None and to.type.name == 'Texture2D':
                    tex = self.tex_id(to)
                st = (t['m_Scale']['x'], t['m_Scale']['y'], t['m_Offset']['x'], t['m_Offset']['y'])
        col = [1.0, 1.0, 1.0, 1.0]
        cols = {}
        for name, c in d['m_SavedProperties']['m_Colors']:
            nm = name['name'] if isinstance(name, dict) else name
            cols[nm] = (c['r'], c['g'], c['b'], c['a'])
        if '_MainColor' in cols and 'Distorted' in shname:
            c = cols['_MainColor']
            col = [c[0], c[1], c[2], 1.0]
        elif '_TintColor' in cols:
            col = list(cols['_TintColor'])
        elif '_Color' in cols and 'Transparent' in shname:
            col = list(cols['_Color'])
        if 'Projector' in shname:
            blend = BLEND_MUL
        dist = cols.get('_Distort', (-0.0005, -0.0003, 0, 0))
        m = {'name': d['m_Name'], 'shader': shname, 'blend': blend, 'tex': tex, 'st': st, 'color': col,
             'distort': (dist[0], dist[1]), 'sky': 'Sky' in shname}
        self.mat_idx[k] = len(self.materials)
        self.materials.append(m)
        return self.mat_idx[k]

    def clip_id(self, obj):
        if obj is None:
            return -1
        k = key(obj)
        if k not in self.clip_idx:
            self.clip_idx[k] = len(self.clips)
            self.clips.append(obj)
        return self.clip_idx[k]

    def audio_id(self, obj):
        if obj is None:
            return -1
        k = key(obj)
        if k not in self.audio_idx:
            self.audio_idx[k] = len(self.audio)
            self.audio.append(obj)
        return self.audio_idx[k]

    # -- nodos --
    def add_tree(self, go, parent=-1):
        idx = len(self.nodes)
        n = {'go': go, 'parent': parent, 'children': []}
        self.nodes.append(n)
        self.objmap[key(go['obj'])] = idx
        for c in go['comps']:
            self.objmap[key(c['obj'])] = idx
        for ch in go['children']:
            n['children'].append(self.add_tree(ch, idx))
        return idx

    def ref(self, obj, ptr):
        """Indice del nodo al que apunta un PPtr (a GameObject o a componente)."""
        if ptr is None or ptr[2] == 0:
            return -1
        o = self.sc.resolve_ptr(obj, ptr)
        if o is None:
            return -1
        return self.objmap.get(key(o), -1)

    def comps_of(self, n):
        go = n['go']
        out = []
        classes = {c.get('class') for c in go['comps'] if c['type'] == 'MonoBehaviour'}
        for c in go['comps']:
            t = c['type']
            o = c['obj']
            d = c.get('data')
            if t == 'MeshFilter' and d:
                mesh = self.sc.resolve(o.assets_file, d['m_Mesh'][1], d['m_Mesh'][2])
                rend = next((x for x in go['comps'] if x['type'] == 'MeshRenderer'), None)
                if mesh is None or rend is None or not rend.get('data'):
                    continue
                rd = rend['data']
                mats = [self.mat_id(self.sc.resolve(rend['obj'].assets_file, p[1], p[2])) for p in rd['m_Materials']]
                subsets = list(rd.get('m_SubsetIndices') or [])
                sbroot = self.ref(rend['obj'], rd.get('m_StaticBatchRoot'))
                words = [self.mesh_id(mesh), 1 if rd['m_Enabled'] else 0, len(mats)] + [m & 0xffffffff for m in mats]
                words += [len(subsets)] + subsets + [sbroot & 0xffffffff]
                out.append((C_MESH, words))
            elif t == 'SkinnedMeshRenderer' and d:
                mesh = self.sc.resolve(o.assets_file, d['m_Mesh'][1], d['m_Mesh'][2])
                mats = [self.mat_id(self.sc.resolve(o.assets_file, p[1], p[2])) for p in d['m_Materials']]
                bones = [self.ref(o, b) for b in d['m_Bones']]
                words = [self.mesh_id(mesh, world=False), 1 if d['m_Enabled'] else 0, len(mats)] + \
                    [m & 0xffffffff for m in mats] + [len(bones)] + [b & 0xffffffff for b in bones]
                out.append((C_SKIN, words))
            elif t == 'BoxCollider' and d:
                c_, s_ = d['m_Center'], d['m_Size']
                out.append((C_BOX, [int(bool(d['m_IsTrigger'])), int(bool(d['m_Enabled']))] +
                            [f2u(c_['x']), f2u(c_['y']), f2u(c_['z']), f2u(s_['x']), f2u(s_['y']), f2u(s_['z'])]))
            elif t == 'SphereCollider' and d:
                c_ = d['m_Center']
                out.append((C_SPHERE, [int(bool(d['m_IsTrigger'])), int(bool(d['m_Enabled'])),
                                       f2u(c_['x']), f2u(c_['y']), f2u(c_['z']), f2u(d['m_Radius'])]))
            elif t == 'CapsuleCollider' and d:
                c_ = d['m_Center']
                out.append((C_CAPSULE, [int(bool(d['m_IsTrigger'])), int(bool(d['m_Enabled'])),
                                        f2u(c_['x']), f2u(c_['y']), f2u(c_['z']), f2u(d['m_Radius']),
                                        f2u(d['m_Height']), d['m_Direction']]))
            elif t == 'CharacterController' and d:
                c_ = d['m_Center']
                out.append((C_CHARCTRL, [f2u(d['m_Height']), f2u(d['m_Radius']), f2u(d['m_SlopeLimit']),
                                         f2u(d['m_StepOffset']), f2u(d['m_SkinWidth']),
                                         f2u(c_['x']), f2u(c_['y']), f2u(c_['z'])]))
            elif t == 'Camera' and d:
                bg = d['m_BackGroundColor']
                out.append((C_CAMERA, [f2u(d['near clip plane']), f2u(d['far clip plane']), f2u(d['field of view']),
                                       f2u(bg['r']), f2u(bg['g']), f2u(bg['b'])]))
            elif t == 'Animation' and d:
                clips = [self.clip_id(self.sc.resolve(o.assets_file, p[1], p[2])) for p in d['m_Animations']]
                clips += self.extra_anim.get(go['name'], [])
                defc = self.clip_id(self.sc.resolve(o.assets_file, d['m_Animation'][1], d['m_Animation'][2]))
                out.append((C_ANIMATION, [defc & 0xffffffff, int(bool(d['m_PlayAutomatically'])), d['m_WrapMode'],
                                          len(clips)] + [c & 0xffffffff for c in clips]))
            elif t == 'MonoBehaviour':
                cl, f = c.get('class'), c.get('fields')
                if f is None:
                    continue
                if cl == 'TrackChunk':
                    cps = f.get('CheckPoints') or []
                    w = [f2u(f['zSize']), f['probability'], f2u(f['zMinimum']), int(bool(f['zMaximumActive'])),
                         f2u(f['zMaximum']), int(bool(f['isTutorial'])), len(cps)]
                    for cp in cps:
                        w += [cp['TrackNumber'], f2u(cp['Z'])]
                    out.append((C_TRACKCHUNK, w))
                elif cl in ('Randomizer',):
                    out.append((C_RANDOMIZER, []))
                elif cl == 'RandomizerTarget':
                    tg = [self.ref(o, p) for p in f.get('Targets') or []]
                    out.append((C_RANDTARGET, [len(tg)] + [x & 0xffffffff for x in tg]))
                elif cl in ('Group', 'SuperSneakersGroup'):
                    out.append((C_SSGROUP if cl == 'SuperSneakersGroup' else C_GROUP, []))
                elif cl == 'SpawnPoint':
                    ks = ['dailyLetter', 'doubleScoreMultiplier', 'jetpackPickup', 'jumpBooster', 'magnetBooster',
                          'mysteryBox']
                    out.append((C_SPAWNPOINT, [self.ref(o, f[k_]) & 0xffffffff for k_ in ks]))
                elif cl == 'RandomizeOffset':
                    r = f['randomOffsets']
                    out.append((C_RANDOFFSET, [int(bool(r['left'])), int(bool(r['mid'])), int(bool(r['right']))]))
                elif cl == 'Mirror':
                    out.append((C_MIRROR, []))
                elif cl == 'MovingTrain':
                    out.append((C_MOVINGTRAIN, [f2u(f['speed']), f2u(f['trainCount'])]))
                elif cl == 'MovingCoin':
                    out.append((C_MOVINGCOIN, [f2u(f['speed'])]))
                elif cl == 'CoinPlaceholder':
                    out.append((C_COINPLACE, []))
                elif cl == 'CoinLine':
                    out.append((C_COINLINE, [f2u(f['length']), f2u(f['coinSpacing'])]))
                elif cl == 'CoinJumpCurve':
                    out.append((C_COINJUMP, [f2u(f['speed']), f2u(f['curveOffset']), f2u(f['coinSpacing']),
                                             f2u(f['beginRatio']), f2u(f['endRatio']), int(bool(f['superSneakers']))]))
                elif cl == 'Pickup':
                    kind = -1
                    for k_, v in PICKUP_KIND.items():
                        if k_ in classes:
                            kind = v
                    out.append((C_PICKUP, [kind & 0xffffffff, int(bool(f.get('CanBeSpawned', 1)))]))
                elif cl == 'PickupRotate':
                    out.append((C_PICKUPROTATE, [self.ref(o, f['target']) & 0xffffffff, f2u(f['speed']),
                                                 f2u(f['rotatePhase'])]))
                elif cl == 'PickupDefault':
                    out.append((C_PICKUPDEFAULT, [self.ref(o, f['meshRenderer']) & 0xffffffff,
                                                  self.ref(o, f['glow']) & 0xffffffff,
                                                  int(bool(f['ShouldSpawnParticles']))]))
                elif cl == 'DailyLetterPickup':
                    letters = [self.mesh_id(self.sc.resolve_ptr(o, p)) for p in f.get('Letters') or []]
                    out.append((C_DAILYLETTER, [self.ref(o, f['meshRenderer']) & 0xffffffff,
                                                self.ref(o, f['glow']) & 0xffffffff,
                                                self.ref(o, f['LetterMesh']) & 0xffffffff, len(letters)] +
                                [x & 0xffffffff for x in letters]))
                elif cl == 'Tunnel':
                    out.append((C_TUNNEL, []))
                elif cl == 'TrackObject':
                    out.append((C_TRACKOBJECT, []))
                elif cl == 'RendererActivator':
                    out.append((C_RENDACT, []))
                elif cl == 'SnapObject':
                    out.append((C_SNAP, []))
                elif cl == 'Glow':
                    out.append((C_GLOW, []))
                elif cl == 'Coin':
                    out.append((C_COIN, []))
                elif cl == 'VisibleObject':
                    out.append((C_VISIBLE, []))
                elif cl == 'OnTriggerObject':
                    out.append((C_ONTRIGGER, []))
                elif cl == 'TutorialEvent':
                    txt = self.strings.get(f.get('text') or '')
                    out.append((C_TUTORIAL, [int(bool(f['displayText'])), txt, int(bool(f['displayMesh'])),
                                             self.ref(o, f['mesh']) & 0xffffffff, f2u(f['direction']),
                                             f2u(f['time']), int(bool(f['endTutorial'])),
                                             int(bool(f['allowHoverboard']))]))
                elif cl == 'CurvePoint':
                    out.append((C_CURVEPOINT, [f2u(f['t']), int(bool(f['smoothTangents'])),
                                               self.ref(o, f['customIn']) & 0xffffffff,
                                               self.ref(o, f['customOut']) & 0xffffffff, f2u(f['weight'])]))
                elif cl == 'JetPackCloud':
                    out.append((C_JETCLOUD, []))
        return out

    def build_world(self):
        blob = []
        recs = []
        for i, n in enumerate(self.nodes):
            go = n['go']
            td = go['tr']['data']
            p, r, s = td['m_LocalPosition'], td['m_LocalRotation'], td['m_LocalScale']
            first = n['children'][0] if n['children'] else -1
            nxt = -1
            if n['parent'] >= 0:
                sib = self.nodes[n['parent']]['children']
                j = sib.index(i)
                if j + 1 < len(sib):
                    nxt = sib[j + 1]
            comps = self.comps_of(n)
            off = len(blob)
            for t, w in comps:
                blob += [t, len(w)] + list(w)
            recs.append(struct.pack('<iii3f4f3fHBBBBHI', n['parent'], first, nxt,
                                    p['x'], p['y'], p['z'], r['x'], r['y'], r['z'], r['w'], s['x'], s['y'], s['z'],
                                    self.strings.get(go['name']), go['layer'], TAG_IDS.get(go['tag'], 0),
                                    1 if go['active'] else 0, len(comps), 0, off))
        out = bytearray(b'SSW1')
        out += struct.pack('<II', len(recs), len(blob))
        for r_ in recs:
            out += r_
        out += struct.pack('<%dI' % len(blob), *[x & 0xffffffff for x in blob])
        out += self.strings.blob()
        return bytes(out)


# ---------------------------------------------------------------- mallas
def subdivide(V, UV, tris, max_edge):
    V = [tuple(v) for v in V]
    UV = [tuple(u) for u in UV]
    cache = {}

    def mid(a, b):
        k = (min(a, b), max(a, b))
        if k not in cache:
            cache[k] = len(V)
            V.append(tuple((V[a][i] + V[b][i]) * 0.5 for i in range(3)))
            UV.append(tuple((UV[a][i] + UV[b][i]) * 0.5 for i in range(2)))
        return cache[k]

    def l2(a, b):
        return sum((V[a][i] - V[b][i]) ** 2 for i in range(3))

    out = []
    stack = list(tris)
    m2 = max_edge * max_edge
    guard = 0
    while stack:
        t = stack.pop()
        a, b, c = t
        e = [(l2(a, b), 0), (l2(b, c), 1), (l2(c, a), 2)]
        e.sort(reverse=True)
        if e[0][0] <= m2 or guard > 400000:
            out.append(t)
            continue
        guard += 1
        w = e[0][1]
        if w == 0:
            m = mid(a, b); stack += [(a, m, c), (m, b, c)]
        elif w == 1:
            m = mid(b, c); stack += [(a, b, m), (a, m, c)]
        else:
            m = mid(c, a); stack += [(a, b, m), (m, b, c)]
    return V, UV, out


CLUSTER = 64.0       # tamano de celda para trocear el escenario (culling por trozos); la curvatura es exacta en el GE


def mesh_triangles(mo, h):
    """Triangulos por submalla. En Unity 3.5 cada submalla dice si es tira (isTriStrip != 0) o lista."""
    tt = mo.read_typetree()
    ib = list(h.m_IndexBuffer or [])
    use16 = getattr(h, 'm_Use16BitIndices', True)
    out = []
    for sm in tt['m_SubMeshes']:
        first = sm['firstByte'] // (2 if use16 else 4)
        cnt = sm['indexCount']
        idx = ib[first:first + cnt]
        tris = []
        if sm.get('isTriStrip', 1) == 0:
            for i in range(0, len(idx) - 2, 3):
                tris.append((idx[i], idx[i + 1], idx[i + 2]))
        else:
            for i in range(len(idx) - 2):
                a, b, c = idx[i], idx[i + 1], idx[i + 2]
                if a == b or a == c or b == c:
                    continue
                tris.append((b, a, c) if i & 1 else (a, b, c))
        out.append(tris)
    return out


def make_parts(V, UV, subs, skin, world):
    """Divide la malla en trozos (submalla + celda espacial) con sus propios vertices."""
    parts = []
    for si, sm in enumerate(subs):
        if not sm:
            continue
        if world and not skin:
            cells = {}
            for t in sm:
                cx = sum(V[i][0] for i in t) / 3.0
                cy = sum(V[i][1] for i in t) / 3.0
                cz = sum(V[i][2] for i in t) / 3.0
                k = (int(math.floor(cx / CLUSTER)), int(math.floor(cy / CLUSTER)), int(math.floor(cz / CLUSTER)))
                cells.setdefault(k, []).append(t)
            groups = list(cells.values())
        else:
            groups = [sm]
        for g in groups:
            remap = {}
            idx = []
            for t in g:
                for i in t:
                    if i not in remap:
                        remap[i] = len(remap)
                    idx.append(remap[i])
            order = sorted(remap, key=remap.get)
            parts.append((si, order, idx))
    return parts


def export_meshes(ex):
    out = bytearray(b'SSM3')
    out += struct.pack('<I', len(ex.meshes))
    body = bytearray()
    offs = []
    stats = []
    for mi, mo in enumerate(ex.meshes):
        d = mo.read()
        h = MeshHandler(d)
        h.process()
        V = list(h.m_Vertices or [])
        UV = list(h.m_UV0 or [(0.0, 0.0)] * len(V))
        subs = mesh_triangles(mo, h)
        skin = bool(h.m_BoneWeights) and len(h.m_BoneWeights) == len(V)
        if MAX_EDGE > 0 and ex.mesh_world[mi] and not skin:
            nsubs = []
            for sm in subs:
                V, UV, sm2 = subdivide(V, UV, sm, MAX_EDGE)
                nsubs.append(sm2)
            subs = nsubs
        Va = np.array(V, dtype=np.float32).reshape(-1, 3) if V else np.zeros((1, 3), np.float32)
        mn, mx = Va.min(axis=0), Va.max(axis=0)
        ctr = (mn + mx) * 0.5
        rad = float(np.sqrt(((Va - ctr) ** 2).sum(axis=1)).max())
        world = ex.mesh_world[mi] and rad > CLUSTER * 0.75
        parts = make_parts(V, UV, subs, skin, world)
        offs.append(len(body))
        nm = d.m_Name.encode()[:31]
        body += struct.pack('<B', len(nm)) + nm.ljust(31, b'\0')
        body += struct.pack('<HHBBH', len(parts), len(subs), 1 if skin else 0, 0, 0)
        body += struct.pack('<4f6f', ctr[0], ctr[1], ctr[2], rad, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2])
        # pesos para la curvatura exacta en el GE (skinning con 4 "huesos"): 1 y la posicion normalizada a la caja
        # del modelo (0..1 -> 0..32768). Los vertices repetidos en varios trozos tienen los mismos pesos: sin juntas.
        ext = np.where(mx > mn, mx - mn, 1.0).astype(np.float32)
        Q = np.clip(np.rint((Va - mn) / ext * 32768.0), 0, 32768).astype(np.int64)
        tv = 0
        for si, order, idx in parts:
            P = Va[order]
            pmn, pmx = P.min(axis=0), P.max(axis=0)
            pc = (pmn + pmx) * 0.5
            pr = float(np.sqrt(((P - pc) ** 2).sum(axis=1)).max())
            body += struct.pack('<HHI4f', si, len(order), len(idx), pc[0], pc[1], pc[2], pr)
            for i in order:
                body += struct.pack('<4H5f', 32768, Q[i][0], Q[i][1], Q[i][2], UV[i][0], 1.0 - UV[i][1], V[i][0], V[i][1], V[i][2])
            body += struct.pack('<%dH' % len(idx), *idx)
            if len(idx) % 2:
                body += b'\0\0'
            if skin:
                bw, bi = h.m_BoneWeights, h.m_BoneIndices
                for i in order:
                    body += struct.pack('<4f4B', *[float(x) for x in bw[i]], *[int(x) for x in bi[i]])
            tv += len(order)
        if skin:
            bp = d.m_BindPose
            # lotes para el skinning por hardware del GE (como mucho 8 huesos por lote)
            bw, bi = h.m_BoneWeights, h.m_BoneIndices
            batches = []
            for si, sm in enumerate(subs):
                cur, bones = [], []
                for t in sm:
                    tb = set()
                    for i in t:
                        for k in range(4):
                            if bw[i][k] > 0.0001:
                                tb.add(int(bi[i][k]))
                    nb = sorted(set(bones) | tb)
                    if len(nb) > 8 and cur:
                        batches.append((si, bones, cur))
                        cur, bones = [], []
                        nb = sorted(tb)
                    bones = nb
                    cur.append(t)
                if cur:
                    batches.append((si, bones, cur))
            body += struct.pack('<I', len(batches))
            for si, bones, tris in batches:
                remap = {}
                idx = []
                for t in tris:
                    for i in t:
                        if i not in remap:
                            remap[i] = len(remap)
                        idx.append(remap[i])
                order = sorted(remap, key=remap.get)
                nb = len(bones)
                body += struct.pack('<HHHH', si, nb, len(order), 0) + struct.pack('<I', len(idx))
                body += struct.pack('<8B', *(bones + [0] * (8 - nb)))
                for i in order:
                    w = [0.0] * nb
                    for k in range(4):
                        if bw[i][k] > 0.0001:
                            w[bones.index(int(bi[i][k]))] += float(bw[i][k])
                    body += struct.pack('<%df' % nb, *w)
                    body += struct.pack('<5f', UV[i][0], 1.0 - UV[i][1], V[i][0], V[i][1], V[i][2])
                body += struct.pack('<%dH' % len(idx), *idx)
                if len(idx) % 2:
                    body += b'\0\0'
            body += struct.pack('<I', len(bp))
            for m in bp:
                body += struct.pack('<16f', *[m.e00, m.e10, m.e20, m.e30, m.e01, m.e11, m.e21, m.e31,
                                              m.e02, m.e12, m.e22, m.e32, m.e03, m.e13, m.e23, m.e33])
        else:
            body += struct.pack('<I', 0)
        while len(body) % 4:
            body += b'\0'
        stats.append((d.m_Name, tv, sum(len(p[2]) for p in parts) // 3, len(parts)))
    for o in offs:
        out += struct.pack('<I', o + 8 + 4 * len(offs))
    out += body
    return bytes(out), stats


# ---------------------------------------------------------------- texturas
def swizzle(data, width_bytes, height):
    """Swizzle de la PSP: bloques de 16 bytes x 8 filas."""
    src = np.frombuffer(data, dtype=np.uint8).reshape(height, width_bytes)
    bx = width_bytes // 16
    by = height // 8
    t = src.reshape(by, 8, bx, 16).transpose(0, 2, 1, 3)
    return t.tobytes()


TEX_T8, TEX_5650, TEX_4444, TEX_8888 = 0, 1, 2, 3


def tex_rgba(t):
    """Imagen RGBA de una Texture2D. Las "ARGB4444" de este APK (formato 2: la estela del jetpack, jetpackSmoke) estan
    guardadas en realidad como RGBA4444 y UnityPy les cambia el orden de los canales: el arcoiris del original salia
    azul, rojo y morado."""
    img = t.image.convert('RGBA')
    if int(t.m_TextureFormat) == 2:
        img = Image.fromarray(np.ascontiguousarray(np.asarray(img)[:, :, [3, 0, 1, 2]]), 'RGBA')
    return img


def export_textures(ex, maxsize=512, hq=()):
    out = bytearray(b'SST1')
    out += struct.pack('<I', len(ex.textures))
    body = bytearray()
    offs = []
    for to in ex.textures:
        force_t8 = False
        if isinstance(to, tuple):          # textura generada por el conversor (fuentes pre-escaladas, Halloween)
            gen_name, gen_img = to[1], to[2]
            force_t8 = len(to) > 3 and to[3]
            t = None
            img = gen_img.convert('RGBA')
        else:
            t = to.read()
            img = tex_rgba(t)
        w, h = img.size
        nw = 1 << max(0, (w - 1).bit_length())
        nh = 1 << max(0, (h - 1).bit_length())
        while nw > maxsize or nh > maxsize:     # misma escala en los dos ejes
            nw, nh = max(1, nw // 2), max(1, nh // 2)
        nw, nh = max(nw, 16), max(nh, 8)
        if (nw, nh) != (w, h):
            img = img.resize((nw, nh), Image.LANCZOS)
        a = np.asarray(img)
        has_alpha = bool((a[:, :, 3] < 250).any())
        name = t.m_Name if t is not None else gen_name
        if name in hq or (t is None and not force_t8):
            if has_alpha:
                fmt = TEX_4444
                r = (a[:, :, 0].astype(np.uint16) >> 4)
                g = (a[:, :, 1].astype(np.uint16) >> 4) << 4
                b = (a[:, :, 2].astype(np.uint16) >> 4) << 8
                al = (a[:, :, 3].astype(np.uint16) >> 4) << 12
                pix = (r | g | b | al).astype('<u2').tobytes()
            else:
                fmt = TEX_5650
                r = (a[:, :, 0].astype(np.uint16) >> 3)
                g = (a[:, :, 1].astype(np.uint16) >> 2) << 5
                b = (a[:, :, 2].astype(np.uint16) >> 3) << 11
                pix = (r | g | b).astype('<u2').tobytes()
            data = swizzle(pix, nw * 2, nh)
            pal = b''
        else:
            fmt = TEX_T8
            if has_alpha:
                q = img.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
                pal_rgba = np.zeros((256, 4), np.uint8)
                # la paleta de FASTOCTREE con RGBA trae el alfa
                p = q.getpalette('RGBA')
                if p is None:
                    p = q.getpalette() or []
                    p = sum([p[i:i + 3] + [255] for i in range(0, len(p), 3)], [])
                p = np.array(p[:1024], np.uint8).reshape(-1, 4)
                pal_rgba[:len(p)] = p
            else:
                q = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
                p = np.array(q.getpalette()[:768], np.uint8).reshape(-1, 3)
                pal_rgba = np.zeros((256, 4), np.uint8)
                pal_rgba[:len(p), :3] = p
                pal_rgba[:, 3] = 255
            idx = np.asarray(q, dtype=np.uint8)
            data = swizzle(idx.tobytes(), nw, nh)
            pal = pal_rgba.tobytes()
        offs.append(len(body))
        body += struct.pack('<B', min(len(name.encode()), 31)) + name.encode()[:31].ljust(31, b'\0')
        body += struct.pack('<HHHH', nw, nh, fmt, 1 if has_alpha else 0)
        body += struct.pack('<II', len(pal), len(data))
        body += pal + data
        while len(body) % 16:
            body += b'\0'
        print('  textura %-28s %4dx%-4d -> %3dx%-3d %s' % (name, w, h, nw, nh, ['T8', '5650', '4444', '8888'][fmt]))
    for o in offs:
        out += struct.pack('<I', o)
    # cabecera: magic, n, offsets; los datos empiezan alineados a 16
    hdr_len = len(out)
    pad = (16 - hdr_len % 16) % 16
    res = bytearray(out)
    res += b'\0' * pad
    base = len(res)
    for i in range(len(offs)):
        struct.pack_into('<I', res, 8 + 4 * i, offs[i] + base)
    res += body
    return bytes(res)


# ---------------------------------------------------------------- animaciones
def hermite(k0, k1, t, comp=None):
    t0, t1 = k0['time'], k1['time']
    dt = t1 - t0
    if dt <= 0:
        return k0['value'] if comp is None else k0['value'][comp]

    def g(k, f):
        v = k[f]
        return v if comp is None else v[comp]
    p0, p1 = g(k0, 'value'), g(k1, 'value')
    m0, m1 = g(k0, 'outSlope'), g(k1, 'inSlope')
    if math.isinf(m0) or math.isinf(m1) or math.isnan(m0) or math.isnan(m1):
        return p0
    s = (t - t0) / dt
    s2, s3 = s * s, s * s * s
    return (2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * m0 * dt + (-2 * s3 + 3 * s2) * p1 + (s3 - s2) * m1 * dt


def eval_curve(keys, t, comp=None):
    if not keys:
        return 0.0
    if t <= keys[0]['time']:
        v = keys[0]['value']
        return v if comp is None else v[comp]
    if t >= keys[-1]['time']:
        v = keys[-1]['value']
        return v if comp is None else v[comp]
    lo, hi = 0, len(keys) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if keys[mid]['time'] <= t:
            lo = mid
        else:
            hi = mid
    return hermite(keys[lo], keys[hi], t, comp)


def export_anims(ex):
    out = bytearray(b'SSA1')
    out += struct.pack('<I', len(ex.clips))
    body = bytearray()
    offs = []
    for co in ex.clips:
        pose = isinstance(co, tuple)     # ('pose', clip, nombre): solo el primer fotograma, quieto (ClampForever)
        d = (co[1] if pose else co).read_typetree()
        tracks = {}
        length = 0.0
        for kind, lst in (('rot', d['m_RotationCurves']), ('pos', d['m_PositionCurves']), ('scl', d['m_ScaleCurves'])):
            for c in lst:
                keys = c['curve']['m_Curve']
                if not keys:
                    continue
                length = max(length, keys[-1]['time'])
                tracks.setdefault(c['path'], {})[kind] = keys
        if pose:
            length = 0.0
            tracks = {k: v for k, v in tracks.items() if k.startswith('gSkeleton_game')}   # sin la tabla
        nfr = max(1, int(math.ceil(length * ANIM_FPS - 1e-4)) + 1)
        offs.append(len(body))
        name = co[2] if pose else d['m_Name']
        evs = [] if pose else d.get('m_Events') or []
        body += struct.pack('<B', min(len(name.encode()), 47)) + name.encode()[:47].ljust(47, b'\0')
        body += struct.pack('<fIIIIf', length, 8 if pose else d['m_WrapMode'], nfr, len(tracks), len(evs),
                            d.get('m_SampleRate', 30.0))
        for e in evs:
            fn = e['functionName'].encode()[:31]
            body += struct.pack('<f', e['time']) + struct.pack('<B', len(fn)) + fn.ljust(31, b'\0')
        for path, tr in tracks.items():
            pb = ex.strings.get(path)
            flags = (1 if 'pos' in tr else 0) | (2 if 'rot' in tr else 0) | (4 if 'scl' in tr else 0)
            body += struct.pack('<HH', pb, flags)
            for f in range(nfr):
                t = min(f / ANIM_FPS, length)
                if 'pos' in tr:
                    body += struct.pack('<3f', *[eval_curve(tr['pos'], t, c_) for c_ in 'xyz'])
                if 'rot' in tr:
                    q = [eval_curve(tr['rot'], t, c_) for c_ in 'xyzw']
                    n = math.sqrt(sum(x * x for x in q)) or 1.0
                    body += struct.pack('<4f', *[x / n for x in q])
                if 'scl' in tr:
                    body += struct.pack('<3f', *[eval_curve(tr['scl'], t, c_) for c_ in 'xyz'])
    for o in offs:
        out += struct.pack('<I', o + 8 + 4 * len(offs))
    out += body
    return bytes(out)


# ---------------------------------------------------------------- audio
def decode_aiff(b):
    if b[:4] != b'FORM':
        return None
    pos = 12
    ch = bits = rate = None
    data = None
    while pos + 8 <= len(b):
        cid = b[pos:pos + 4]
        sz = struct.unpack('>I', b[pos + 4:pos + 8])[0]
        cd = b[pos + 8:pos + 8 + sz]
        if cid == b'COMM':
            ch, _, bits = struct.unpack('>hIh', cd[:8])
            e = struct.unpack('>H', cd[8:10])[0]
            m = struct.unpack('>Q', cd[10:18])[0]
            rate = m * 2.0 ** (e - 16383 - 63)
        elif cid == b'SSND':
            off = struct.unpack('>I', cd[:4])[0]
            data = cd[8 + off:]
        pos += 8 + sz + (sz & 1)
    if data is None or bits != 16:
        return None
    a = np.frombuffer(data[:len(data) // 2 * 2], dtype='>i2').astype(np.float32)
    a = a.reshape(-1, ch).mean(axis=1)
    return a, rate


def resample(a, rate, new):
    if abs(rate - new) < 1 or len(a) == 0:
        return a
    n = int(len(a) * new / rate)
    x = np.linspace(0, len(a) - 1, n)
    return np.interp(x, np.arange(len(a)), a)


def write_wav(path, a, rate):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(rate))
        w.writeframes(np.clip(a, -32768, 32767).astype('<i2').tobytes())


def export_audio(ex, outdir):
    os.makedirs(os.path.join(outdir, 'music'), exist_ok=True)
    os.makedirs(os.path.join(outdir, 'wav'), exist_ok=True)
    names = []
    blobs = []
    for ao in ex.audio:
        d = ao.read()
        raw = bytes(d.m_AudioData or b'')
        name = d.m_Name
        names.append(name)
        if raw[:4] == b'FORM':
            r = decode_aiff(raw)
            if r is None:
                blobs.append(b'')
                continue
            a, rate = r
            write_wav(os.path.join(outdir, 'wav', name + '.wav'), a, rate)
            pcm = resample(a, rate, 22050)
            blobs.append(np.clip(pcm, -32768, 32767).astype('<i2').tobytes())
        elif raw[:4] == b'RIFF':
            with open(os.path.join(outdir, 'wav', name + '.wav'), 'wb') as f:
                f.write(raw)
            with wave.open(io.BytesIO(raw)) as w:
                fr = w.readframes(w.getnframes())
                a = np.frombuffer(fr, '<i2').astype(np.float32).reshape(-1, w.getnchannels()).mean(axis=1)
                rate = w.getframerate()
            blobs.append(np.clip(resample(a, rate, 22050), -32768, 32767).astype('<i2').tobytes())
        else:
            # MP3 (musica y algun efecto): se guarda tal cual
            with open(os.path.join(outdir, 'music', name + '.mp3'), 'wb') as f:
                f.write(raw)
            blobs.append(b'')
    out = bytearray(b'SSS1')
    out += struct.pack('<I', len(names))
    off = 8 + len(names) * 40
    for n, b in zip(names, blobs):
        nb = n.encode()[:31]
        out += struct.pack('<B', len(nb)) + nb.ljust(31, b'\0') + struct.pack('<II', off, len(b) // 2)
        off += len(b)
        off += (4 - off % 4) % 4
    for b in blobs:
        out += b
        while len(out) % 4:
            out += b'\0'
    return bytes(out), names



# ---------------------------------------------------------------- interfaz (NGUI)
def ui_sprites_used():
    """Nombres de sprite que usa el port: las cadenas entre comillas de src/*.c (los que no existen se ignoran)."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')
    names = set()
    for fn in os.listdir(src):
        if fn.endswith('.c'):
            names |= set(re.findall(r'"([A-Za-z][A-Za-z0-9_]*)"', open(os.path.join(src, fn), encoding='utf-8', errors='ignore').read()))
    return names


def read_atlas(sc, name):
    """(imagen, sprites) del UIAtlas con ese nombre que tiene textura propia."""
    for f in sc.files.values():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            cl, fl = sc.read_mono(o)
            if cl != 'UIAtlas' or not fl or not fl['material'][2]:
                continue
            go = sc.resolve_ptr(o, fl['m_GameObject'])
            if go is None or go.read().m_Name != name:
                continue
            d = sc.resolve_ptr(o, fl['material']).read_typetree()
            mat = sc.resolve_ptr(o, fl['material'])
            for _, t in d['m_SavedProperties']['m_TexEnvs']:
                tex = sc.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                if tex is not None:
                    return tex.read().image.convert('RGBA'), {sp['name']: sp for sp in fl['sprites']}
    return None, {}


def build_psp_atlas(sc, sc14):
    """Interfaz de la 1.4 en el mismo espacio que la de la 1.0: el LowResAtlas de la 1.4 mide 1024x512 (la PSP no
    admite mas de 512), asi que se reempaquetan en una hoja de 512x512 solo los sprites que usa el port, sacados de
    la 1.4 (o de la 1.0 si la 1.4 no los tiene). Devuelve (imagen, sprites) o None si no caben."""
    img14, sp14 = read_atlas(sc14, 'LowResAtlas')
    img10, sp10 = read_atlas(sc, 'LowResAtlas')
    if img14 is None:
        return None
    used = ui_sprites_used()
    items = []
    for n in sorted(used):
        src = (img14, sp14[n]) if n in sp14 else (img10, sp10[n]) if n in sp10 else None
        if src is None:
            continue
        o = src[1]['outer']
        items.append((n, src[0], src[1], int(round(o['width'])), int(round(o['height']))))
    # estantes, de mas alto a mas bajo, 2 pixeles de separacion
    W = H = 512
    items.sort(key=lambda it: (-it[4], -it[3]))
    x = y = shelf = 0
    place = {}
    for n, im, sp, w, h in items:
        if x + w > W:
            x, y, shelf = 0, y + shelf + 2, 0
        if y + h > H:
            print('  AVISO: los sprites de la 1.4 no caben en 512x512: se usa la interfaz de la 1.0')
            return None
        place[n] = (x, y)
        x += w + 2
        shelf = max(shelf, h)
    sheet = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    sprites = []
    for n, im, sp, w, h in items:
        o, i = sp['outer'], sp['inner']
        px, py = place[n]
        sheet.paste(im.crop((int(o['x']), int(o['y']), int(o['x']) + w, int(o['y']) + h)), (px, py))
        dx, dy = px - o['x'], py - o['y']
        sprites.append({'name': n, 'outer': {'x': px, 'y': py, 'width': w, 'height': h},
                        'inner': {'x': i['x'] + dx, 'y': i['y'] + dy, 'width': i['width'], 'height': i['height']}})
    n14 = sum(1 for it in items if it[0] in sp14)
    print('  interfaz de la 1.4: %d sprites (%d de la 1.4, %d de la 1.0) en 512x512, hasta y=%d' %
          (len(items), n14, len(items) - n14, y + shelf))
    return sheet, sprites


def ui_backgrounds(sc14):
    """Fondo desenfocado de las pantallas de la 1.4 (BackgroundHelper): normal y Halloween, recortados a apaisado."""
    for f in sc14.files.values():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            cl, fl = sc14.read_mono(o)
            if cl != 'BackgroundHelper' or not fl:
                continue
            res = []
            for k in ('normalTex', 'halloweenTex'):
                t = sc14.resolve_ptr(o, fl[k])
                if t is None:
                    return None
                im = t.read().image.convert('RGB')
                w, h = im.size
                ch = int(w * 272 / 480)          # franja apaisada de la imagen vertical (tren y grafiti)
                y0 = int(h * 0.30)
                res.append(im.crop((0, y0, w, y0 + ch)).resize((256, 128), Image.LANCZOS))
            return res
    return None


def char_previews(sc14):
    """CharacterModelPreviewFactory de la 1.4: por personaje la malla, la textura y su clip de reposo (respirando)."""
    for f in sc14.files.values():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            cl, fl = sc14.read_mono(o)
            if cl != 'CharacterModelPreviewFactory' or not fl:
                continue
            pre = sc14.resolve_ptr(o, fl['characterModelPreviewPrefab'])
            out = []
            for c in fl['characters']:
                mat, mesh = sc14.resolve_ptr(o, c['material']), sc14.resolve_ptr(o, c['mesh'])
                clip = sc14.resolve_ptr(o, c['animationClip'])
                if mat is None or mesh is None or clip is None:
                    continue
                tex = None
                for nm, t in mat.read_typetree()['m_SavedProperties']['m_TexEnvs']:
                    tex = sc14.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                    if tex is not None:
                        break
                if tex is not None:
                    out.append({'name': c['name'], 'mesh': mesh, 'tex': tex, 'clip': clip})
            return sc14, pre, out
    return None


def quat_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def clip_pose(clip, t=0.0):
    """Ruta -> (pos, rot, scl) del clip en el instante t (lo que falte queda en None)."""
    d = clip.read_typetree()
    pose = {}
    for kind, lst in (('pos', d['m_PositionCurves']), ('rot', d['m_RotationCurves']), ('scl', d['m_ScaleCurves'])):
        for c in lst:
            keys = c['curve']['m_Curve']
            if keys:
                comps = 'xyzw' if kind == 'rot' else 'xyz'
                pose.setdefault(c['path'], {})[kind] = [eval_curve(keys, t, c_) for c_ in comps]
    return pose


# miniaturas del carrusel de personajes: celdas de THUMB_W x THUMB_H en una textura de 256x256 (64 KB en T8) que el
# juego solo carga en la pantalla de personajes
THUMB_W, THUMB_H, THUMB_SS = 42, 64, 4


def render_char_thumbs(prev):
    """Dibuja cada personaje de frente, quieto en el primer fotograma de su clip de reposo, con skinning y
    rasterizado por software (z-buffer, textura sin luz como el shader del juego) a 4x y reducido."""
    sc14, pre, chars = prev
    nodes = []          # (padre, ruta, pos, rot, scl)
    tmap = {}
    smr = None

    def walk(g, parent, path):
        td = g['tr']['data']
        p, r, s_ = td['m_LocalPosition'], td['m_LocalRotation'], td['m_LocalScale']
        idx = len(nodes)
        nodes.append((parent, path, [p['x'], p['y'], p['z']], [r['x'], r['y'], r['z'], r['w']], [s_['x'], s_['y'], s_['z']]))
        tmap[key(g['tr']['obj'])] = idx
        for ch in g['children']:
            walk(ch, idx, (path + '/' if path else '') + ch['name'])
        return g
    root = walk(sc14.go_tree(pre), -1, '')
    stack = [root]
    while stack:
        g = stack.pop()
        stack += g['children']
        for c in g['comps']:
            if c['type'] == 'SkinnedMeshRenderer' and c.get('data'):
                smr = c
    if smr is None:
        return None
    bones = [tmap.get(key(sc14.resolve_ptr(smr['obj'], b)), 0) for b in smr['data']['m_Bones']]
    shots = []
    for ch in chars:
        pose = clip_pose(ch['clip'])
        world = []
        for parent, path, p, r, s_ in nodes:
            o = pose.get(path, {})
            p, r, s_ = o.get('pos', p), o.get('rot', r), o.get('scl', s_)
            m = np.eye(4)
            m[:3, :3] = quat_mat(np.array(r) / (np.linalg.norm(r) or 1.0)) * np.array(s_)
            m[:3, 3] = p
            world.append(world[parent] @ m if parent >= 0 else m)
        md = ch['mesh'].read()
        h = MeshHandler(md)
        h.process()
        V = np.array(h.m_Vertices, np.float64).reshape(-1, 3)
        UV = np.array(h.m_UV0, np.float64).reshape(-1, 2)
        BW = np.array(h.m_BoneWeights, np.float64).reshape(-1, 4)
        BI = np.array(h.m_BoneIndices, np.int64).reshape(-1, 4)
        BP = [np.array([[getattr(m_, 'e%d%d' % (r_, c_)) for c_ in range(4)] for r_ in range(4)]) for m_ in md.m_BindPose]
        skin = np.array([world[bones[i]] @ BP[i] for i in range(len(BP))])
        Vh = np.c_[V, np.ones(len(V))]
        S = np.zeros((len(V), 3))
        for k in range(4):
            S += BW[:, k:k + 1] * np.einsum('nij,nj->ni', skin[BI[:, k]], Vh)[:, :3]
        tris = np.array([t for sm in mesh_triangles(ch['mesh'], h) for t in sm], np.int64)
        tex = np.asarray(tex_rgba(ch['tex'].read()).convert('RGBA'))
        shots.append((ch['name'], S, UV, tris, tex))
    # misma escala para todos (Frank sigue siendo mas alto que Tricky): la del mas alto llena la celda
    hmax = max(s[1][:, 1].max() - s[1][:, 1].min() for s in shots)
    W, H = THUMB_W * THUMB_SS, THUMB_H * THUMB_SS
    scale0 = (H - 2 * THUMB_SS) / hmax
    cols = 256 // THUMB_W
    sheet = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    sprites = []
    for n, (name, S, UV, tris, tex) in enumerate(shots):
        th, tw = tex.shape[:2]
        # de frente: el personaje mira hacia +z; la camara esta delante mirando hacia -z (x al reves)
        cx = (S[:, 0].max() + S[:, 0].min()) * 0.5
        scale = min(scale0, (W - 2 * THUMB_SS) / (S[:, 0].max() - S[:, 0].min()))   # el disfraz de Yutani
        X = W * 0.5 - (S[:, 0] - cx) * scale
        Y = H - THUMB_SS - (S[:, 1] - S[:, 1].min()) * scale
        Z = -S[:, 2]
        col = np.zeros((H, W, 4), np.uint8)
        zb = np.full((H, W), np.inf)
        for a, b, c in tris:
            x0, x1 = int(max(0, np.floor(min(X[a], X[b], X[c])))), int(min(W - 1, np.ceil(max(X[a], X[b], X[c]))))
            y0, y1 = int(max(0, np.floor(min(Y[a], Y[b], Y[c])))), int(min(H - 1, np.ceil(max(Y[a], Y[b], Y[c]))))
            if x1 < x0 or y1 < y0:
                continue
            den = (Y[b] - Y[c]) * (X[a] - X[c]) + (X[c] - X[b]) * (Y[a] - Y[c])
            if abs(den) < 1e-9:
                continue
            px, py = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            l0 = ((Y[b] - Y[c]) * (px - X[c]) + (X[c] - X[b]) * (py - Y[c])) / den
            l1 = ((Y[c] - Y[a]) * (px - X[c]) + (X[a] - X[c]) * (py - Y[c])) / den
            l2 = 1 - l0 - l1
            inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not inside.any():
                continue
            z = l0 * Z[a] + l1 * Z[b] + l2 * Z[c]
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            m = inside & (z < sub)
            if not m.any():
                continue
            u = l0 * UV[a, 0] + l1 * UV[b, 0] + l2 * UV[c, 0]
            v = l0 * UV[a, 1] + l1 * UV[b, 1] + l2 * UV[c, 1]
            tx = np.clip((u % 1.0) * tw, 0, tw - 1).astype(np.int64)
            ty = np.clip((1.0 - v % 1.0) * th, 0, th - 1).astype(np.int64)
            sub[m] = z[m]
            col[y0:y1 + 1, x0:x1 + 1][m] = np.c_[tex[ty[m], tx[m], :3], np.full(m.sum(), 255)]
        # reducir con alfa premultiplicado (sin halo oscuro en el borde)
        f = col.astype(np.float64)
        f[:, :, :3] *= f[:, :, 3:4] / 255.0
        f = f.reshape(THUMB_H, THUMB_SS, THUMB_W, THUMB_SS, 4).mean(axis=(1, 3))
        a_ = f[:, :, 3:4]
        f[:, :, :3] = np.where(a_ > 0, f[:, :, :3] * 255.0 / np.maximum(a_, 1e-6), 0)
        im = Image.fromarray(np.clip(np.rint(f), 0, 255).astype(np.uint8), 'RGBA')
        x, y = (n % cols) * THUMB_W, (n // cols) * THUMB_H
        sheet.paste(im, (x, y))
        r_ = {'x': x, 'y': y, 'width': THUMB_W, 'height': THUMB_H}
        sprites.append({'name': 'thumb_' + name, 'outer': r_, 'inner': dict(r_)})
    return sheet, sprites


def fix_coin_glow(ex):
    """El brillo de las monedas (material "glow gold", Additive falloff) es un trocito de 26x26 pixeles de la hoja
    'main' de 1024x1024, pegado a otros dibujos. En la PSP la hoja va a 512 y el filtrado mezcla esos vecinos en el
    borde del plano: se veia un cuadro semitransparente con el borde de colores. Se saca a su propia textura de 32x32
    (2 KB) con el borde a negro (en Additive el negro no suma nada) y el material apunta a ella."""
    mi = next((i for i, m in enumerate(ex.materials) if m['name'] == 'glow gold'), None)
    if mi is None or ex.materials[mi]['tex'] < 0:
        return
    box = None
    for n in ex.nodes:
        comps = n['go']['comps']
        rend = next((c for c in comps if c['type'] == 'MeshRenderer' and c.get('data')), None)
        filt = next((c for c in comps if c['type'] == 'MeshFilter' and c.get('data')), None)
        if rend is None or filt is None:
            continue
        mats = [ex.sc.resolve(rend['obj'].assets_file, p_[1], p_[2]) for p_ in rend['data']['m_Materials']]
        if not any(m is not None and ex.mat_idx.get(key(m)) == mi for m in mats):
            continue
        mo = ex.sc.resolve(filt['obj'].assets_file, filt['data']['m_Mesh'][1], filt['data']['m_Mesh'][2])
        h = MeshHandler(mo.read())
        h.process()
        uv = np.array(h.m_UV0, np.float64).reshape(-1, 2)
        b = (uv[:, 0].min(), uv[:, 0].max(), uv[:, 1].min(), uv[:, 1].max())
        box = b if box is None else (min(box[0], b[0]), max(box[1], b[1]), min(box[2], b[2]), max(box[3], b[3]))
    m = ex.materials[mi]
    if box is None or m['st'] != (1.0, 1.0, 0.0, 0.0):
        return
    u0, u1, v0, v1 = box
    src = ex.textures[m['tex']]
    img = tex_rgba(src.read()).convert('RGB')
    W, H = img.size
    crop = img.crop((u0 * W, (1 - v1) * H, u1 * W, (1 - v0) * H)).resize((32, 32), Image.BICUBIC)
    a = np.asarray(crop).astype(np.float64)
    yy, xx = np.mgrid[0:32, 0:32]
    r = np.maximum(np.abs(xx - 15.5), np.abs(yy - 15.5)) / 16.0
    a *= np.clip((0.97 - r) / 0.15, 0.0, 1.0)[:, :, None]
    m['tex'] = len(ex.textures)
    ex.textures.append(('generada', 'coin_glow', Image.fromarray(np.clip(np.rint(a), 0, 255).astype(np.uint8), 'RGB')))
    sx, sy = 1.0 / (u1 - u0), 1.0 / (v1 - v0)
    m['st'] = (sx, sy, -u0 * sx, -v0 * sy)
    print('  brillo de las monedas: textura propia de 32x32 (antes %dx%d pixeles de %s)' %
          (round((u1 - u0) * W), round((v1 - v0) * H), src.read().m_Name))


def export_ui(sc, ex, sc14=None):
    """Atlas de sprites (UIAtlas) y fuentes (UIFont) de NGUI."""
    atlases = []
    fonts = []
    for fname, f in sc.files.items():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            cl, fl = sc.read_mono(o)
            if cl not in ('UIAtlas', 'UIFont') or not fl:
                continue
            go = sc.resolve_ptr(o, fl['m_GameObject'])
            gname = go.read().m_Name if go else '?'
            if cl == 'UIAtlas':
                mat = sc.resolve_ptr(o, fl['material'])
                if mat is None or not fl['sprites']:
                    continue
                d = mat.read_typetree()
                tex = None
                for name, t in d['m_SavedProperties']['m_TexEnvs']:
                    tex = sc.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                    if tex is not None:
                        break
                if tex is None:
                    continue
                td = tex.read()
                atlases.append({'name': gname, 'tex': ex.tex_id(tex), 'w': td.m_Width, 'h': td.m_Height,
                                'pixel': fl['mPixelSize'], 'sprites': fl['sprites'], 'key': key(o)})
            else:
                if fl['mAtlas'][2] == 0:
                    continue
                at = sc.resolve_ptr(o, fl['mAtlas'])
                fonts.append({'name': gname, 'atlas_key': key(at) if at else None, 'font': fl['mFont'],
                              'spacing': (fl['mSpacingX'], fl['mSpacingY'])})
    baked_atlases, baked_fonts = bake_fonts(ex, atlases, fonts)
    if sc14 is not None:
        pa = build_psp_atlas(sc, sc14)
        if pa:
            for a in atlases:
                if a['name'] == 'LowResAtlas':
                    a['tex'] = len(ex.textures)
                    a['w'], a['h'], a['sprites'] = 512, 512, pa[1]
                    ex.textures.append(('generada', 'LowResAtlas14', pa[0], True))
        # pantalla de personajes como la de la 1.4 (CharacterModelPreviewFactory): miniaturas del carrusel y, para
        # el modelo grande, la pose de reposo de cada uno (1 fotograma, ~1 KB) en vez de la animacion de pintar
        prev = char_previews(sc14)
        th = render_char_thumbs(prev) if prev else None
        if th:
            atlases.append({'name': 'CharThumbs', 'tex': len(ex.textures), 'w': 256, 'h': 256, 'pixel': 1.0,
                            'sprites': th[1], 'key': ('gen', 'CharThumbs')})
            ex.textures.append(('generada', 'ui_chars', th[0], True))
            print('  miniaturas de %d personajes' % len(th[1]))
        if prev:
            # el Animation del personaje recibe solo la primera: el juego cambia el clip de ese estado
            ex.extra_anim['avatar_prefab'] = [len(ex.clips)]
            for c in prev[2]:
                ex.clips.append(('pose', c['clip'], 'pose_' + c['name']))
        bg = ui_backgrounds(sc14)
        if bg:
            ex.textures.append(('generada', 'ui_bg', bg[0]))
            ex.ui_bg_halloween = bg[1]
    out = bytearray(b'SSU1')
    out += struct.pack('<II', len(atlases) + len(baked_atlases), len(fonts) + len(baked_fonts))
    akeys = [a['key'] for a in atlases]
    for a in atlases:
        nb = a['name'].encode()[:31]
        out += struct.pack('<B', len(nb)) + nb.ljust(31, b'\0')
        out += struct.pack('<iIIfI', a['tex'], a['w'], a['h'], a['pixel'], len(a['sprites']))
        for sp in a['sprites']:
            sn = sp['name'].encode()[:31]
            o_, i_ = sp['outer'], sp['inner']
            out += struct.pack('<B', len(sn)) + sn.ljust(31, b'\0')
            out += struct.pack('<8f', o_['x'], o_['y'], o_['width'], o_['height'], i_['x'], i_['y'], i_['width'], i_['height'])
    # atlas de las fuentes pre-escaladas (sin sprites): van detras de los originales y antes de las fuentes
    for ba in baked_atlases:
        nb = ba['name'].encode()[:31]
        out += struct.pack('<B', len(nb)) + nb.ljust(31, b'\0')
        out += struct.pack('<iIIfI', ba['tex'], ba['w'], ba['h'], 1.0, 0)
    for fo in fonts:
        nb = fo['name'].encode()[:31]
        f = fo['font']
        ai = akeys.index(fo['atlas_key']) if fo['atlas_key'] in akeys else -1
        sn = f['mSpriteName'].encode()[:31]
        glyphs = f['mSaved']
        out += struct.pack('<B', len(nb)) + nb.ljust(31, b'\0')
        out += struct.pack('<B', len(sn)) + sn.ljust(31, b'\0')
        out += struct.pack('<iiiiiiI', ai, f['mSize'], f['mBase'], f['mWidth'], f['mHeight'], fo['spacing'][0], len(glyphs))
        for g in glyphs:
            out += struct.pack('<9h', g['index'], g['x'], g['y'], g['width'], g['height'], g['offsetX'], g['offsetY'],
                               g['advance'], 0)
    # fuentes pre-escaladas: fuentes con las coordenadas ya en su atlas nuevo
    for bf in baked_fonts:
        nb = bf['name'].encode()[:31]
        out += struct.pack('<B', len(nb)) + nb.ljust(31, b'\0')
        out += struct.pack('<B', 0) + b'\0' * 31
        out += struct.pack('<iiiiiiI', len(atlases) + bf['atlas'], bf['size'], bf['base'], 0, 0, bf['spacing'], len(bf['glyphs']))
        for g in bf['glyphs']:
            out += struct.pack('<9h', *g, 0)
    print('  %d atlas, %d fuentes (+%d pre-escaladas)' % (len(atlases), len(fonts), len(baked_fonts)))
    return bytes(out)


# Tamanos en pixeles a los que la interfaz del port dibuja cada familia (ver hud.c). Las letras se generan ya a
# ese tamano desde la textura original a resolucion completa (1024x512, que no cabe en la PSP) y se dibujan 1:1:
# sin esto el texto sale de la textura reducida a la mitad y ampliado, y se ve borroso en la consola.
BAKE_SIZES = {'Lilita': [14, 16, 18, 20, 24, 28, 34], 'Titan': [17, 20, 30, 52, 69]}   # hud.c no baja de 14 px
# caracteres por familia y tamano (None = todos): la Titan grande solo hace falta para los numeros
BAKE_CHARS = {'Lilita': {}, 'Titan': {52: '0123456789', 69: '0123456789'}}


# letras que la fuente del original no tiene (solo trae ASCII): se componen con la letra base y el acento dibujado
SPANISH = {'á': ('a', 'acute'), 'é': ('e', 'acute'), 'í': ('i', 'acute'), 'ó': ('o', 'acute'), 'ú': ('u', 'acute'),
           'Á': ('A', 'acute'), 'É': ('E', 'acute'), 'Í': ('I', 'acute'), 'Ó': ('O', 'acute'), 'Ú': ('U', 'acute'),
           'ñ': ('n', 'tilde'), 'Ñ': ('N', 'tilde'), 'ü': ('u', 'diaer'), 'Ü': ('U', 'diaer'),
           '¿': ('?', 'flip'), '¡': ('!', 'flip')}


def accent_mark(kind, px):
    """Acento, tilde o dieresis en blanco con alfa, dibujado a 4x y reducido (bordes suaves)."""
    from PIL import ImageDraw
    S = 4
    w, h = max(3, int(round(px * 0.34))), max(2, int(round(px * 0.2)))
    t = max(1.3, px / 8.0) * S
    im = Image.new('L', (w * S, h * S), 0)
    d = ImageDraw.Draw(im)
    if kind == 'acute':
        d.line([(w * S * 0.2, h * S - t * 0.5), (w * S * 0.85, t * 0.5)], fill=255, width=int(t))
    elif kind == 'tilde':
        pts = [(w * S * i / 12.0, h * S * 0.5 - math.sin(i / 12.0 * 2 * math.pi) * (h * S * 0.5 - t * 0.5)) for i in range(13)]
        d.line(pts, fill=255, width=int(t), joint='curve')
    else:
        r = t * 0.65
        for cx in (w * S * 0.25, w * S * 0.75):
            d.ellipse([cx - r, h * S * 0.5 - r, cx + r, h * S * 0.5 + r], fill=255)
    a = im.resize((w, h), Image.LANCZOS)
    return Image.merge('RGBA', (Image.new('L', (w, h), 255),) * 3 + (a,))


def spanish_glyphs(px, same):
    """Glifos compuestos (indice Latin-1) a partir de los de un tamano: [(px, tupla, imagen)]."""
    by = {gi[1][0]: gi for gi in same}
    out = []
    xg = by.get(ord('x'))
    xtop = xg[1][6] if xg else None
    for ch, (b, kind) in SPANISH.items():
        bg = by.get(ord(b))
        if bg is None or bg[2] is None:
            continue
        _, _, _, w, h, ox, oy, adv = bg[1]
        img = bg[2]
        if kind == 'flip':
            # signos de apertura: el del final girado, con la parte de arriba a la altura de la x
            out.append((px, (ord(ch), 0, 0, w, h, ox, xtop if xtop is not None else oy, adv), img.rotate(180)))
            continue
        top = oy
        if b == 'i' and xtop is not None and xtop > oy:
            img = img.crop((0, xtop - oy, w, h))           # i sin punto
            h, top = h - (xtop - oy), xtop
            oy = top
        elif b.islower() and xtop is not None:
            top = xtop
        m = accent_mark(kind, px)
        mw, mh = m.size
        gap = max(1, int(round(px / 14.0)))
        mox = ox + (w - mw) // 2 + (1 if kind == 'acute' else 0)
        moy = top - gap - mh
        x0, y0 = min(ox, mox), min(oy, moy)
        x1, y1 = max(ox + w, mox + mw), max(oy + h, moy + mh)
        comp = Image.new('RGBA', (x1 - x0, y1 - y0), (255, 255, 255, 0))
        comp.alpha_composite(img, (ox - x0, oy - y0))
        comp.alpha_composite(m, (mox - x0, moy - y0))
        out.append((px, (ord(ch), 0, 0, x1 - x0, y1 - y0, x0, y0, adv), comp))
    return out


def bake_fonts(ex, atlases, fonts):
    akeys = {a['key']: a for a in atlases}
    fam = {}
    for fo in fonts:
        m = re.match(r'([A-Za-z]+?)(\d+)$', fo['name'])
        if not m or m.group(1) not in BAKE_SIZES or fo['atlas_key'] not in akeys:
            continue
        fam.setdefault(m.group(1), []).append(fo)
    baked_atlases, baked_fonts = [], []
    for family, sizes in BAKE_SIZES.items():
        srcs = sorted(fam.get(family, []), key=lambda fo: fo['font']['mSize'])
        if not srcs:
            continue
        glyph_imgs = []      # (px, glyph tuple sin x/y, imagen)
        for px in sizes:
            # origen: la fuente original mas pequena que sea al menos 1.3 veces mayor (se reduce con buen filtro)
            src = next((fo for fo in srcs if fo['font']['mSize'] >= px * 1.3), srcs[-1])
            f = src['font']
            at = akeys[src['atlas_key']]
            img = ex.textures[at['tex']].read().image.convert('RGBA')
            sp = next((s_ for s_ in at['sprites'] if s_['name'] == f['mSpriteName']), None)
            sx, sy = (sp['outer']['x'], sp['outer']['y']) if sp else (0, 0)
            k = px / f['mSize']
            chars = BAKE_CHARS[family].get(px)
            for g in f['mSaved']:
                if chars is not None and chr(g['index']) not in chars:
                    continue
                w, h = g['width'], g['height']
                # a tamanos pequenos el redondeo dejaba letras montadas y espacios de 2 pixeles: un poco de aire entre
                # letras y un espacio de al menos un 30 % del tamano
                track = 1.0 if px <= 16 else 0.6 if px <= 24 else 0.0
                adv = int(round(g['advance'] * k + track))
                if g['index'] == 32:
                    adv = max(adv, int(round(px * 0.3)))
                ox, oy = int(round(g['offsetX'] * k)), int(round(g['offsetY'] * k))
                if w <= 0 or h <= 0:
                    glyph_imgs.append((px, (g['index'], 0, 0, 0, 0, ox, oy, adv), None))
                    continue
                crop = img.crop((int(sx + g['x']), int(sy + g['y']), int(sx + g['x'] + w), int(sy + g['y'] + h)))
                nw, nh = max(1, int(round(w * k))), max(1, int(round(h * k)))
                # alfa premultiplicado al reducir: sin halos oscuros alrededor de las letras
                gi = crop.convert('RGBa').resize((nw, nh), Image.LANCZOS).convert('RGBA')
                glyph_imgs.append((px, (g['index'], 0, 0, nw, nh, ox, oy, adv), gi))
            base = int(round(f['mBase'] * k))
            if chars is None and px <= (30 if family == 'Titan' else 24):   # los grandes de Lilita son nombres
                glyph_imgs += spanish_glyphs(px, [gi for gi in glyph_imgs if gi[0] == px])
            baked_fonts.append({'name': '%s@%d' % (family, px), 'atlas': len(baked_atlases), 'size': px, 'base': base,
                                'spacing': int(round(src['spacing'][0] * k)), 'glyphs': [], 'px': px})
        # empaquetado por estantes, de mas alto a mas bajo, con 1 pixel de separacion
        order = sorted([i for i, gi in enumerate(glyph_imgs) if gi[2] is not None], key=lambda i: -glyph_imgs[i][2].size[1])
        W = 512
        x = y = shelf = 0
        pos = {}
        for i in order:
            gw, gh = glyph_imgs[i][2].size
            if x + gw + 1 > W:
                x, y, shelf = 0, y + shelf + 1, 0
            pos[i] = (x, y)
            x += gw + 1
            shelf = max(shelf, gh)
        print('  fuente %s: %d glifos, alto usado %d' % (family, len(order), y + shelf))
        H = 1 << max(3, (y + shelf - 1).bit_length())
        if H > 512:
            raise SystemExit('Las fuentes %s no caben en 512x512 (hacen falta %d de alto)' % (family, y + shelf))
        sheet = Image.new('RGBA', (W, H), (255, 255, 255, 0))
        fonts_of_family = [bf for bf in baked_fonts if bf['name'].startswith(family + '@')]
        for i, (px, gt, gi) in enumerate(glyph_imgs):
            bf = next(b for b in fonts_of_family if b['px'] == px)
            if gi is not None:
                gx, gy = pos[i]
                sheet.paste(gi, (gx, gy))
                gt = (gt[0], gx, gy) + gt[3:]
            bf['glyphs'].append(gt)
        name = 'FontBaked' + family
        tid = len(ex.textures)
        # letras blancas con alfa: en T8 (paleta de 256 niveles de alfa) ocupan la mitad que en 4444 y tienen mas
        # niveles de transparencia; el color lo pone el vertice al dibujar
        sheet = Image.merge('RGBA', (Image.new('L', sheet.size, 255),) * 3 + (sheet.split()[3],))
        ex.textures.append(('generada', name, sheet, True))
        baked_atlases.append({'name': name, 'tex': tid, 'w': W, 'h': H})
        print('  fuente %s pre-escalada a %s px: atlas %dx%d' % (family, sizes, W, H))
    return baked_atlases, baked_fonts

# ---------------------------------------------------------------- main
EXCLUDE_ROOTS = {'0UI', 'IngameHighScore', 'XXXTestToRemove', 'temp'}


# ---------------------------------------------------------------- Halloween (1.4)
HW_TEXTURES = ('main', 'trains', 'train_start', 'enemies')


def unpack_apk(apk):
    tmp = tempfile.mkdtemp(prefix='sspsp_')
    atexit.register(shutil.rmtree, tmp, True)     # cada APK descomprimido ocupa 50-70 MB
    with zipfile.ZipFile(apk) as z:
        for n in z.namelist():
            if n.startswith('assets/bin/Data/'):
                z.extract(n, tmp)
    return os.path.join(tmp, 'assets', 'bin', 'Data')


def export_halloween(sc, outdir, ex, hq):
    """ThemeAssets de la 1.4: texturas y mallas del tema "halloween" con los mismos nombres que en la 1.0."""
    from types import SimpleNamespace
    themes = None
    for f in sc.files.values():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            try:
                scr = sc.script_of(o)
            except Exception:
                continue
            if scr and scr[2] == 'ThemeAssets':
                themes = (o, sc.read_mono(o)[1])
                break
        if themes:
            break
    if not themes or not themes[1]:
        print('  AVISO: el APK no tiene ThemeAssets (no es la 1.4 clasica): sin Halloween')
        return
    tobj, td = themes
    by_theme = {a['theme']: a for a in td['assets']}
    if 'halloween' not in by_theme:
        print('  AVISO: sin tema de Halloween en ThemeAssets')
        return

    def tex_map(a):
        r = {}
        for tm in a['textureAndMaterials']:
            o = sc.resolve_ptr(tobj, tm['texture'])
            if o is not None:
                d = o.read()
                r[d.m_Name] = d
        return r
    nrm14, hw14 = tex_map(by_theme['normal']), tex_map(by_theme['halloween'])
    v10 = {}
    for to in ex.textures:
        if not isinstance(to, tuple):
            d = to.read()
            v10.setdefault(d.m_Name, d)
    gen = []
    for name in HW_TEXTURES:
        if name not in v10 or name not in nrm14 or name not in hw14:
            print('  AVISO: falta la textura %s' % name)
            continue
        a = np.asarray(v10[name].image.convert('RGBA')).astype(np.int16)
        n = np.asarray(nrm14[name].image.convert('RGBA')).astype(np.int16)
        h = np.asarray(hw14[name].image.convert('RGBA'))
        if a.shape != n.shape or a.shape != h.shape:
            print('  AVISO: %s no tiene el mismo tamano en la 1.0 y la 1.4' % name)
            continue
        # el atlas de la 1.4 es el de la 1.0 con algun hueco reutilizado: donde la 1.4 normal no es igual que la
        # 1.0 se deja la 1.0 (si no, algun objeto de la 1.0 saldria con trozos de otro dibujo)
        diff = np.abs(a[:, :, :3] - n[:, :, :3]).sum(axis=2) > 30
        out = np.where(diff[:, :, None], a.astype(np.uint8), h)
        print('  textura %-12s %.1f %% de pixeles se quedan como en la 1.0' % (name, diff.mean() * 100))
        gen.append(('gen', name, Image.fromarray(out, 'RGBA'), name not in hq))
    if getattr(ex, 'ui_bg_halloween', None) is not None:
        gen.append(('gen', 'ui_bg', ex.ui_bg_halloween))     # fondo de las pantallas en Halloween
    meshes = []
    names10 = {m.read().m_Name for m in ex.meshes}
    a = by_theme['halloween']
    for p in a['environmentModels'] + a['characterModels']:
        go = sc.resolve_ptr(tobj, p)
        if go is None:
            continue
        stack = [sc.go_tree(go)]
        while stack:
            g = stack.pop()
            stack += g['children']
            for c in g['comps']:
                dd = c.get('data')
                mp = dd.get('m_Mesh') if isinstance(dd, dict) else None
                if not mp or not mp[2]:
                    continue
                mo = sc.resolve_ptr(c['obj'], mp)
                nm = mo.read().m_Name if mo is not None else ''
                base = nm[len('halloween_'):] if nm.startswith('halloween_') else nm
                if base in names10:      # solo lo que existe en la 1.0 (lo nuevo de la 1.4 no tiene donde ir)
                    meshes.append(mo)
    # Personajes de la 1.4 que no estan en la 1.0 (Characters.cs): Tagbot, Tasha, Zoe, Brody, Prince K y Zombie Jake
    # (solo en Halloween). Todos usan el esqueleto de Jake con los mismos 18 huesos en el mismo orden: en el juego se
    # dibujan con el nodo de Jake cambiando la malla y la textura. Sus mallas van a chars14.bin, aparte, para que el
    # juego cargue solo la del personaje que se esta viendo (cada una son ~150-250 KB de RAM).
    want = {'tag': 'tag', 'tasha': 'tasha', 'zoe': 'zoe', 'brody': 'brody', 'princek': 'princek',
            'zombiejake': 'halloweenJake'}
    found = {}
    for o in sc.env.objects:
        if o.type.name != 'SkinnedMeshRenderer':
            continue
        d = o.read_typetree()
        mo = sc.resolve(o.assets_file, d['m_Mesh']['m_FileID'], d['m_Mesh']['m_PathID'])
        if mo is None:
            continue
        mname = mo.read().m_Name
        key_ = next((k for k, v in want.items() if v == mname), None)
        if key_ is None or key_ in found:
            continue
        mat = sc.resolve(o.assets_file, d['m_Materials'][0]['m_FileID'], d['m_Materials'][0]['m_PathID'])
        md = mat.read_typetree() if mat is not None else None
        tex = None
        for nm_, t in (md['m_SavedProperties']['m_TexEnvs'] if md else []):
            nm_ = nm_['name'] if isinstance(nm_, dict) else nm_
            if nm_ == '_MainTex':
                tex = sc.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
        if tex is not None:
            found[key_] = (mo, tex.read())
    cmeshes = []
    for k in want:
        if k not in found:
            print('  AVISO: no se encuentra el personaje %s' % k)
            continue
        mo, td = found[k]
        cmeshes.append(mo)
        gen.append(('gen', 'avatar_' + ('zombieJake' if k == 'zombiejake' else k), td.image, True))
    print('  personajes de la 1.4: %s' % ', '.join(k for k in want if k in found))
    cm = SimpleNamespace(meshes=cmeshes, mesh_world=[False] * len(cmeshes))
    cbin, cstats = export_meshes(cm)
    with open(os.path.join(outdir, 'chars14.bin'), 'wb') as f:
        f.write(cbin)
    print('  %-11s %8d KB (%s)' % ('chars14.bin', len(cbin) // 1024, ', '.join(s_[0] for s_ in cstats)))
    hm = SimpleNamespace(meshes=meshes, mesh_world=[False] * len(meshes))
    mbin, stats = export_meshes(hm)
    tbin = export_textures(SimpleNamespace(textures=gen), hq=hq)
    for fn, b in (('hw_tex.bin', tbin), ('hw_mesh.bin', mbin)):
        with open(os.path.join(outdir, fn), 'wb') as f:
            f.write(b)
        print('  %-11s %8d KB' % (fn, len(b) // 1024))
    print('  mallas: %s' % ', '.join(s_[0] for s_ in stats))


def export_eboot_art(data_dir, outdir):
    """ICON0.PNG (144x80) y PIC1.PNG (480x272) del EBOOT, recortados de la pantalla de carga del APK (splash.png)."""
    f = os.path.join(data_dir, 'splash.png')
    if not os.path.exists(f):
        return
    sp = Image.open(f).convert('RGB')
    W, H = sp.size
    y0 = int(H * 0.31)            # fondo: Jake, Fresh, el inspector y el perro
    sp.crop((0, y0, W, y0 + int(W * 272 / 480))).resize((480, 272), Image.LANCZOS).save(os.path.join(outdir, 'PIC1.PNG'))
    w = int(W * 0.92)             # icono: el logo con el cielo
    x0 = (W - w) // 2
    sp.crop((x0, 0, x0 + w, int(w * 80 / 144))).resize((144, 80), Image.LANCZOS).save(os.path.join(outdir, 'ICON0.PNG'))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    apk, outdir = sys.argv[1], sys.argv[2]
    apk14 = None
    for opt in ('--apk14', '--halloween'):
        if opt in sys.argv:
            i = sys.argv.index(opt)
            apk14 = sys.argv[i + 1] if i + 1 < len(sys.argv) else None
    os.makedirs(outdir, exist_ok=True)
    print('Descomprimiendo el APK...')
    data_dir = unpack_apk(apk)
    export_eboot_art(data_dir, outdir)
    print('Cargando la escena de Unity...')
    sc = Scene(data_dir)
    ex = Exporter(sc)
    roots = sc.roots('level0')
    trees = []
    for r in roots:
        go = sc.go_tree(r)
        if go['name'] in EXCLUDE_ROOTS:
            continue
        trees.append(go)
    # prefabs de las monedas (CoinPool / InAirCoinsManager) y otros referenciados
    extra = []
    for t in trees:
        stack = [t]
        while stack:
            g = stack.pop()
            stack += g['children']
            for c in g['comps']:
                if c.get('class') in ('CoinPool', 'InAirCoinsManager') and c.get('fields'):
                    p = c['fields']['coinPrefab']
                    o = sc.resolve_ptr(c['obj'], p)
                    if o is not None and o.type.name == 'GameObject':
                        extra.append(o)
    seen = set()
    for o in extra:
        if key(o) in seen:
            continue
        seen.add(key(o))
        trees.append(sc.go_tree(o))
    for t in trees:
        ex.add_tree(t)
    print('  %d nodos' % len(ex.nodes))
    world = ex.build_world()
    # recoger tambien los clips de audio referenciados por los MonoBehaviour (AudioClipInfo, etc.)
    for n in ex.nodes:
        for c in n['go']['comps']:
            if c['type'] == 'AudioSource' and c.get('data'):
                ex.audio_id(sc.resolve_ptr(c['obj'], c['data']['m_audioClip']))
            f = c.get('fields')
            if not f:
                continue
            stack = [f]
            while stack:
                v = stack.pop()
                if isinstance(v, dict):
                    stack += list(v.values())
                elif isinstance(v, list):
                    if len(v) == 3 and v[0] == 'ptr':
                        o = sc.resolve_ptr(c['obj'], v)
                        if o is not None and o.type.name == 'AudioClip':
                            ex.audio_id(o)
                    else:
                        stack += v
                elif isinstance(v, tuple) and len(v) == 3 and v[0] == 'ptr':
                    o = sc.resolve_ptr(c['obj'], v)
                    if o is not None and o.type.name == 'AudioClip':
                        ex.audio_id(o)
    # el resto de clips de audio del juego (por si acaso)
    for o in sc.env.objects:
        if o.type.name == 'AudioClip':
            ex.audio_id(o)
    sc14 = None
    if apk14:
        print('Descomprimiendo el APK de la 1.4...')
        sc14 = Scene(unpack_apk(apk14))
    print('Exportando interfaz...')
    ui = export_ui(sc, ex, sc14)
    print('Exportando mallas...')
    mesh, stats = export_meshes(ex)
    tv = sum(s[1] for s in stats)
    tt = sum(s[2] for s in stats)
    print('  %d mallas, %d vertices, %d triangulos' % (len(stats), tv, tt))
    fix_coin_glow(ex)
    print('Exportando texturas...')
    hq = ('main', 'trains', 'FontAtlas')
    tex = export_textures(ex, hq=hq)
    print('Exportando animaciones...')
    anim = export_anims(ex)
    print('  %d clips' % len(ex.clips))
    print('Exportando audio...')
    sfx, names = export_audio(ex, outdir)
    print('  %d sonidos' % len(names))
    # materiales
    mat = bytearray(b'SSX1')
    mat += struct.pack('<I', len(ex.materials))
    for m in ex.materials:
        nb = m['name'].encode()[:31]
        mat += struct.pack('<B', len(nb)) + nb.ljust(31, b'\0')
        mat += struct.pack('<iI4f4f2fI', m['tex'], m['blend'], *m['st'], *m['color'], *m['distort'],
                           1 if m['sky'] else 0)
    # world.bin lleva al final materiales y nombres de sonidos (las cadenas se escriben al final)
    world = ex.build_world()
    names_blob = bytearray(struct.pack('<I', len(names)))
    for n in names:
        nb = n.encode()[:31]
        names_blob += struct.pack('<B', len(nb)) + nb.ljust(31, b'\0')
    for fn, b in (('world.bin', world), ('mat.bin', bytes(mat)), ('mesh.bin', mesh), ('tex.bin', tex),
                  ('anim.bin', anim), ('sfx.bin', sfx), ('ui.bin', ui)):
        with open(os.path.join(outdir, fn), 'wb') as f:
            f.write(b)
        print('  %-10s %8d KB' % (fn, len(b) // 1024))
    with open(os.path.join(outdir, 'meshes.txt'), 'w') as f:
        for i, s_ in enumerate(stats):
            f.write('%d %s %d %d %d\n' % (i, *s_))
    for fn in ('hw_tex.bin', 'hw_mesh.bin', 'chars14.bin'):
        if os.path.exists(os.path.join(outdir, fn)):
            os.remove(os.path.join(outdir, fn))
    if sc14 is not None:
        export_halloween(sc14, outdir, ex, hq)
    print('Listo.')


if __name__ == '__main__':
    main()
