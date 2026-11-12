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
from unityscene import Scene, MultiScene  # noqa: E402
from UnityPy.helpers.MeshHelper import MeshHandler  # noqa: E402

# ---------------------------------------------------------------- formato
# Debe coincidir con src/worldfmt.h
C_MESH, C_SKIN, C_BOX, C_SPHERE, C_CAPSULE, C_TRACKCHUNK, C_RANDOMIZER, C_RANDTARGET, C_GROUP, \
    C_SPAWNPOINT, C_RANDOFFSET, C_MIRROR, C_MOVINGTRAIN, C_MOVINGCOIN, C_COINPLACE, C_COINLINE, \
    C_COINJUMP, C_PICKUP, C_PICKUPROTATE, C_PICKUPDEFAULT, C_TUNNEL, C_TRACKOBJECT, C_RENDACT, \
    C_SNAP, C_GLOW, C_TUTORIAL, C_ANIMATION, C_CHARCTRL, C_CAMERA, C_COIN, C_VISIBLE, C_SSGROUP, \
    C_ONTRIGGER, C_DAILYLETTER, C_CURVEPOINT, C_JETCLOUD, C_RANDHOLD, C_SPAWNEGG = range(1, 39)

TAG_IDS = {'Untagged': 0, 'Player': 1, 'HitMovingTrain': 2, 'JumpTrain': 3, 'DodgeTrain': 4, 'RollBarrier': 5,
           'JumpBarrier': 6, 'HitBarrier': 7, 'HitTrain': 8, 'Ground': 9, 'Station': 10, 'Subway': 11,
           'JumpHighBarrier': 12, 'MainCamera': 13}

PICKUP_KIND = {'Coin': 0, 'CoinMagnetPickup': 1, 'SuperSneakersPickup': 2, 'JetpackPickup': 3,
               'DoubleScoreMultiplierPickup': 4, 'MysteryBoxPickup': 5, 'DailyLetterPickup': 6, 'EasterEggPickup': 7,
               'SaveMeTokenPickup': 8}

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
    # el archivo concreto (no su nombre): con dos APK a la vez los dos tienen sharedassets0.assets
    return (id(obj.assets_file), obj.path_id)


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
    def add_tree(self, go, parent=-1, remap=None):
        # una copia del huevo de Pascua (inject_eggs) lleva su propio mapa: sus referencias internas (PickupRotate,
        # PickupDefault) apuntan a sus propios nodos y no a los de otra copia
        if go.get('copy'):
            remap = {}
        idx = len(self.nodes)
        n = {'go': go, 'parent': parent, 'children': [], 'remap': remap}
        self.nodes.append(n)
        m = self.objmap if remap is None else remap
        m[key(go['obj'])] = idx
        for c in go['comps']:
            m[key(c['obj'])] = idx
        for ch in go['children']:
            n['children'].append(self.add_tree(ch, idx, remap))
        return idx

    def ref(self, obj, ptr):
        """Indice del nodo al que apunta un PPtr (a GameObject o a componente)."""
        if ptr is None or ptr[2] == 0:
            return -1
        o = self.sc.resolve_ptr(obj, ptr)
        if o is None:
            return -1
        rm = getattr(self, 'cur_remap', None)
        if rm is not None and key(o) in rm:
            return rm[key(o)]
        return self.objmap.get(key(o), -1)

    def hunt_slots(self, n, o, f):
        out = []
        for kind in ('egg', 'doll'):
            v = n['go']['hunt'].get(kind)
            if v == 'native':
                out.append(self.ref(o, f['easterEgg']) & 0xffffffff)
            elif v is None:
                out.append(0xffffffff)
            else:
                out.append(n['children'][v])
        return out

    def comps_of(self, n):
        go = n['go']
        out = []
        self.cur_remap = n.get('remap')
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
                elif cl == 'RandomizerHold':
                    # 1.5.2: el ambiente de cada tramo de carretera (carretera, con edificios, subterraneo, bosque,
                    # talud derecho o izquierdo); se elige uno y se mantiene 3000 unidades
                    tg = [self.ref(o, p) for p in f.get('children') or []]
                    out.append((C_RANDHOLD, [len(tg)] + [x & 0xffffffff for x in tg]))
                elif cl == 'RandomizerTarget':
                    tg = [self.ref(o, p) for p in f.get('Targets') or []]
                    out.append((C_RANDTARGET, [len(tg)] + [x & 0xffffffff for x in tg]))
                elif cl in ('Group', 'SuperSneakersGroup'):
                    out.append((C_SSGROUP if cl == 'SuperSneakersGroup' else C_GROUP, []))
                elif cl == 'SpawnPoint':
                    ks = ['dailyLetter', 'doubleScoreMultiplier', 'jetpackPickup', 'jumpBooster', 'magnetBooster',
                          'mysteryBox']
                    if 'saveMeToken' in f:   # la 1.9: la llave (Save Me)
                        ks.append('saveMeToken')
                    out.append((C_SPAWNPOINT, [self.ref(o, f[k_]) & 0xffffffff for k_ in ks]))
                    if go.get('hunt'):           # huevo y muneca anadidos (inject_hunts)
                        out.append((C_SPAWNEGG, self.hunt_slots(n, o, f)))
                elif cl == 'SpawnPointEgg':
                    # la 1.8 en adelante: punto propio de la caza (SpawnPointEgg, un Selector): [huevo, muneca]
                    if go.get('hunt'):
                        out.append((C_SPAWNEGG, self.hunt_slots(n, o, f)))
                    else:
                        out.append((C_SPAWNEGG, [self.ref(o, f['easterEgg']) & 0xffffffff, 0xffffffff]))
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
                                             self.ref(o, f.get('mesh')) & 0xffffffff, f2u(f['direction']),
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


CLUSTER = float(os.environ.get('SS_CLUSTER', 64))       # tamano de celda para trocear el escenario (culling por trozos); la curvatura es exacta en el GE


def mesh_triangles(mo, h):
    """Triangulos por submalla. En Unity 3.5 cada submalla dice si es tira (isTriStrip != 0) o lista; en Unity 4 (la
    1.7) no hay isTriStrip sino topology (0: lista de triangulos, 1: tira). Antes las de Unity 4 se leian como tiras:
    unas 2,5 veces mas triangulos, casi todos falsos."""
    tt = mo.read_typetree()
    ib = list(h.m_IndexBuffer or [])
    use16 = getattr(h, 'm_Use16BitIndices', True)
    out = []
    for sm in tt['m_SubMeshes']:
        first = sm['firstByte'] // (2 if use16 else 4)
        cnt = sm['indexCount']
        idx = ib[first:first + cnt]
        tris = []
        strip = sm['isTriStrip'] != 0 if 'isTriStrip' in sm else sm.get('topology', 0) == 1
        if not strip:
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


# Mapa de la 1.5.2: sus modelos grandes (bordes del barranco, bosque, carreteras, puente) son largos y con pocos
# vertices: en celdas de CLUSTER salian trozos de ~40 vertices, cada uno un dibujo (3,5 veces mas trozos que el mapa
# clasico). Ahi se empieza con celdas de ADAPT_MAX y solo se parten (a la mitad, hasta CLUSTER) las que pasan de
# ADAPT_VERTS vertices: lo denso queda igual que antes y lo disperso en menos trozos. Misma geometria y textura
ADAPT_MAX = 256.0
ADAPT_VERTS = 240


def cells_of(V, tris, size):
    cells = {}
    for t in tris:
        cx = sum(V[i][0] for i in t) / 3.0
        cy = sum(V[i][1] for i in t) / 3.0
        cz = sum(V[i][2] for i in t) / 3.0
        k = (int(math.floor(cx / size)), int(math.floor(cy / size)), int(math.floor(cz / size)))
        cells.setdefault(k, []).append(t)
    return list(cells.values())


def adaptive_cells(V, tris, size):
    out = []
    for g in cells_of(V, tris, size):
        if size > CLUSTER and len({i for t in g for i in t}) > ADAPT_VERTS:
            out += adaptive_cells(V, g, max(CLUSTER, size * 0.5))
        else:
            out.append(g)
    return out


def make_parts(V, UV, subs, skin, world, adaptive=False):
    """Divide la malla en trozos (submalla + celda espacial) con sus propios vertices."""
    parts = []
    for si, sm in enumerate(subs):
        if not sm:
            continue
        if world and not skin:
            groups = adaptive_cells(V, sm, ADAPT_MAX) if adaptive else cells_of(V, sm, CLUSTER)
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


# Versiones ligeras del escenario para verlo de lejos (ya en la niebla): solo otra lista de triangulos sobre los mismos
# vertices del trozo (no ocupan casi nada). Los vertices de la misma celda de (cell) unidades se juntan en uno de ellos
# (uno original: la textura no cambia) sin abrir grietas (lod_indices) y se quitan los triangulos que quedan sin area. El GE procesa cada indice: menos triangulos, menos trabajo, que es lo que no llegaba a 33 ms
# Desactivado: en la consola los tuneles se veian con huecos. Ninguna malla lleva versiones ligeras (el juego dibuja
# siempre la normal, como antes, en todos los mundos)
LOD_LEVELS = ()


def lod_indices(P, UVl, idx, cell, uvq):
    """Sin grietas: una posicion solo se junta con la de referencia de su celda (la del vertice de menor indice) si en
    todas las zonas del atlas que la usan existe tambien un vertice en la posicion de referencia. Asi los dos lados de
    una costura de textura (vertices repetidos en la misma posicion) se mueven siempre al mismo punto. Antes, cada zona
    elegia su propio punto y se abrian huecos (se veia el cielo en los tuneles)."""
    from collections import defaultdict
    P = np.asarray(P, np.float64)
    n = len(P)
    pk = [tuple(x) for x in np.floor(P / cell).astype(np.int64)]
    gk = [tuple(x) for x in np.floor(np.asarray(UVl, np.float64) * uvq).astype(np.int64)]
    pos = [tuple(x) for x in np.round(P, 3)]
    cells = defaultdict(dict)                 # celda -> posicion -> {zona del atlas: vertice}
    for i in range(n):
        cells[pk[i]].setdefault(pos[i], {}).setdefault(gk[i], i)
    rep = np.arange(n)
    for c, positions in cells.items():
        A = min(positions, key=lambda q: min(positions[q].values()))
        ga = positions[A]
        for q, groups in positions.items():
            if q == A or not set(groups) <= set(ga):
                continue
            positions[q] = {g: ga[g] for g in groups}
    for i in range(n):
        rep[i] = cells[pk[i]][pos[i]][gk[i]]
    T = rep[np.asarray(idx, np.int64).reshape(-1, 3)]
    ok = (T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])
    T = T[ok]
    if len(T):
        _, keep = np.unique(np.sort(T, 1), axis=0, return_index=True)
        T = T[np.sort(keep)]
    return [int(x) for x in T.ravel()]


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
        parts = make_parts(V, UV, subs, skin, world, getattr(ex, 'adaptive_parts', False))
        offs.append(len(body))
        names_ = getattr(ex, 'mesh_names', None)
        nm = (names_[mi] if names_ else d.m_Name).encode()[:31]
        body += struct.pack('<B', len(nm)) + nm.ljust(31, b'\0')
        nlod = len(LOD_LEVELS) if world and not skin else 0
        body += struct.pack('<HHBBH', len(parts), len(subs), 1 if skin else 0, nlod, 0)
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
            for cell, uvq in (LOD_LEVELS if nlod else ()):
                li = lod_indices(P, np.array([UV[i] for i in order], np.float64), idx, cell, uvq)
                body += struct.pack('<I', len(li))
                body += struct.pack('<%dH' % len(li), *li)
                if len(li) % 2:
                    body += b'\0\0'
            tv += len(order)
        if skin:
            bp = d.m_BindPose
            # lotes para el skinning por hardware del GE (como mucho 8 huesos por lote)
            bw, bi = h.m_BoneWeights, h.m_BoneIndices
            batches = []
            # Antes se cortaba un lote cada vez que el siguiente triangulo (en el orden de la malla) pasaba de 8 huesos:
            # el perro salia en 35 lotes y el inspector en 21 (cada lote es un dibujo y sus matrices). Ahora se juntan
            # los triangulos con los mismos huesos y esos grupos se empaquetan en lotes de hasta 8 huesos, empezando por
            # los grupos con mas huesos y anadiendo los que menos huesos nuevos traen
            for si, sm in enumerate(subs):
                groups = {}
                for t in sm:
                    tb = set()
                    for i in t:
                        for k in range(4):
                            if bw[i][k] > 0.0001:
                                tb.add(int(bi[i][k]))
                    groups.setdefault(frozenset(tb), []).append(t)
                left = sorted(groups.items(), key=lambda g: (-len(g[0]), -len(g[1])))
                while left:
                    bones, cur = set(left[0][0]), list(left[0][1])
                    left = left[1:]
                    while True:
                        best, bk = None, None
                        for k, (gb, gt) in enumerate(left):
                            add = len(gb - bones)
                            if len(bones) + add > 8:
                                continue
                            key = (add, -len(gt))
                            if bk is None or key < bk:
                                best, bk = k, key
                        if best is None:
                            break
                        bones |= left[best][0]
                        cur += left[best][1]
                        del left[best]
                    batches.append((si, sorted(bones), cur))
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


# texturas mas pequenas que las demas: la del inspector y el perro (enemies) a 256 (64 KB con paleta) cabe en la VRAM
# junto a las del escenario; el inspector ocupa como mucho un tercio de la pantalla. La misma en Halloween y Navidad
SMALL_TEX = {'enemies': 256}
SMALL_TEX_ON = False   # solo en el mapa de la 1.5.2 (el clasico como antes)


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
        lim = min(maxsize, (SMALL_TEX if SMALL_TEX_ON else {}).get(t.m_Name if t is not None else gen_name, maxsize))
        while nw > lim or nh > lim:     # misma escala en los dos ejes
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


def export_anims(ex, local_strings=False):
    # local_strings: las rutas de los huesos van en una tabla propia al final (SSA2). boardanim.bin vale para todos
    # los mundos (data/ y data/w15) y cada world.bin tiene su propia tabla de cadenas: con indices de la del mundo
    # clasico, en el de la 1.5.2 las pistas caian en huesos equivocados y el personaje se rompia en la tabla
    strs = Strings() if local_strings else ex.strings
    out = bytearray(b'SSA2' if local_strings else b'SSA1')
    out += struct.pack('<I', len(ex.clips))
    hdr = 8 + (4 if local_strings else 0)
    if local_strings:
        out += b'\0\0\0\0'      # posicion de la tabla de cadenas (se rellena al final)
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
            pb = strs.get(path)
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
        out += struct.pack('<I', o + hdr + 4 * len(offs))
    out += body
    if local_strings:
        while len(out) % 4:
            out += b'\0'
        struct.pack_into('<I', out, 8, len(out))
        out += strs.blob()
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


def streamed_audio(ao):
    """AudioClip de Unity 3.5 con el sonido aparte, en el .resS de su archivo (m_Stream = 2; en la 1.5.2 la musica
    del menu). UnityPy no lee bien su typetree: nombre, formato, tipo, 3D/hardware, stream, tamano y posicion."""
    raw = ao.get_raw_data()
    n = struct.unpack_from('<I', raw, 0)[0]
    if n <= 0 or n > 64 or 4 + n > len(raw):
        return None, None
    name = raw[4:4 + n].decode('utf-8', 'replace')
    p = 4 + n + (-n % 4)
    if p + 20 > len(raw):
        return None, None
    stream, size, off = struct.unpack_from('<III', raw, p + 12)
    # el .resS va junto al archivo .assets del clip, en la carpeta de datos del APK
    path = None
    base = os.path.basename(getattr(ao.assets_file, 'name', '') or '')
    folder = getattr(getattr(ao.assets_file, 'parent', None), 'path', None)
    if base and folder and os.path.exists(os.path.join(folder, base + '.resS')):
        path = os.path.join(folder, base + '.resS')
    if stream != 2 or path is None:
        return None, None
    with open(path, 'rb') as f:
        f.seek(off)
        return name, f.read(size)


def export_audio(ex, outdir, write_files=True):
    # write_files: la musica (mp3) y los wav de consulta solo en data/ (los demas mundos la comparten)
    if write_files:
        os.makedirs(os.path.join(outdir, 'music'), exist_ok=True)
        os.makedirs(os.path.join(outdir, 'wav'), exist_ok=True)
    names = []
    blobs = []
    for ao in ex.audio:
        try:
            d = ao.read()
            raw = bytes(d.m_AudioData or b'')
            name = d.m_Name
        except Exception:
            name, raw = streamed_audio(ao)
            if name is None:
                print('  AVISO: no se puede leer un AudioClip (%d)' % ao.path_id)
                continue
        names.append(name)
        if raw[:4] == b'FORM':
            r = decode_aiff(raw)
            if r is None:
                blobs.append(b'')
                continue
            a, rate = r
            if write_files:
                write_wav(os.path.join(outdir, 'wav', name + '.wav'), a, rate)
            pcm = resample(a, rate, 22050)
            blobs.append(np.clip(pcm, -32768, 32767).astype('<i2').tobytes())
        elif raw[:4] == b'RIFF':
            if write_files:
                with open(os.path.join(outdir, 'wav', name + '.wav'), 'wb') as f:
                    f.write(raw)
            with wave.open(io.BytesIO(raw)) as w:
                fr = w.readframes(w.getnframes())
                a = np.frombuffer(fr, '<i2').astype(np.float32).reshape(-1, w.getnchannels()).mean(axis=1)
                rate = w.getframerate()
            blobs.append(np.clip(resample(a, rate, 22050), -32768, 32767).astype('<i2').tobytes())
        else:
            # MP3 (musica y algun efecto): se guarda tal cual
            if write_files:
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


NOTEBOOK_SPRITES = (('GenericAtlas', 'newbackground'), ('GenericAtlas', 'metal_spiral_lo'), ('GenericAtlas', 'menu_decal8'),
                    ('LowResAtlas', 'front_fill_blue'), ('LowResAtlas', 'front_fill_green'),
                    ('LowResAtlas', 'front_fill_purple'), ('LowResAtlas', 'front_icon_friends'),
                    ('LowResAtlas', 'front_icon_me'), ('LowResAtlas', 'front_icon_shop'),
                    ('LowResAtlas', 'button_fill_menuMe'), ('LowResAtlas', 'icon_boards'), ('LowResAtlas', 'icon_xmas'))


# Ajustes (SettingsPopup) y lista de premios de las cajas (PrizeListPopup) de la 1.5.2: sus iconos, en otra hoja
SETTINGS_SPRITES = tuple(('LowResAtlas', n) for n in (
    'icon_soundOn', 'icon_off', 'icon_small_mysteryBox', 'icon_upgrades_headstart2000', 'icon_upgrades_headstart500',
    'icon_loginReward_stroke', 'trophy_goldChain_clock_stroke', 'icon_upgrades_hoverboard',
    'Icon_coin_pouch_large_stroke', 'trophy_fresh_token', 'trophy_yutani_token', 'trophy_spike_token',
    'trophy_tricky_token', 'background_stripes', 'coin2d',
    # DailyChallengePopup (reto diario)
    'icon_bigSuperMysteryBox', 'Icon_coin_pouch_large', 'Icon_coin_pouch_larger', 'checkbox_checked_mission',
    'background_splat'))


def sprite_from_raw_atlas(scx, name, small=False):
    """Un dibujo de las hojas de la interfaz (UIAtlas de NGUI) de una version de Unity 4 (la 1.7), cuya lista de
    dibujos no se puede leer con su tipo: se busca el nombre en los datos del UIAtlas y detras van sus rectangulos
    (outer x, y, ancho, alto). La hoja sale del material (lo primero tras el nombre del componente). Devuelve la imagen
    del mayor que encuentre (la hoja de alta resolucion; con small, el de la normal) o None"""
    import struct
    best = None
    byfile = {}
    for o in scx.env.objects:
        byfile.setdefault(o.assets_file.name, {})[o.path_id] = o
    key_ = name.encode() + b'\0'
    for o in scx.env.objects:
        if o.type.name != 'MonoBehaviour':
            continue
        try:
            raw = o.get_raw_data()
        except Exception:
            continue
        i = raw.find(key_)
        if i < 0 or len(raw) < 1000 or raw[i - 4:i] != struct.pack('<i', len(name)):
            continue
        try:
            p = 8 + 4 + 8
            n = struct.unpack_from('<i', raw, p)[0]
            p = (p + 4 + n + 3) & ~3
            fid, pid = struct.unpack_from('<ii', raw, p)
            x, y, w, h = struct.unpack_from('<4f', raw, i + ((len(name) + 3) & ~3))
            if fid == 0:
                mat = byfile[o.assets_file.name].get(pid)
            else:
                ext = o.assets_file.externals[fid - 1].path.split('/')[-1]
                mat = next((v.get(pid) for k, v in byfile.items() if k.lower() == ext.lower()), None)
            if mat is None:
                continue
            md = mat.read()
            tex = None
            for _, t in md.m_SavedProperties.m_TexEnvs:
                try:
                    tex = t.m_Texture.read()
                    break
                except Exception:
                    pass
            if tex is None:
                continue
            im = tex_rgba(tex).crop((int(x), int(y), int(x + w), int(y + h)))
            if best is None or (im.size[0] < best.size[0] if small else im.size[0] > best.size[0]):
                best = im
        except Exception:
            continue
    return best


def build_notebook_atlas(sc15, sprite_list=None, W=512, H=256, what='la libreta de la 1.5.2'):
    """Interfaz de la 1.5.2: la hoja de libreta (newbackground, espiral y dibujo de fondo, del GenericAtlas) y los
    botones nuevos de la portada (FRIENDS / ME / SHOP), en una hoja de 512x256 (u otra lista de sprites en otra hoja).
    Devuelve (imagen, sprites) o None."""
    srcs = {}
    items = []
    for at, n in (sprite_list or NOTEBOOK_SPRITES):
        if at not in srcs:
            srcs[at] = read_atlas(sc15, at)
        im, sp = srcs[at]
        if im is None or n not in sp:
            continue
        o = sp[n]['outer']
        w, h = int(round(o['width'])), int(round(o['height']))
        if n == 'menu_decal8':
            # el dibujo gris de fondo se ve grande y semitransparente: al 60 % no se nota y cabe todo en 512x256
            cut = im.crop((int(o['x']), int(o['y']), int(o['x']) + w, int(o['y']) + h))
            w, h = int(w * 0.6), int(h * 0.6)
            im = cut.resize((w, h), Image.LANCZOS)
            box = {'x': 0, 'y': 0, 'width': w, 'height': h}
            items.append((n, im, {'outer': box, 'inner': dict(box)}, w, h))
            continue
        items.append((n, im, sp[n], w, h))
    if not items:
        return None
    items.sort(key=lambda it: (-it[4], -it[3]))
    x = y = shelf = 0
    place = {}
    for n, im, sp, w, h in items:
        if x + w > W:
            x, y, shelf = 0, y + shelf + 2, 0
        if y + h > H:
            print('  AVISO: %s no cabe en %dx%d' % (what, W, H))
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
    print('  interfaz de la 1.5.2: %d sprites (libreta y portada) en 512x256' % len(items))
    return sheet, sprites


def export_board_crash_fx(sc15):
    """Character.hoverboardCrashParticleSystem de la 1.5.2 (el humo de colores al romperse la tabla): los parametros del
    ParticleSystem y su textura (spraySplash). fx.bin: SSF1 y los valores; None si no esta"""
    for f in sc15.files.values():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            cl, fl = sc15.read_mono(o)
            if cl != 'Character' or not fl or 'hoverboardCrashParticleSystem' not in fl:
                continue
            ps = sc15.resolve_ptr(o, fl['hoverboardCrashParticleSystem'])
            if ps is None:
                return None
            t = ps.read_typetree()
            go = sc15.resolve(ps.assets_file, t['m_GameObject']['m_FileID'], t['m_GameObject']['m_PathID'])
            tr = ren = None
            for c in go.read_typetree()['m_Component']:
                pp = c[-1] if isinstance(c, (list, tuple)) else c.get('component', c)
                co = sc15.resolve(go.assets_file, pp['m_FileID'], pp['m_PathID'])
                if co is not None and co.type.name == 'Transform':
                    tr = co.read_typetree()
                if co is not None and co.type.name == 'ParticleSystemRenderer':
                    ren = co
            tex = None
            if ren is not None:
                rd = ren.read_typetree()
                m = rd['m_Materials'][0]
                mat = sc15.resolve(ren.assets_file, m['m_FileID'], m['m_PathID'])
                if mat is not None:
                    for _, tx in mat.read_typetree()['m_SavedProperties']['m_TexEnvs']:
                        tt = sc15.resolve(mat.assets_file, tx['m_Texture']['m_FileID'], tx['m_Texture']['m_PathID'])
                        if tt is not None:
                            # formato 2 (ARGB4444 que en realidad es RGBA4444, como jetpackSmoke): con la imagen
                            # directa de UnityPy los canales salen cambiados y se pierde la transparencia (cuadrados)
                            tex = tex_rgba(tt.read())
                            break
            I = t['InitialModule']

            def rng(k):
                v = I[k]
                hi = v['scalar']
                lo = v['minCurve']['m_Curve'][0]['value'] * hi if v.get('minMaxState') == 3 else hi
                return lo, hi
            sc = I['startColor']
            cols = []
            for g in ('minGradient', 'maxGradient'):
                G = sc[g]
                cols += [G['key0']['rgba'], G['key1']['rgba']]
            E = t['EmissionModule']
            S = t['ShapeModule']
            sz = t['SizeModule']['curve']['maxCurve']['m_Curve'] if t['SizeModule']['enabled'] else [{'value': 1.0}, {'value': 1.0}]
            lp, lr, ls = tr['m_LocalPosition'], tr['m_LocalRotation'], tr['m_LocalScale']
            out = bytearray(b'SSF1')
            out += struct.pack('<I', E['cnt0'] if E.get('m_BurstCount', 0) > 0 else 0)
            out += struct.pack('<2f', *rng('startLifetime')) + struct.pack('<2f', *rng('startSpeed'))
            out += struct.pack('<2f', *rng('startSize')) + struct.pack('<f', I['startRotation']['scalar'])
            out += struct.pack('<2f', S['radius'], S['angle'])
            out += struct.pack('<3f', lp['x'], lp['y'], lp['z']) + struct.pack('<4f', lr['x'], lr['y'], lr['z'], lr['w'])
            out += struct.pack('<f', ls['x']) + struct.pack('<2f', sz[0]['value'], sz[-1]['value'])
            out += struct.pack('<4I', *cols)
            return bytes(out), tex
    return None


def find_texture(sc, name):
    for f in sc.files.values():
        for o in f.objects.values():
            if o.type.name == 'Texture2D' and o.read().m_Name == name:
                return o.read().image
    return None


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


def soft_raster(X, Y, Z, UV, tris, tex, W, H, buf=None):
    """Rasterizado por software con z-buffer (mas cerca = Z menor) y textura sin luz, como los shaders del juego.
    buf = (color, z) para dibujar varias piezas en la misma imagen."""
    th, tw = tex.shape[:2]
    col, zb = buf if buf is not None else (np.zeros((H, W, 4), np.uint8), np.full((H, W), np.inf))
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
    return col


def ss_down(col, w, h, ss):
    """Reduce la imagen supermuestreada con alfa premultiplicado (sin halo oscuro en el borde)."""
    f = col.astype(np.float64)
    f[:, :, :3] *= f[:, :, 3:4] / 255.0
    f = f.reshape(h, ss, w, ss, 4).mean(axis=(1, 3))
    a_ = f[:, :, 3:4]
    f[:, :, :3] = np.where(a_ > 0, f[:, :, :3] * 255.0 / np.maximum(a_, 1e-6), 0)
    return Image.fromarray(np.clip(np.rint(f), 0, 255).astype(np.uint8), 'RGBA')


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
        # de frente: el personaje mira hacia +z; la camara esta delante mirando hacia -z (x al reves)
        cx = (S[:, 0].max() + S[:, 0].min()) * 0.5
        scale = min(scale0, (W - 2 * THUMB_SS) / (S[:, 0].max() - S[:, 0].min()))   # el disfraz de Yutani
        X = W * 0.5 - (S[:, 0] - cx) * scale
        Y = H - THUMB_SS - (S[:, 1] - S[:, 1].min()) * scale
        im = ss_down(soft_raster(X, Y, -S[:, 2], UV, tris, tex, W, H), THUMB_W, THUMB_H, THUMB_SS)
        x, y = (n % cols) * THUMB_W, (n // cols) * THUMB_H
        sheet.paste(im, (x, y))
        r_ = {'x': x, 'y': y, 'width': THUMB_W, 'height': THUMB_H}
        sprites.append({'name': 'thumb_' + name, 'outer': r_, 'inner': dict(r_)})
    return sheet, sprites


# premios de las cajas misteriosas (MysteryBoxHandler de la 1.4): campo del prefab -> nombre del sprite en el juego
REWARD_PREFABS = [('rewardCoins', ['Icon_coin_pouch_larger', 'icon_coin_large']),
                  ('rewardTokenTricky', ['trophy_tricky_token']), ('rewardTokenFresh', ['trophy_fresh_token']),
                  ('rewardTokenSpike', ['trophy_spike_token']), ('rewardTokenYutani', ['trophy_yutani_token']),
                  ('trophyDiamond', ['trophy_diamond']), ('trophyGoldBar', ['trophy_gold']),
                  ('trophyChainClock', ['trophy_goldChainClock']), ('trophyChainDollar', ['trophy_goldChainDollar']),
                  ('trophyGoldSkull', ['trophy_goldSkull']), ('trophyHeadphones', ['trophy_headphones']),
                  ('trophyLPBlack', ['trophy_lpBlack']), ('trophyTapeBlack', ['trophy_tapeBlack']),
                  ('rewardPowerupHoverboard', ['icon_upgrades_hoverboard']),
                  ('rewardPowerupHeadstart500', ['icon_upgrades_headstart500']),
                  ('rewardPowerupHeadstart2000', ['icon_upgrades_headstart2000'])]
# las cajas: cerrada y con la tapa levantada (clip "up" a la mitad, cuando sale el premio)
REWARD_BOXES = [('mysteryBoxPrefab', 'box_mystery'), ('superMysteryBoxPrefab', 'box_super')]
REWARD_PX, REWARD_BOX_PX = 72, 88
# giro con el que aparecen (MysteryBoxHandler._rewardStartRotation y _boxRotation). En la 1.4 los premios dan vueltas:
# la gorra de Tricky se ve mejor de frente que por dentro
REWARD_ROT = {'rewardTokenTricky': (15.0, 169.5, 0.0), 'mysteryBoxPrefab': (0.0, 250.0, 20.0),
              'superMysteryBoxPrefab': (0.0, 250.0, 20.0)}


def euler_quat(ex_, ey, ez):
    """Quaternion.Euler de Unity (Z, luego X, luego Y)."""
    def q(ax, a):
        h = math.radians(a) * 0.5
        v = [0.0, 0.0, 0.0, math.cos(h)]
        v[ax] = math.sin(h)
        return v

    def mul(a, b):
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]
    return mul(q(1, ey), mul(q(0, ex_), q(2, ez)))


def reward_parts(sc14, go, root_rot, texcache, up_time=None):
    """Piezas (vertices en el mundo, uv, triangulos, textura) de un prefab de premio. up_time: pose del clip "up"
    de su Animation (la tapa de la caja)."""
    parts = []

    def walk(g, M, top, pose, rel):
        td = g['tr']['data']
        p, r, s_ = td['m_LocalPosition'], td['m_LocalRotation'], td['m_LocalScale']
        pos, rot, scl = [p['x'], p['y'], p['z']], [r['x'], r['y'], r['z'], r['w']], [s_['x'], s_['y'], s_['z']]
        anim = next((c for c in g['comps'] if c['type'] == 'Animation' and c.get('data')), None)
        if anim is not None and up_time is not None:
            for p_ in anim['data']['m_Animations']:
                cl = sc14.resolve_ptr(anim['obj'], p_)
                if cl is not None and cl.read_typetree()['m_Name'] == 'up':
                    pose, rel = clip_pose(cl, up_time), ''
        if pose is not None and rel is not None and rel in pose:
            o = pose[rel]
            pos, rot, scl = o.get('pos', pos), o.get('rot', rot), o.get('scl', scl)
        if top:
            rot, pos = root_rot, [0.0, 0.0, 0.0]
        m = np.eye(4)
        m[:3, :3] = quat_mat(np.array(rot) / (np.linalg.norm(rot) or 1.0)) * np.array(scl)
        m[:3, 3] = pos
        M = M @ m
        filt = next((c for c in g['comps'] if c['type'] == 'MeshFilter' and c.get('data')), None)
        rend = next((c for c in g['comps'] if c['type'] == 'MeshRenderer' and c.get('data')), None)
        if filt and rend and rend['data']['m_Materials']:
            mo = sc14.resolve_ptr(filt['obj'], filt['data']['m_Mesh'])
            mat = sc14.resolve_ptr(rend['obj'], rend['data']['m_Materials'][0])
            to = None
            if mat is not None:
                for nm_, t in mat.read_typetree()['m_SavedProperties']['m_TexEnvs']:
                    to = sc14.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                    if to is not None:
                        break
            if mo is not None and to is not None:
                h = MeshHandler(mo.read())
                h.process()
                V = np.array(h.m_Vertices, np.float64).reshape(-1, 3)
                Wv = (M @ np.c_[V, np.ones(len(V))].T).T[:, :3]
                if to.path_id not in texcache:
                    texcache[to.path_id] = np.asarray(tex_rgba(to.read()).convert('RGBA'))
                tris = np.array([t for sm in mesh_triangles(mo, h) for t in sm], np.int64)
                parts.append((Wv, np.array(h.m_UV0, np.float64).reshape(-1, 2), tris, texcache[to.path_id]))
        for ch in g['children']:
            walk(ch, M, False, pose, (rel + '/' if rel else '') + ch['name'] if rel is not None else None)
    walk(sc14.go_tree(go), np.eye(4), True, None, None)
    return parts


def draw_parts(parts, px, box):
    """Dibuja las piezas en una celda de px x px; box = (min, max) de los vertices que tiene que llenar."""
    ss = 4
    W = px * ss
    mn, mx = box
    # la camara de la interfaz mira hacia +z: x a la derecha, y arriba, mas cerca = z menor
    scale = (W - 4 * ss) / max(mx[0] - mn[0], mx[1] - mn[1])
    cx, cy = (mx[0] + mn[0]) * 0.5, (mx[1] + mn[1]) * 0.5
    buf = (np.zeros((W, W, 4), np.uint8), np.full((W, W), np.inf))
    for Wv, UV, tris, tex in parts:
        soft_raster(W * 0.5 + (Wv[:, 0] - cx) * scale, W * 0.5 - (Wv[:, 1] - cy) * scale, Wv[:, 2], UV, tris, tex,
                    W, W, buf)
    return ss_down(buf[0], px, px, ss)


def render_rewards(sc14):
    """Los modelos 3D que salen de las cajas en la 1.4 (la bolsa de monedas, las fichas, los trofeos, la tabla y
    los headstart) y las dos cajas, cerradas y abiertas, dibujados una vez como aparecen alli. Los premios en celdas
    de 72x72 y las cajas de 88x88, que el juego dibuja 1:1 o mas pequenas (nunca estiradas): una hoja de 512x256
    (128 KB en T8) que solo se carga mientras se abren cajas."""
    fl = obj = None
    for f in sc14.files.values():
        for o in f.objects.values():
            if o.type.name == 'MonoBehaviour':
                cl, fl_ = sc14.read_mono(o)
                if cl == 'MysteryBoxHandler' and fl_:
                    fl, obj = fl_, o
                    break
        if fl:
            break
    if fl is None:
        return None
    sheet = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
    sprites = []
    texcache = {}

    def add(names, im, x, y, px):
        sheet.paste(im, (x, y))
        for nm in names:
            r_ = {'x': x, 'y': y, 'width': px, 'height': px}
            sprites.append({'name': nm, 'outer': r_, 'inner': dict(r_)})
    for n, (field, names) in enumerate(REWARD_PREFABS):
        go = sc14.resolve_ptr(obj, fl.get(field))
        if go is None:
            continue
        parts = reward_parts(sc14, go, euler_quat(*REWARD_ROT.get(field, (15.0, -10.5, 0.0))), texcache)
        if not parts:
            continue
        allv = np.vstack([p_[0] for p_ in parts])
        add(names, draw_parts(parts, REWARD_PX, (allv.min(axis=0), allv.max(axis=0))),
            (n % 7) * REWARD_PX, (n // 7) * REWARD_PX, REWARD_PX)
    k = 0
    for field, name in REWARD_BOXES:
        go = sc14.resolve_ptr(obj, fl.get(field))
        if go is None:
            continue
        rot = euler_quat(*REWARD_ROT[field])
        shut, opened = reward_parts(sc14, go, rot, texcache, 0.0), reward_parts(sc14, go, rot, texcache, 0.46)
        if not shut or not opened:
            continue
        allv = np.vstack([p_[0] for p_ in shut + opened])     # la misma escala para las dos: la caja no salta
        box = (allv.min(axis=0), allv.max(axis=0))
        for parts, suffix in ((shut, ''), (opened, '_open')):
            add([name + suffix], draw_parts(parts, REWARD_BOX_PX, box), 2 * REWARD_PX + k * REWARD_BOX_PX,
                2 * REWARD_PX, REWARD_BOX_PX)
            k += 1
    return (sheet, sprites) if sprites else None


REWARD_SPIN_FRAMES, REWARD_SPIN_PX = 12, 64


def render_reward_spins(sc14):
    """Los premios de la caja dando vueltas como en el original (MysteryBoxHandler los gira en 3D): cada modelo se
    dibuja desde 12 angulos (cada 30 grados alrededor del eje vertical) con la misma escala en todos, en una hoja de
    256x256 por premio (64 KB en T8) que el juego carga solo mientras ese premio esta en pantalla. Devuelve
    [(nombre del sprite plano, imagen)]"""
    fl = obj = None
    for f in sc14.files.values():
        for o in f.objects.values():
            if o.type.name == 'MonoBehaviour':
                cl, fl_ = sc14.read_mono(o)
                if cl == 'MysteryBoxHandler' and fl_:
                    fl, obj = fl_, o
                    break
        if fl:
            break
    if fl is None:
        return []
    out = []
    texcache = {}
    px = REWARD_SPIN_PX
    for field, names in REWARD_PREFABS:
        go = sc14.resolve_ptr(obj, fl.get(field))
        if go is None:
            continue
        rx, ry, rz = REWARD_ROT.get(field, (15.0, -10.5, 0.0))
        frames = [reward_parts(sc14, go, euler_quat(rx, ry + k * 360.0 / REWARD_SPIN_FRAMES, rz), texcache)
                  for k in range(REWARD_SPIN_FRAMES)]
        if not all(frames):
            continue
        allv = np.vstack([p_[0] for fr in frames for p_ in fr])   # la misma escala en todos: no late al girar
        box = (allv.min(axis=0), allv.max(axis=0))
        sheet = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
        for k, fr in enumerate(frames):
            sheet.paste(draw_parts(fr, px, box), ((k % 4) * px, (k // 4) * px))
        out.append((names[0], sheet))
    return out


BOX_FRAME_TIMES, BOX_FRAME_PX = (0.0, 0.15, 0.30, 0.46), 88


def render_box_frames(sc14):
    """Las dos cajas (normal y super) a su tamano de siempre (88 px) con la tapa abriendose en 4 pasos: el clip "up" de su
    Animation (MysteryBoxHandler) muestreado de 0 a 0,46 s, la misma escala en todos (la caja no salta). Hoja de
    512x256 (128 KB en T8) que solo se carga mientras se abren cajas. Devuelve (imagen, sprites) o None"""
    fl = obj = None
    for f in sc14.files.values():
        for o in f.objects.values():
            if o.type.name == 'MonoBehaviour':
                cl, fl_ = sc14.read_mono(o)
                if cl == 'MysteryBoxHandler' and fl_:
                    fl, obj = fl_, o
                    break
        if fl:
            break
    if fl is None:
        return None
    sheet = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
    sprites = []
    texcache = {}
    px = BOX_FRAME_PX
    for row, (field, name) in enumerate(REWARD_BOXES):
        go = sc14.resolve_ptr(obj, fl.get(field))
        if go is None:
            continue
        rot = euler_quat(*REWARD_ROT[field])
        frames = [reward_parts(sc14, go, rot, texcache, t) for t in BOX_FRAME_TIMES]
        if not all(frames):
            continue
        allv = np.vstack([p_[0] for fr in frames for p_ in fr])
        box = (allv.min(axis=0), allv.max(axis=0))
        for k, fr in enumerate(frames):
            x, y = k * (px + 4), row * (px + 4)
            sheet.paste(draw_parts(fr, px, box), (x, y))
            r_ = {'x': x, 'y': y, 'width': px, 'height': px}
            sprites.append({'name': '%s_f%d' % (name, k), 'outer': r_, 'inner': dict(r_)})
    return (sheet, sprites) if sprites else None


def fix_coin_glow(ex):
    """El brillo de las monedas (material "glow gold", Additive falloff) es un trocito de 26x26 pixeles de la hoja
    'main' de 1024x1024, pegado a otros dibujos. En la PSP la hoja va a 512 y el filtrado mezcla esos vecinos en el
    borde del plano: se veia un cuadro semitransparente con el borde de colores. Se saca a su propia textura de 32x32
    (2 KB) con el borde a negro (en Additive el negro no suma nada) y el material apunta a ella.
    Con el mundo de otra version (data/w15) hay mas de un material asi (el de la 1.0 y el de la 1.5.2): todos."""
    for mi, mat in enumerate(ex.materials):
        if mat['name'] == 'glow gold' and mat['tex'] >= 0:
            fix_glow_material(ex, mi)


def fix_glow_material(ex, mi):
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


def export_ui(sc, ex, sc14=None, boards=None, sc15=None, city_sc=None):
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
        for scx, ver in ((sc15, '1.5.2'), (getattr(ex, 'sc16', None), '1.6'), (getattr(ex, 'sc17', None), '1.7'),
                         (getattr(ex, 'sc18', None), '1.8'), (getattr(ex, 'sc19', None), '1.9'),
                         (getattr(ex, 'sc110', None), '1.10')):
            if not prev or scx is None:
                continue
            # los que trae de mas (Elf Tricky en la 1.5.2, Tony en la 1.6): con el nombre de su malla, que es como
            # los busca el juego
            have = {c['name'] for c in prev[2]}
            p15 = char_previews(scx)
            for c in (p15[2] if p15 else []):
                if c['name'] not in have and c['name'] != 'zombiejake':
                    c = dict(c, name=c['mesh'].read().m_Name)
                    if c['name'] not in have:
                        prev[2].append(c)
                        print('  miniatura y pose de la %s: %s' % (ver, c['name']))
        th = render_char_thumbs(prev) if prev else None
        if th:
            atlases.append({'name': 'CharThumbs', 'tex': len(ex.textures), 'w': 256, 'h': 256, 'pixel': 1.0,
                            'sprites': th[1], 'key': ('gen', 'CharThumbs')})
            ex.textures.append(('generada', 'ui_chars', th[0], True))
            print('  miniaturas de %d personajes' % len(th[1]))
        rw = render_rewards(sc14)
        if rw:
            atlases.append({'name': 'RewardIcons', 'tex': len(ex.textures), 'w': 512, 'h': 256, 'pixel': 1.0,
                            'sprites': rw[1], 'key': ('gen', 'RewardIcons')})
            ex.textures.append(('generada', 'ui_rewards', rw[0], True))
            print('  premios de las cajas: %d dibujos' % len(set(s_['outer']['x'] * 1000 + s_['outer']['y'] for s_ in rw[1])))
            bf = render_box_frames(sc14)
            if bf:
                atlases.append({'name': 'BoxFrames', 'tex': len(ex.textures), 'w': 512, 'h': 256, 'pixel': 1.0,
                                'sprites': bf[1], 'key': ('gen', 'BoxFrames')})
                ex.textures.append(('generada', 'ui_boxes', bf[0], True))
                print('  cajas abriendose: %d dibujos de %d px' % (len(bf[1]), BOX_FRAME_PX))
            spins = render_reward_spins(sc14)
            for nm, im in spins:
                ex.textures.append(('generada', ('rw3d_' + nm)[:31], im, True))
            print('  premios girando en 3D: %d (12 vistas cada uno)' % len(spins))
        if prev:
            # el Animation del personaje recibe solo la primera: el juego cambia el clip de ese estado
            ex.extra_anim['avatar_prefab'] = ex.extra_anim.get('avatar_prefab', []) + [len(ex.clips)]
            for c in prev[2]:
                ex.clips.append(('pose', c['clip'], 'pose_' + c['name']))
        if boards is not None and boards.thumbs is not None:
            atlases.append({'name': 'BoardThumbs', 'tex': len(ex.textures), 'w': 256, 'h': boards.thumbs[0].size[1], 'pixel': 1.0,
                            'sprites': boards.thumbs[1], 'key': ('gen', 'BoardThumbs')})
            ex.textures.append(('generada', 'ui_boards', boards.thumbs[0], True))
        nb = build_notebook_atlas(sc15) if sc15 is not None else None
        if nb:
            atlases.append({'name': 'Notebook15', 'tex': len(ex.textures), 'w': 512, 'h': 256, 'pixel': 1.0,
                            'sprites': nb[1], 'key': ('gen', 'Notebook15')})
            ex.textures.append(('generada', 'ui_notebook', nb[0], True))
        st = build_notebook_atlas(sc15, SETTINGS_SPRITES, 256, 256, 'ajustes y premios') if sc15 is not None else None
        if st:
            atlases.append({'name': 'Settings15', 'tex': len(ex.textures), 'w': 256, 'h': 256, 'pixel': 1.0,
                            'sprites': st[1], 'key': ('gen', 'Settings15')})
            ex.textures.append(('generada', 'ui_settings', st[0], True))
            print('  ajustes y premios de la 1.5.2: %d sprites en 256x256' % len(st[1]))
        # icono del Score Booster de la 1.7 (su hoja de interfaz es de Unity 4): el de la hoja normal (35x35), a 32x32
        sc17_ = getattr(ex, 'sc17', None)
        if sc17_ is not None:
            ic = sprite_from_raw_atlas(sc17_, 'icon_upgrades_scorebooster', small=True)
            if ic is not None:
                ex.textures.append(('generada', 'ui_booster', ic.resize((32, 32), Image.LANCZOS), True))
                print('  icono del Score Booster de la 1.7: %dx%d' % ic.size)
            # en la tienda el original lo pone sobre el fondo de rayos azul de los iconos (icon_background_normal):
            # los de la 1.4 ya lo llevan dibujado, este no
            bgi = sprite_from_raw_atlas(sc17_, 'icon_background_normal', small=True)
            if ic is not None and bgi is not None:
                c = bgi.convert('RGBA').resize((32, 32), Image.LANCZOS)
                st_ = ic.convert('RGBA').resize((29, 29), Image.LANCZOS)
                c.alpha_composite(st_, ((32 - 29) // 2, (32 - 29) // 2))
                ex.textures.append(('generada', 'ui_booster_shop', c, True))
                print('  icono del Score Booster para la tienda, con el fondo de rayos')
        # la llave de la 1.9 (Save Me, de momento solo en el mapa de Sidney): su icono de la hoja normal
        sc19_ = getattr(ex, 'level_sc', None)
        if sc19_ is not None and sc19_ in (getattr(ex, 'sc19', None), getattr(ex, 'sc110', None)):
            ic = sprite_from_raw_atlas(sc19_, 'icon_key', small=True)
            if ic is not None:
                w_, h_ = ic.size
                sq = Image.new('RGBA', (max(w_, h_),) * 2, (0, 0, 0, 0))
                sq.paste(ic, ((sq.size[0] - w_) // 2, (sq.size[1] - h_) // 2))
                ex.textures.append(('generada', 'ui_key', sq.resize((32, 32), Image.LANCZOS), True))
                print('  icono de la llave de la 1.9: %dx%d' % (w_, h_))
        # icono de la muneca de la caza semanal de la 1.10 (contador), de su hoja normal
        sc110_ = getattr(ex, 'sc110', None)
        if sc110_ is not None:
            ic = sprite_from_raw_atlas(sc110_, 'icon_doll_medium', small=True)
            if ic is not None:
                w_, h_ = ic.size
                sq = Image.new('RGBA', (max(w_, h_),) * 2, (0, 0, 0, 0))
                sq.paste(ic, ((sq.size[0] - w_) // 2, (sq.size[1] - h_) // 2))
                ex.textures.append(('generada', 'ui_doll', sq.resize((32, 32), Image.LANCZOS), True))
                print('  icono de la muneca de la caza semanal: %dx%d' % (w_, h_))
        # icono del huevo de Pascua de la 1.8 (contador de la busqueda de huevos), de su hoja normal
        sc18_ = getattr(ex, 'sc18', None) or getattr(ex, 'sc19', None)
        if sc18_ is not None:
            ic = sprite_from_raw_atlas(sc18_, 'icon_egg', small=True)
            if ic is not None:
                w_, h_ = ic.size
                sq = Image.new('RGBA', (max(w_, h_),) * 2, (0, 0, 0, 0))
                sq.paste(ic, ((sq.size[0] - w_) // 2, (sq.size[1] - h_) // 2))
                ex.textures.append(('generada', 'ui_egg', sq.resize((32, 32), Image.LANCZOS), True))
                print('  icono del huevo de la 1.8: %dx%d' % (w_, h_))
        mb = find_texture(sc15, 'mysterybox_bg') if sc15 is not None else None
        if mb is not None:
            # remolino de colores detras de la caja misteriosa (MysteryBoxPopup): difuminado, basta con 256x256
            ex.textures.append(('generada', 'ui_mbox_bg', mb.convert('RGB').resize((256, 256), Image.LANCZOS)))
        # 1.10 (MysteryBoxPopup.AnimateStripes): cajas de la caza semanal con el degradado de Tokio y rayas que suben
        sc110b = getattr(ex, 'sc110', None)
        if sc110b is not None:
            tb = find_texture(sc110b, 'mysterybox_bg_tokyo')
            ts = find_texture(sc110b, 'tokyo_stripe')
            if tb is not None and ts is not None:
                ex.textures.append(('generada', 'ui_mbox_tokyo', tb.convert('RGB').resize((128, 8), Image.LANCZOS)))
                ex.textures.append(('generada', 'ui_stripe', ts.convert('RGBA').resize((16, 256), Image.LANCZOS), True))
                print('  fondo y rayas de Tokio para las cajas de la caza semanal')
        fx = export_board_crash_fx(sc15) if sc15 is not None else None
        if fx is not None:
            ex.fx_bin = fx[0]
            if fx[1] is not None:
                ex.textures.append(('generada', 'fx_spray', fx[1].convert('RGBA'), True))
            print('  humo al romperse la tabla (hoverboardCrashParticleSystem): %d particulas' % struct.unpack_from('<I', fx[0], 4)[0])
        spc = find_texture(city_sc, 'splashScreen') if city_sc is not None else None
        sp15 = find_texture(sc15, 'splashScreen') if sc15 is not None and spc is None else None
        if spc is not None:
            # mapas de ciudad (1.6 en adelante): cada uno con la imagen de inicio de su APK (su ciudad detras de Jake y
            # Tricky), con la misma disposicion que la de la 1.5.2
            sp15 = spc
            print('  pantalla de carga de la ciudad (splashScreen de su APK)')
        if sp15 is not None:
            # pantalla de carga de la 1.5.2: la imagen de inicio es vertical (9:16, guardada en 1024x1024); para la
            # PSP se toma una franja apaisada de la parte de arriba (la libreta con el consejo tapa la de abajo).
            # 512x256 en 5650 = 256 KB, solo mientras se carga
            im = sp15.convert('RGB').resize((576, 1024), Image.LANCZOS)
            y0 = int(1024 * 0.20)
            ex.textures.append(('generada', 'ui_splash', im.crop((0, y0, 576, y0 + 326)).resize((512, 256), Image.LANCZOS)))
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


# ---------------------------------------------------------------- tablas (Hoverboards.cs de la 1.5.2)
# boardType -> nombre, precio, desbloqueo (0 siempre, 1 gratis, 2 monedas), efecto (1 super salto, 2 va agachado);
# en el orden de la lista de la tienda (Hoverboards.boardOrder). El de la malla es el campo boardModelName
BOARDS_15 = [(3, 'Freestyler', 45000, 2, 0), (1, 'Bouncer', 280000, 2, 1), (0, 'Hoverboard', 0, 0, 0),
             (2, 'Lowrider', 320000, 2, 2), (6, 'Starboard', 0, 1, 0), (4, 'Big Kahuna', 65000, 2, 0),
             (5, 'Superhero', 8000, 2, 0)]
# la 1.6 anade 4 tablas (Hoverboards.cs de la 1.6), detras de las de la 1.5.2 para no cambiar las partidas guardadas.
# unlock 3: con monedas, pero solo se compra en su temporada (Liberty: "New York Special"; en el juego, del 2 al 31
# de enero, tras la Navidad); las demas siempre
BOARDS_16 = BOARDS_15 + [(7, 'Miami', 12000, 2, 0), (8, 'Monster', 30000, 2, 0), (9, 'Skull Fire', 75000, 2, 0),
                         (10, 'Liberty', 50000, 3, 0)]
# la 1.7: Daredevil (speedboard, 11), que acelera el juego mientras dura (effect 3), 85000 monedas como el original
# Toucan (12, "Rio Special": unlock 4, solo en la temporada de Rio) y Lumberjack (13, "Treehugger" en el codigo)
BOARDS_17 = BOARDS_16 + [(11, 'Daredevil', 85000, 2, 3), (12, 'Toucan', 50000, 4, 0), (13, 'Lumberjack', 4000, 2, 0)]
# la 1.8: Chicky (14, "Egg Hunt Special": unlock 6, se gana con 100 huevos de Pascua; "price" es el numero de huevos),
# Scoot (15, "rome" en el codigo) y Kick-Off (16, "Rome Special": unlock 5, solo en la temporada de Roma)
BOARDS_18 = BOARDS_17 + [(14, 'Chicky', 100, 6, 0), (15, 'Scoot', 35000, 2, 0), (16, 'Kick-Off', 50000, 5, 0)]
# la 1.9: Outback (17, "theOutback": "Sydney Special", unlock 7, solo en la temporada de Sidney) y Great White (18,
# "greatWhiteWakeboard", 20000 siempre)
BOARDS_19 = BOARDS_18 + [(17, 'Outback', 50000, 7, 0), (18, 'Great White', 20000, 2, 0)]
# la 1.10: Cherry (19, "Item Hunt": unlock 9, premio de la cuarta semana de la caza semanal; "price" son munecas) y
# Kitty (20, "Fortune": 50000, "Tokyo Special", unlock 8)
BOARDS_110 = BOARDS_19 + [(19, 'Cherry', 60, 9, 0), (20, 'Kitty', 50000, 8, 0)]
BOARD_ACTIONS = ('runAnimations', 'landAnimations', 'jumpAnimations', 'hangtimeAnimations', 'rollAnimations',
                 'dodgeLeftAnimations', 'dodgeRightAnimations', 'grindAnimations', 'grindLandAnimations',
                 'getOnBoardAnimations')


def export_boards(sc15, ex, clip_names10, table=None):
    """Las 7 tablas de la 1.5.2 (Hoverboard.hoverboardSelector): malla y textura de cada una (se leen solo mientras
    se usan), sus animaciones del personaje para correr, saltar, rodar... (HoverboardRendering) y boards.bin, la
    tabla que lee el juego. Las animaciones que ya estan en la 1.0 (h_run, h_jump...) no se repiten."""
    from types import SimpleNamespace
    hb = None
    for f in sc15.files.values():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            try:
                cl, fl = sc15.read_mono(o)
            except Exception:
                continue
            if cl == 'Hoverboard' and fl and fl.get('hoverboardSelector'):
                hb = (o, fl)
                break
        if hb:
            break
    if hb is None:
        print('  AVISO: la 1.5.2 no trae las tablas')
        return None
    obj, fl = hb
    prefabs = {s['boardType']: sc15.resolve_ptr(obj, s['hoverboardPrefab']) for s in fl['hoverboardSelector']}
    meshes, gen, new_clips = [], [], []
    props = None
    rows = []
    thumb_src = []      # (nombre del sprite, malla) para las miniaturas
    for btype, name, price, unlock, effect in (table or BOARDS_15):
        go = prefabs.get(btype)
        if go is None:
            continue
        g = sc15.go_tree(go)
        mf = next((c for c in g['comps'] if c['type'] == 'MeshFilter' and c.get('data')), None)
        mr = next((c for c in g['comps'] if c['type'] == 'MeshRenderer' and c.get('data')), None)
        hr = next((c for c in g['comps'] if c.get('class') == 'HoverboardRendering'), None)
        mesh_name = ''
        if mf is not None:
            mo_t = sc15.resolve_ptr(mf['obj'], mf['data']['m_Mesh'])
            if mo_t is not None:
                thumb_src.append(('board_%d' % len(rows), mo_t))
        if btype != 0 and mf is not None:       # la normal es la de la 1.0 (misma malla y textura main)
            mo = sc15.resolve_ptr(mf['obj'], mf['data']['m_Mesh'])
            if mo is not None:
                meshes.append(mo)
                mesh_name = mo.read().m_Name
            if props is None and mr is not None:
                mat = sc15.resolve_ptr(mr['obj'], mr['data']['m_Materials'][0])
                for _, t in mat.read_typetree()['m_SavedProperties']['m_TexEnvs']:
                    to = sc15.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                    if to is not None:
                        props = to.read()
                        break
        acts = []
        for a in BOARD_ACTIONS:
            names = []
            for pair in (hr['fields'].get(a) or []) if hr else []:
                c = sc15.resolve_ptr(hr['obj'], pair['characterAnimationClip'])
                if c is None:
                    continue
                cn = c.read().m_Name
                names.append(cn)
                if cn not in clip_names10 and cn not in [x[1] for x in new_clips]:
                    new_clips.append((c, cn))
            acts.append(names)
        rows.append((name, mesh_name, price, unlock, effect, acts))
    # las animaciones nuevas van aparte (boardanim.bin): el juego carga solo las de la tabla elegida
    thumbs = None
    if props is not None:
        gen.append(('gen', 'board_props', tex_rgba(props).resize((256, 256), Image.LANCZOS), True))
        # miniaturas para la pantalla de tablas (como las de personajes): de pie e inclinadas, 64x64 cada una
        texa = np.asarray(tex_rgba(props).convert('RGBA'))
        sheet = Image.new('RGBA', (256, 128 if len(thumb_src) <= 8 else 256), (0, 0, 0, 0))   # 4 por fila
        # con mas de 16 (la 1.8 trae 17) van a 51x51, 5 por fila: la misma hoja de 256x256 (el juego las escala a 64)
        cell = 64 if len(thumb_src) <= 16 else 51
        cols = 256 // cell
        sprites = []
        rot = np.eye(4)
        rot[:3, :3] = quat_mat(np.array(euler_quat(-25.0, 0.0, 62.0)))
        for k, (sname, mo) in enumerate(thumb_src):
            h = MeshHandler(mo.read())
            h.process()
            V = np.array(h.m_Vertices, np.float64).reshape(-1, 3)
            Wv = (rot @ np.c_[V, np.ones(len(V))].T).T[:, :3]
            tris = np.array([t for sm in mesh_triangles(mo, h) for t in sm], np.int64)
            parts = [(Wv, np.array(h.m_UV0, np.float64).reshape(-1, 2), tris, texa)]
            im = draw_parts(parts, cell, (Wv.min(axis=0), Wv.max(axis=0)))
            x, y = (k % cols) * cell, (k // cols) * cell
            sheet.paste(im, (x, y))
            r_ = {'x': x, 'y': y, 'width': cell, 'height': cell}
            sprites.append({'name': sname, 'outer': r_, 'inner': dict(r_)})
        thumbs = (sheet, sprites)
    out = bytearray(b'SSB1') + struct.pack('<I', len(rows))
    for name, mesh_name, price, unlock, effect, acts in rows:
        out += name.encode()[:31].ljust(32, b'\0') + mesh_name.encode()[:31].ljust(32, b'\0')
        out += struct.pack('<iII', price, unlock, effect)
        for names in acts:
            out += struct.pack('<I', len(names))
            for n in names:
                out += n.encode()[:31].ljust(32, b'\0')
    print('  tablas: %s (%d animaciones nuevas)' % (', '.join(r[0] for r in rows), len(new_clips)))
    return SimpleNamespace(meshes=meshes, gen=gen, table=bytes(out), clips=[c for c, _ in new_clips], thumbs=thumbs)


def export_extras_only(outdir, hq, extra):
    """Sin la 1.4: chars14.bin (mallas que se leen al usarlas) y hw_tex.bin (texturas nuevas) solo con lo de la 1.5.2."""
    from types import SimpleNamespace
    cbin, cstats = export_meshes(SimpleNamespace(meshes=extra.meshes, mesh_world=[False] * len(extra.meshes)))
    with open(os.path.join(outdir, 'chars14.bin'), 'wb') as f:
        f.write(cbin)
    tbin = export_textures(SimpleNamespace(textures=extra.gen), hq=hq)
    with open(os.path.join(outdir, 'hw_tex.bin'), 'wb') as f:
        f.write(tbin)
    print('  chars14.bin %d KB, hw_tex.bin %d KB (solo contenido de la 1.5.2)' % (len(cbin) // 1024, len(tbin) // 1024))


def export_halloween(sc, outdir, ex, hq, extra=None):
    """ThemeAssets de la 1.4: texturas y mallas del tema "halloween" con los mismos nombres que en la 1.0.
    extra: contenido de la 1.5.2 (tablas) que va en los mismos archivos."""
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
    if extra is not None:
        cmeshes = cmeshes + extra.meshes
        gen = gen + extra.gen
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


XM_TEXTURES = ('trains', 'train_start', 'environment', 'enemies', 'props')


def export_xmas(sc15, outdir, ex, hq):
    """ThemeAssets de la 1.5.2, tema "xmas" (Navidad): texturas de recambio con el mismo nombre y tamano que las del
    mundo, y las mallas navidenas del escenario, el perro y el guardia. Mismo formato que hw_tex.bin / hw_mesh.bin
    (xm_tex.bin / xm_mesh.bin) en la carpeta de este mundo. Devuelve el color de la niebla o None"""
    from types import SimpleNamespace
    themes = None
    for f in sc15.files.values():
        for o in f.objects.values():
            if o.type.name != 'MonoBehaviour':
                continue
            try:
                scr = sc15.script_of(o)
            except Exception:
                continue
            if scr and scr[2] == 'ThemeAssets':
                themes = (o, sc15.read_mono(o)[1])
                break
        if themes:
            break
    if not themes or not themes[1]:
        return None
    tobj, td = themes
    by_theme = {a['theme']: a for a in td['assets']}
    if 'xmas' not in by_theme or 'normal' not in by_theme:
        print('  AVISO: sin tema de Navidad en ThemeAssets')
        return None

    def tex_map(a):
        r = {}
        for tm in a['textureAndMaterials']:
            o = sc15.resolve_ptr(tobj, tm['texture'])
            if o is not None:
                d = o.read()
                r.setdefault(d.m_Name, d)
        return r
    nrm, xm = tex_map(by_theme['normal']), tex_map(by_theme['xmas'])
    cur = {}
    for to in ex.textures:
        if not isinstance(to, tuple):
            d = to.read()
            cur.setdefault(d.m_Name, d)
    gen = []
    for name in XM_TEXTURES:
        if name not in cur or name not in nrm or name not in xm:
            print('  AVISO: Navidad: falta la textura %s' % name)
            continue
        a = np.asarray(tex_rgba(cur[name])).astype(np.int16)
        n = np.asarray(tex_rgba(nrm[name])).astype(np.int16)
        h = np.asarray(tex_rgba(xm[name]))
        if a.shape != n.shape or a.shape != h.shape:
            print('  AVISO: Navidad: %s no tiene el mismo tamano' % name)
            continue
        # como en Halloween: donde la del mundo no es la normal de la 1.5.2 (hueco reutilizado) se deja la del mundo
        diff = np.abs(a[:, :, :3] - n[:, :, :3]).sum(axis=2) > 30
        out = np.where(diff[:, :, None], a.astype(np.uint8), h)
        print('  Navidad: textura %-12s %.1f %% de pixeles se quedan como estaban' % (name, diff.mean() * 100))
        gen.append(('gen', name, Image.fromarray(out, 'RGBA'), name not in hq))
    meshes, mworld, mnames = [], [], []
    names = {}
    for i, m in enumerate(ex.meshes):
        names.setdefault(m.read().m_Name, ex.mesh_world[i])
    a = by_theme['xmas']
    for kind in ('environmentModels', 'characterModels'):
      for p in a[kind]:
        go = sc15.resolve_ptr(tobj, p)
        if go is None:
            continue
        stack = [sc15.go_tree(go)]
        while stack:
            g = stack.pop()
            stack += g['children']
            for c in g['comps']:
                dd = c.get('data')
                mp = dd.get('m_Mesh') if isinstance(dd, dict) else None
                if not mp or not mp[2]:
                    continue
                mo = sc15.resolve_ptr(c['obj'], mp)
                nm = mo.read().m_Name if mo is not None else ''
                if not nm.startswith('xmas_') or mo in meshes:
                    continue
                base = nm[len('xmas_'):]
                # el cielo (xmas_skyPlane) lee el degradado morado del atlas environment de Navidad: el juego le pone
                # esa textura al material del cielo mientras dura el tema (ThemeAssets: environment -> sky)
                # el escenario de la 1.5.2 se llama "normal_X" y su version de Navidad "xmas_X" (tuneles, estacion,
                # carreteras, bordes del barranco, puente, edificios...): con otras coordenadas de textura, hechas
                # para el atlas de Navidad. Sin ellas el escenario normal leia el atlas nuevo en sitios que cambiaron
                # (lineas negras y blancas en el barranco, el puente, las paredes rojas y el suelo). Se guardan con el
                # nombre de su pareja (el juego las empareja por nombre) y como escenario si ella lo es (subdivididas
                # igual)
                tgt = 'normal_' + base if 'normal_' + base in names else base
                if tgt not in names:
                    print('  AVISO: Navidad: la malla %s no tiene pareja en el mundo' % nm)
                    continue
                meshes.append(mo)
                mworld.append(bool(names[tgt]))
                mnames.append(tgt)
    hm = SimpleNamespace(meshes=meshes, mesh_world=mworld, mesh_names=mnames,
                         adaptive_parts=getattr(ex, 'adaptive_parts', False))
    mbin, stats = export_meshes(hm)
    tbin = export_textures(SimpleNamespace(textures=gen), hq=hq)
    for fn, b in (('xm_tex.bin', tbin), ('xm_mesh.bin', mbin)):
        with open(os.path.join(outdir, fn), 'wb') as f:
            f.write(b)
        print('  %-11s %8d KB' % (fn, len(b) // 1024))
    print('  Navidad: mallas %s' % (', '.join(s_[0] for s_ in stats) or '(ninguna)'))
    c = a.get('fogColor')
    return (c['r'], c['g'], c['b']) if c else None


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


def egg_template(sc18):
    """El huevo de Pascua de la 1.8 (easterEgg: Pickup, EasterEggPickup, PickupRotate, su malla y el brillo) tal
    como esta en uno de sus SpawnPointEgg del Level."""
    for r in sc18.roots('level0'):
        g = sc18.go_tree(r)
        if g['name'] != 'Level':
            continue
        st = [g]
        while st:
            n = st.pop()
            st += n['children']
            if n['name'] == 'easterEgg' and any(c.get('class') == 'EasterEggPickup' for c in n['comps']):
                return n
    return None


def inject_hunts(ex, trees, level_sc, scs):
    """Cazas por fecha en todos los mapas: el huevo de Pascua de la 1.8 (temporada de Roma) y la muneca de la caza
    semanal de la 1.10 (temporada de Tokio). A cada SpawnPoint (power-ups) se le anaden de hijos una copia de cada
    uno; a los SpawnPointEgg de los mapas que los traen (Roma, Sidney, Tokio) la copia del que no traen. El juego
    saca uno u otro segun la caza que este activa. Sus materiales llevan solo el trozo de hoja que usan."""
    tpls = {}
    for kind, scx in scs.items():
        if scx is None:
            continue
        t = egg_template(scx)
        if t is None:
            continue
        zero = dict(t['tr'])
        zero['data'] = dict(t['tr']['data'])
        zero['data']['m_LocalPosition'] = {'x': 0.0, 'y': 0.0, 'z': 0.0}
        tpls[kind] = (t, zero)
    if not tpls:
        return
    native = None
    if level_sc is not None:
        nt = egg_template(level_sc)
        if nt is not None:
            native = 'doll' if level_sc is scs.get('doll') else 'egg'
    cnt = 0
    for t in trees:
        if t['name'] != 'Level':
            continue
        st = [t]
        while st:
            g = st.pop()
            st += g['children']
            cls = {c.get('class') for c in g['comps']}
            if 'SpawnPoint' in cls or 'SpawnPointEgg' in cls:
                if g.get('hunt'):
                    continue
                # los mapas que traen sus SpawnPointEgg (Roma, Sidney, Tokio: 33 frente a 36 de power-ups) solo los
                # usan a ellos, como el original; antes se anadian tambien en los de power-ups y salian el doble
                if native is not None and 'SpawnPointEgg' not in cls:
                    continue
                hunt = {}
                for kind in ('egg', 'doll'):
                    if 'SpawnPointEgg' in cls and kind == native:
                        hunt[kind] = 'native'
                        continue
                    if kind not in tpls:
                        continue
                    tp, zero = tpls[kind]
                    cp = dict(tp)
                    # en un SpawnPointEgg, en el mismo sitio que su objeto (el hijo con EasterEggPickup)
                    sib = next((ch for ch in g['children']
                                if any(c.get('class') == 'EasterEggPickup' for c in ch['comps'])), None)
                    cp['tr'] = sib['tr'] if 'SpawnPointEgg' in cls and sib is not None else zero
                    cp['copy'] = True
                    hunt[kind] = len(g['children'])
                    g['children'] = g['children'] + [cp]
                g['hunt'] = hunt
                cnt += 1
    for kind, (tp, _) in tpls.items():
        if kind != native:
            egg_materials(ex, tp, (64, 128) if kind == 'egg' else (128, 256))
    print('  cazas por fecha: %s en %d puntos' % (', '.join('huevo' if k == 'egg' else 'muneca' for k in tpls), cnt))


def egg_materials(ex, tpl, maxsz=(64, 128)):
    """Los materiales del huevo (props y EFX de la 1.8) con solo el trozo de la hoja que usan sus mallas: textura
    pequena generada y escala/desplazamiento del material para que las UV de siempre caigan en ella."""
    st = [tpl]
    while st:
        g = st.pop()
        st += g['children']
        mf = next((c for c in g['comps'] if c['type'] == 'MeshFilter' and c.get('data')), None)
        mr = next((c for c in g['comps'] if c['type'] == 'MeshRenderer' and c.get('data')), None)
        if mf is None or mr is None:
            continue
        mo = ex.sc.resolve_ptr(mf['obj'], mf['data']['m_Mesh'])
        ma = ex.sc.resolve_ptr(mr['obj'], mr['data']['m_Materials'][0])
        if mo is None or ma is None or key(ma) in ex.mat_idx:
            continue
        h = MeshHandler(mo.read(check_read=False))
        h.process()
        uv = np.array(h.m_UV0, np.float64).reshape(-1, 2)
        tex = None
        for _, t in ma.read_typetree()['m_SavedProperties']['m_TexEnvs']:
            to = ex.sc.resolve(ma.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
            if to is not None:
                tex = to.read()
                break
        orig = ex.tex_id
        ex.tex_id = lambda o: -1
        mi = ex.mat_id(ma)
        ex.tex_id = orig
        if tex is None:
            continue
        img = tex_rgba(tex)
        W, H = img.size
        u0, v0 = uv.min(axis=0)
        u1, v1 = uv.max(axis=0)
        # un pixel de margen; en pixeles enteros
        x0, x1 = max(0, int(np.floor(u0 * W)) - 1), min(W, int(np.ceil(u1 * W)) + 1)
        y0, y1 = max(0, int(np.floor((1 - v1) * H)) - 1), min(H, int(np.ceil((1 - v0) * H)) + 1)
        crop = img.crop((x0, y0, x1, y1))
        cw = 1 << max(3, (crop.size[0] - 1).bit_length())
        chh = 1 << max(3, (crop.size[1] - 1).bit_length())
        cw, chh = min(cw, maxsz[0]), min(chh, maxsz[1])
        crop = crop.resize((cw, chh), Image.LANCZOS)
        ua, ub = x0 / W, x1 / W
        va, vb = 1 - y1 / H, 1 - y0 / H
        ex.textures.append(('gen', ('egg_' + ex.materials[mi]['name'])[:31], crop, True))
        m = ex.materials[mi]
        m['tex'] = len(ex.textures) - 1
        m['st'] = (1.0 / (ub - ua), 1.0 / (vb - va), -ua / (ub - ua), -va / (vb - va))
        print('  huevo: material %s, trozo %dx%d de %s' % (m['name'], cw, chh, tex.m_Name))


def add_guard_clips(ex, scx):
    """FollowingGuard de la 1.7: animaciones nuevas del inspector y el perro (esquivar a cada lado, rodar y el perro en
    el aire). Se anaden a sus Animation (guard_prefab / dog_prefab) en todos los mapas; mismo esqueleto."""
    want = {'guard_prefab': ['Guard_Roll', 'Guard_dodgeLeft', 'Guard_dodgeRight'],
            'dog_prefab': ['Dog_dodgeLeft', 'Dog_dodgeRight', 'Dog_hangtime']}
    found = {}
    for o in scx.env.objects:
        if o.type.name != 'AnimationClip':
            continue
        try:
            n = o.read(check_read=False).m_Name
        except Exception:
            continue
        if any(n in v for v in want.values()) and n not in found:
            found[n] = o
    got = []
    for go, names in want.items():
        ids = [ex.clip_id(found[n]) for n in names if n in found]
        if ids:
            ex.extra_anim[go] = ex.extra_anim.get(go, []) + ids
            got += [n for n in names if n in found]
    if got:
        print('  animaciones nuevas del inspector y el perro: %s' % ', '.join(got))


def add_jetpack_clips(ex, scx):
    """CharacterRendering.jetpackAnimations de la 1.8: con el jetpack el personaje vuela con Jetpack_forward o
    Jetpack_forward_2 y al cambiar de carril hace un giro (BarrelRoll) o se inclina (changeLane), al azar (hasta la 1.7
    era siempre jetpack_forward). Mismo esqueleto: se anaden al Animation del personaje (avatar_prefab) en todos los
    mapas; ~80 KB."""
    want = ['Jetpack_forward', 'Jetpack_forward_2', 'Jetpack_BarrelRoll_left', 'Jetpack_changeLane_left',
            'Jetpack_BarrelRoll_right', 'Jackpack_changeLane_Right']
    found = {}
    for o in scx.env.objects:
        if o.type.name != 'AnimationClip':
            continue
        try:
            n = o.read(check_read=False).m_Name
        except Exception:
            continue
        if n in want and n not in found:
            found[n] = o
    ids = [ex.clip_id(found[n]) for n in want if n in found]
    if ids:
        ex.extra_anim['avatar_prefab'] = ex.extra_anim.get('avatar_prefab', []) + ids
    print('  animaciones del jetpack de la 1.8: %s' % (', '.join(n for n in want if n in found) or 'no encontradas'))


def swap_guard16(ex, sc16):
    """Mapa de la 1.6: el inspector y el perro de Nueva York (policia de azul con placa y perro marron). Sus mallas
    (normal_polySurface9 / normal_dog) tienen los mismos huesos en el mismo orden que las de la 1.0, asi que se cambian
    la malla y la textura enemies manteniendo el esqueleto y las animaciones de siempre (como las de Navidad)."""
    want = {'polySurface9': 'normal_polySurface9', 'dog': 'normal_dog'}
    plain = {}   # la 1.9 (Sidney) los llama como la 1.0: polySurface9 y dog
    new_mesh, new_tex = {}, None
    for o in sc16.env.objects:
        if o.type.name not in ('Mesh', 'Texture2D'):
            continue
        try:
            # la 1.7 (Unity 4.0) trae alguna malla que UnityPy no lee entera: solo hace falta el nombre
            n = o.read(check_read=False).m_Name
        except Exception:
            continue
        if o.type.name == 'Mesh':
            for k, v in want.items():
                if n == v and k not in new_mesh:
                    new_mesh[k] = o
                elif n == k and k not in plain:
                    plain[k] = o
        elif new_tex is None and n == 'enemies':
            new_tex = o
    for k, o in plain.items():
        new_mesh.setdefault(k, o)
    ex.mesh_names = [m.read(check_read=False).m_Name for m in ex.meshes]
    done = []
    for i, m in enumerate(ex.meshes):
        nm = ex.mesh_names[i]
        if nm in new_mesh:
            ex.meshes[i] = new_mesh[nm]
            done.append(nm)
    if new_tex is not None:
        for i, t in enumerate(ex.textures):
            if not isinstance(t, tuple) and t.read().m_Name == 'enemies':
                ex.textures[i] = new_tex
                done.append('enemies')
    print('  inspector y perro de la 1.6: %s' % (', '.join(done) or 'AVISO: no encontrados'))


def export_set(sc, level_sc, sc14, outdir, classic=True, sc15=None, boards_ui=None, sc16=None, sc17=None, sc18=None,
               sc19=None, sc110=None):
    """Un conjunto completo de datos (world, mat, mesh, tex, anim, sfx, ui) con el nucleo del juego de `sc` (personaje,
    guardia, camaras, interfaz...) y el mundo (la raiz Level: tramos, intro, vias) de `level_sc`. classic: el de
    siempre, en data/, con Halloween y la musica; si no, otro mundo (data/w15: la 1.5.2) que el juego elige al arrancar."""
    os.makedirs(outdir, exist_ok=True)
    scl = [sc] + ([level_sc] if level_sc is not sc else []) + \
        ([sc18] if sc18 is not None and sc18 is not level_sc else []) + \
        ([sc110] if sc110 is not None and sc110 is not level_sc and sc110 is not sc18 else [])
    eggsrc = sc18 or sc19
    msc = scl[0] if len(scl) == 1 else MultiScene(scl)
    ex = Exporter(msc)
    ex.sc16 = sc16   # contenido de la 1.6 (Tony, tablas) en la interfaz del mundo clasico
    ex.sc17 = sc17   # y de la 1.7 (Carmen)
    ex.sc18 = sc18   # y de la 1.8 (Roberto)
    ex.sc19 = sc19   # y de la 1.9 (Kim)
    ex.sc110 = sc110 # y de la 1.10 (Harumi, la muneca de la caza semanal)
    ex.level_sc = level_sc
    trees = []
    for r in sc.roots('level0'):
        go = sc.go_tree(r)
        if go['name'] in EXCLUDE_ROOTS or (go['name'] == 'Level' and level_sc is not sc):
            continue
        trees.append(go)
    if level_sc is not sc:
        lv = [level_sc.go_tree(r) for r in level_sc.roots('level0')]
        trees += [g for g in lv if g['name'] == 'Level']
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
                    o = msc.resolve_ptr(c['obj'], p)
                    if o is not None and o.type.name == 'GameObject':
                        extra.append(o)
    seen = set()
    for o in extra:
        if key(o) in seen:
            continue
        seen.add(key(o))
        trees.append(msc.go_tree(o))
    # las animaciones nuevas del inspector y el perro de la 1.7 en todos los mapas (mismo esqueleto en todas las
    # versiones; solo cambia que animacion suena)
    if sc17 is not None or sc18 is not None:
        add_guard_clips(ex, sc17 or sc18)
    # y las del jetpack de la 1.8 (giro y cambio de carril), tambien en todos los mapas
    if sc18 is not None or sc19 is not None or sc110 is not None:
        add_jetpack_clips(ex, sc18 or sc19 or sc110)
    # busqueda de huevos de la 1.8 (por fecha, en todos los mapas): el de Roma ya trae sus SpawnPointEgg
    # (la 1.8, la 1.9 y la 1.10 traen sus propios puntos; a los demas mapas se les anaden en los de power-ups)
    if eggsrc is not None or sc110 is not None:
        native = level_sc if level_sc in (sc18, sc19, sc110) and level_sc is not sc else None
        inject_hunts(ex, trees, native, {'egg': eggsrc, 'doll': sc110})
    for t in trees:
        ex.add_tree(t)
    print('  %d nodos' % len(ex.nodes))
    world = ex.build_world()
    # recoger tambien los clips de audio referenciados por los MonoBehaviour (AudioClipInfo, etc.)
    for n in ex.nodes:
        for c in n['go']['comps']:
            if c['type'] == 'AudioSource' and c.get('data'):
                ex.audio_id(msc.resolve_ptr(c['obj'], c['data']['m_audioClip']))
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
                        o = msc.resolve_ptr(c['obj'], v)
                        if o is not None and o.type.name == 'AudioClip':
                            ex.audio_id(o)
                    else:
                        stack += v
                elif isinstance(v, tuple) and len(v) == 3 and v[0] == 'ptr':
                    o = msc.resolve_ptr(c['obj'], v)
                    if o is not None and o.type.name == 'AudioClip':
                        ex.audio_id(o)
    # Save Me de la 1.9 (mapa de Sidney): el sonido de revivir (AudioStateLoop.reviveSound de su escena)
    scr = level_sc if level_sc is not sc and level_sc in (sc19, sc110) else None
    if scr is not None:
        for r in scr.roots('level0'):
            st = [scr.go_tree(r)]
            while st:
                g = st.pop()
                st += g['children']
                for c in g['comps']:
                    if c.get('class') == 'AudioStateLoop' and c.get('fields') and c['fields'].get('reviveSound'):
                        o = scr.resolve_ptr(c['obj'], c['fields']['reviveSound'])
                        if o is not None:
                            ex.audio_id(o)
                            print('  sonido de revivir (Save Me): %s' % o.read(check_read=False).m_Name)
    # el resto de clips de audio del juego (por si acaso)
    for o in sc.env.objects:
        if o.type.name == 'AudioClip':
            ex.audio_id(o)
    boards = None
    if classic and sc15 is not None:
        print('Exportando las tablas de la 1.5.2...')
        names10 = set()
        for c in ex.clips:
            if not isinstance(c, tuple):
                names10.add(c.read_typetree()['m_Name'])
        # con la 1.6 todas las tablas salen de ella (la misma hoja de texturas para las 11)
        # con la 1.7 (en pruebas) las tablas salen de ella: trae las de la 1.6 y Daredevil
        # con la 1.8 salen de ella: trae las 14 anteriores, Chicky, Scoot y Kick-Off
        # con la 1.9, de ella: trae las 17 anteriores, Outback y Great White
        bsrc = sc110 or sc19 or sc18 or sc17 or sc16 or sc15
        boards = export_boards(bsrc, ex, names10, BOARDS_110 if sc110 is not None else
                               BOARDS_19 if sc19 is not None else BOARDS_18 if sc18 is not None
                               else BOARDS_17 if sc17 is not None else BOARDS_16 if sc16 is not None else BOARDS_15)
        # Tony (Characters.cs de la 1.6: 95000 monedas, "New York Special") y Carmen (la 1.7: 95000, "Rio Special"):
        # mismo esqueleto que Jake, a chars14.bin como los de la 1.4
        extra_chars = []
        if sc16 is not None or sc17 is not None or sc18 is not None:
            extra_chars.append((sc16 or sc17 or sc18, 'tony', 'avatar_tony', '1.6'))
        if sc17 is not None or sc18 is not None:
            extra_chars.append((sc17 or sc18, 'carmen', 'avatar_carmen', '1.7'))
        # Roberto (Characters.cs de la 1.8: 95000 monedas, "Rome Special")
        if sc18 is not None or sc19 is not None:
            extra_chars.append((sc18 or sc19, 'roberto', 'avatar_roberto', '1.8'))
        # Kim (la 1.9: 95000, "Sydney Special")
        if sc19 is not None or sc110 is not None:
            extra_chars.append((sc19 or sc110, 'kim', 'avatar_kim', '1.9'))
        # Harumi (la 1.10: 95000, "Tokyo Special")
        if sc110 is not None:
            extra_chars.append((sc110, 'harumi', 'avatar_harumi', '1.10'))
        for scx, mname, tname, ver in (extra_chars if boards is not None else []):
            for o in scx.env.objects:
                if o.type.name != 'SkinnedMeshRenderer':
                    continue
                try:
                    d = o.read_typetree()
                except Exception:
                    continue
                mo = scx.resolve(o.assets_file, d['m_Mesh']['m_FileID'], d['m_Mesh']['m_PathID'])
                if mo is None or mo.read(check_read=False).m_Name != mname:
                    continue
                mat = scx.resolve(o.assets_file, d['m_Materials'][0]['m_FileID'], d['m_Materials'][0]['m_PathID'])
                tex = None
                for _, t in (mat.read_typetree()['m_SavedProperties']['m_TexEnvs'] if mat else []):
                    tt = scx.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                    if tt is not None:
                        tex = tt.read()
                        break
                if tex is not None:
                    boards.meshes.append(mo)
                    boards.gen.append(('gen', tname, tex_rgba(tex), True))
                    print('  personaje de la %s: %s' % (ver, mname))
                break
        if boards is not None:
            # Elf Tricky (Characters.cs de la 1.5.2: 95000 monedas, solo en Navidad): con los personajes de la 1.4
            # en chars14.bin (mismo esqueleto de 18 huesos); se lee solo mientras se usa
            for o in sc15.env.objects:
                if o.type.name != 'SkinnedMeshRenderer':
                    continue
                d = o.read_typetree()
                mo = sc15.resolve(o.assets_file, d['m_Mesh']['m_FileID'], d['m_Mesh']['m_PathID'])
                if mo is None or mo.read().m_Name != 'xmasTricky':
                    continue
                mat = sc15.resolve(o.assets_file, d['m_Materials'][0]['m_FileID'], d['m_Materials'][0]['m_PathID'])
                tex = None
                for _, t in (mat.read_typetree()['m_SavedProperties']['m_TexEnvs'] if mat else []):
                    tt = sc15.resolve(mat.assets_file, t['m_Texture']['m_FileID'], t['m_Texture']['m_PathID'])
                    if tt is not None:
                        tex = tt.read()
                        break
                if tex is not None:
                    boards.meshes.append(mo)
                    boards.gen.append(('gen', 'avatar_trickyXmas', tex_rgba(tex), True))
                    print('  personaje de Navidad: Elf Tricky')
                break
    print('Exportando interfaz...')
    # los otros mundos (data/w15) tienen su propio ui.bin: tambien llevan las miniaturas de las tablas
    # los mapas de ciudad (Nueva York, Rio, Roma...) llevan la pantalla de carga de su APK; el de la 1.5.2 y el
    # clasico, la de la 1.5.2
    city = level_sc if (not classic and level_sc is not sc and sc16 is level_sc) else None
    ui = export_ui(sc, ex, sc14, boards if boards is not None else boards_ui,
                   sc15 if sc15 is not None else (level_sc if level_sc is not sc else None), city_sc=city)
    print('Exportando mallas...')
    ex.adaptive_parts = not classic   # solo el mapa de la 1.5.2 (el clasico como antes)
    if sc16 is not None and level_sc is sc16:
        swap_guard16(ex, sc16)
    mesh, stats = export_meshes(ex)
    tv = sum(s[1] for s in stats)
    tt = sum(s[2] for s in stats)
    print('  %d mallas, %d vertices, %d triangulos' % (len(stats), tv, tt))
    fix_coin_glow(ex)
    print('Exportando texturas...')
    # mapa de la 1.5.2: trains y main con paleta (8 bits) como environment y props: 256 KB en vez de 512, y asi caben
    # las cuatro en la VRAM (el GE las lee mucho mas deprisa que de la RAM). Error medio de color 3,8/255 (con 16 bits
    # 1,8). El clasico (1.0/1.4): main de 16 bits y enemies a su tamano
    global SMALL_TEX_ON
    SMALL_TEX_ON = not classic
    # mapa clasico: main (su escenario) sigue en 16 bits; los trenes con paleta para que quepan los dos en la VRAM con
    # la tercera imagen (960 KB)
    hq = ('main', 'FontAtlas') if classic else ('FontAtlas',)
    tex = export_textures(ex, hq=hq)
    print('Exportando animaciones...')
    anim = export_anims(ex)
    print('  %d clips' % len(ex.clips))
    boardanim = None
    if boards is not None and boards.clips:
        from types import SimpleNamespace
        # como anim.bin, pero con sus propias cadenas: el mismo archivo sirve para los dos mundos
        boardanim = export_anims(SimpleNamespace(clips=boards.clips, strings=None), local_strings=True)
        print('  boardanim.bin: %d animaciones de las tablas, %d KB (se cargan solo las de la elegida)' %
              (len(boards.clips), len(boardanim) // 1024))
    print('Exportando audio...')
    sfx, names = export_audio(ex, outdir, write_files=classic)
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
    for fn, b in (('world.bin', world), ('mat.bin', bytes(mat)), ('mesh.bin', mesh), ('tex.bin', tex),
                  ('anim.bin', anim), ('sfx.bin', sfx), ('ui.bin', ui)):
        with open(os.path.join(outdir, fn), 'wb') as f:
            f.write(b)
        print('  %-10s %8d KB' % (fn, len(b) // 1024))
    if getattr(ex, 'fx_bin', None) is not None:
        with open(os.path.join(outdir, 'fx.bin'), 'wb') as f:
            f.write(ex.fx_bin)
    with open(os.path.join(outdir, 'meshes.txt'), 'w') as f:
        for i, s_ in enumerate(stats):
            f.write('%d %s %d %d %d\n' % (i, *s_))
    if classic:
        for fn in ('hw_tex.bin', 'hw_mesh.bin', 'chars14.bin', 'boards.bin', 'boardanim.bin'):
            if os.path.exists(os.path.join(outdir, fn)):
                os.remove(os.path.join(outdir, fn))
        if boards is not None:
            with open(os.path.join(outdir, 'boards.bin'), 'wb') as f:
                f.write(boards.table)
            if boardanim is not None:
                with open(os.path.join(outdir, 'boardanim.bin'), 'wb') as f:
                    f.write(boardanim)
        if sc14 is not None:
            export_halloween(sc14, outdir, ex, hq, boards)
        elif boards is not None:
            export_extras_only(outdir, hq, boards)
    if not classic and level_sc is not sc:
        # mundo de la 1.5.2: su Navidad (ThemeAssets "xmas") va en esta carpeta
        for fn in ('xm_tex.bin', 'xm_mesh.bin'):
            if os.path.exists(os.path.join(outdir, fn)):
                os.remove(os.path.join(outdir, fn))
        export_xmas(level_sc, outdir, ex, hq)
    ex.boards = boards
    return ex


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    apk, outdir = sys.argv[1], sys.argv[2]

    def opt(*names):
        for o in names:
            if o in sys.argv:
                i = sys.argv.index(o)
                return sys.argv[i + 1] if i + 1 < len(sys.argv) else None
        return None
    apk14, apk15, apk16 = opt('--apk14', '--halloween'), opt('--apk15', '--world'), opt('--apk16')
    apk17, apk18, apk19, apk110 = opt('--apk17'), opt('--apk18'), opt('--apk19'), opt('--apk110')
    os.makedirs(outdir, exist_ok=True)
    print('Descomprimiendo el APK...')
    data_dir = unpack_apk(apk)
    export_eboot_art(data_dir, outdir)
    print('Cargando la escena de Unity...')
    sc = Scene(data_dir)
    sc14 = None
    if apk14:
        print('Descomprimiendo el APK de la 1.4...')
        sc14 = Scene(unpack_apk(apk14))
    sc15 = None
    if apk15:
        print('Descomprimiendo el APK de la 1.5.2...')
        sc15 = Scene(unpack_apk(apk15))
    sc16 = None
    if apk16:
        print('Descomprimiendo el APK de la 1.6...')
        sc16 = Scene(unpack_apk(apk16))
    sc17 = None
    if apk17:
        print('Descomprimiendo el APK de la 1.7 (en pruebas)...')
        sc17 = Scene(unpack_apk(apk17))
    sc18 = None
    if apk18:
        print('Descomprimiendo el APK de la 1.8...')
        sc18 = Scene(unpack_apk(apk18))
    sc19 = None
    if apk19:
        print('Descomprimiendo el APK de la 1.9...')
        sc19 = Scene(unpack_apk(apk19))
    sc110 = None
    if apk110:
        print('Descomprimiendo el APK de la 1.10...')
        sc110 = Scene(unpack_apk(apk110))
    print('== Mundo clasico (data/)')
    # el contenido de las versiones nuevas (tablas, Elf Tricky, Tony) va con el clasico: sirve para todos los mapas
    exc = export_set(sc, sc, sc14, outdir, classic=True, sc15=sc15, sc16=sc16, sc17=sc17, sc18=sc18, sc19=sc19, sc110=sc110)
    w15 = os.path.join(outdir, 'w15')
    if sc15 is not None and os.environ.get('WORLD15', '0') != '0':
        print('== Mundo de la 1.5.2 (data/w15/, en pruebas)')
        export_set(sc, sc15, sc14, w15, classic=False, boards_ui=exc.boards, sc16=sc16, sc17=sc17, sc18=sc18, sc19=sc19, sc110=sc110)
    elif os.path.isdir(w15):
        shutil.rmtree(w15, True)
    # mapa de la 1.6 (data/w16/): un mapa aparte, con sus propias mallas y texturas (en pruebas). Las tablas y Elf
    # Tricky siguen saliendo de la 1.5.2
    w16 = os.path.join(outdir, 'w16')
    if sc16 is not None and os.environ.get('WORLD16', '1') != '0':
        print('== Mapa de la 1.6 (data/w16/, en pruebas)')
        export_set(sc, sc16, sc14, w16, classic=False, boards_ui=exc.boards, sc16=sc16, sc17=sc17, sc18=sc18, sc19=sc19, sc110=sc110)
    elif os.path.isdir(w16):
        shutil.rmtree(w16, True)
    # mapa de la 1.7 (data/w17/, EN PRUEBAS: Rio de Janeiro, se juega con WORLD17=1). Mismo trazado que el de la 1.6;
    # su inspector y su perro (de Rio) se cambian igual que los de Nueva York
    w17 = os.path.join(outdir, 'w17')
    if sc17 is not None:
        print('== Mapa de la 1.7 (data/w17/, en pruebas)')
        # la interfaz (libreta, ME, SHOP, manchas...) de la 1.5.2: la de la 1.7 es de Unity 4 y no se lee entera
        export_set(sc, sc17, sc14, w17, classic=False, sc15=sc15, boards_ui=exc.boards, sc16=sc17, sc17=sc17,
                   sc18=sc18, sc19=sc19, sc110=sc110)
    elif os.path.isdir(w17):
        shutil.rmtree(w17, True)
    # mapa de la 1.8 (data/w18/: Roma, del 28 de febrero al 3 de abril; WORLD18=1 lo deja siempre). Mismo trazado;
    # su inspector y su perro se cambian igual; trae sus propios puntos de huevos (SpawnPointEgg)
    w18 = os.path.join(outdir, 'w18')
    if sc18 is not None:
        print('== Mapa de la 1.8 (data/w18/)')
        export_set(sc, sc18, sc14, w18, classic=False, sc15=sc15, boards_ui=exc.boards, sc16=sc18,
                   sc17=sc17 or sc18, sc18=sc18, sc19=sc19, sc110=sc110)
    elif os.path.isdir(w18):
        shutil.rmtree(w18, True)
    # mapa de la 1.9 (data/w19/: Sidney, del 4 de abril al 1 de mayo; WORLD19=1 lo deja siempre). Mismo trazado; su
    # inspector y su perro se cambian igual; trae sus puntos de huevos. Save Me (llaves) aun no
    w19 = os.path.join(outdir, 'w19')
    if sc19 is not None:
        print('== Mapa de la 1.9 (data/w19/)')
        export_set(sc, sc19, sc14, w19, classic=False, sc15=sc15, boards_ui=exc.boards, sc16=sc19,
                   sc17=sc17 or sc19, sc18=sc18, sc19=sc19, sc110=sc110)
    elif os.path.isdir(w19):
        shutil.rmtree(w19, True)
    # mapa de la 1.10 (data/w110/: Tokio, del 2 al 29 de mayo; WORLD110=1 lo deja siempre)
    w110 = os.path.join(outdir, 'w110')
    if sc110 is not None:
        print('== Mapa de la 1.10 (data/w110/)')
        export_set(sc, sc110, sc14, w110, classic=False, sc15=sc15, boards_ui=exc.boards, sc16=sc110,
                   sc17=sc17 or sc110, sc18=sc18, sc19=sc19, sc110=sc110)
    elif os.path.isdir(w110):
        shutil.rmtree(w110, True)
    print('Listo.')


if __name__ == '__main__':
    main()
