"""Lectura de la escena de Unity 3.5 del APK de Subway Surfers 1.0.

Carga los archivos serializados con UnityPy y lee los MonoBehaviour sin typetree
generando la estructura a partir de Assembly-CSharp.dll (como hace el propio Unity).
"""
import os

import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator
from UnityPy.helpers.TypeTreeNode import TypeTreeNode

TAGS = ['HitMovingTrain', 'JumpTrain', 'DodgeTrain', 'RollBarrier', 'JumpBarrier', 'HitBarrier', 'HitTrain',
        'Ground', 'Station', 'Subway', '', '', '', '', '', '', '', 'JumpHighBarrier', '']
BUILTIN_TAGS = {0: 'Untagged', 1: 'Respawn', 2: 'Finish', 3: 'EditorOnly', 5: 'MainCamera', 6: 'Player',
                7: 'GameController'}


def tag_name(t):
    if t >= 20000:
        return TAGS[t - 20000] if t - 20000 < len(TAGS) else 'Untagged'
    return BUILTIN_TAGS.get(t, 'Untagged')


def conv(v):
    """typetree -> tipos de Python simples; los PPtr quedan como ('ptr', fileID, pathID)."""
    if isinstance(v, dict):
        if set(v.keys()) == {'m_FileID', 'm_PathID'}:
            return ('ptr', v['m_FileID'], v['m_PathID'])
        return {k: conv(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [conv(x) for x in v]
    return v


class Scene:
    def __init__(self, data_dir):
        self.data_dir = data_dir
        self.env = UnityPy.load(data_dir)
        self.files = {}
        for k, f in self.env.files.items():
            if hasattr(f, 'objects'):
                self.files[os.path.basename(k)] = f
        self.gen = TypeTreeGenerator('3.5.5f3')
        self.gen.load_local_dll_folder(os.path.join(data_dir, 'Managed'))
        self._nodes_cache = {}
        self._script_cache = {}

    # ---- referencias entre archivos ----
    def resolve(self, af, fid, pid):
        """Devuelve el ObjectReader al que apunta (fileID, pathID) desde el archivo af."""
        if pid == 0:
            return None
        if fid == 0:
            tgt = af
        else:
            ext = af.externals[fid - 1]
            name = os.path.basename(ext.path.replace('\\', '/'))
            tgt = self.files.get(name)
            if tgt is None:
                return None
        return tgt.objects.get(pid)

    def resolve_ptr(self, obj, ptr):
        if ptr is None or ptr[2] == 0:
            return None
        return self.resolve(obj.assets_file, ptr[1], ptr[2])

    # ---- MonoBehaviour ----
    def _nodes35(self, asm, full):
        key = (asm, full)
        if key in self._nodes_cache:
            return self._nodes_cache[key]
        if not asm.endswith('.dll'):
            asm += '.dll'
        base = self.gen.get_nodes(asm, full)
        out = []
        skip = None
        for b in base:
            if skip is not None:
                if b.m_Level > skip:
                    continue
                skip = None
            if b.m_Name == 'm_RotationOrder':  # no existe en las AnimationCurve de Unity 3.5
                skip = b.m_Level
                continue
            mf = b.m_MetaFlag
            if b.m_Name == 'm_Enabled' and b.m_Level == 1:
                mf |= 0x4000
            out.append(TypeTreeNode(b.m_Level, b.m_Type, b.m_Name, 0, 0, m_MetaFlag=mf))
        n = TypeTreeNode.from_list(out)
        self._nodes_cache[key] = n
        return n

    def script_of(self, obj):
        r = obj.reader
        r.Position = obj.byte_start
        r.read_int(); r.read_int(); r.read_u_byte(); r.align_stream()
        fid = r.read_int(); pid = r.read_int()
        key = (id(obj.assets_file), fid, pid)
        if key not in self._script_cache:
            s = self.resolve(obj.assets_file, fid, pid)
            if s is None:
                self._script_cache[key] = None
            else:
                ms = s.read()
                self._script_cache[key] = (ms.m_AssemblyName, ms.m_Namespace, ms.m_ClassName)
        return self._script_cache[key]

    def read_mono(self, obj):
        sc = self.script_of(obj)
        if not sc:
            return None, None
        full = (sc[1] + '.' + sc[2]) if sc[1] else sc[2]
        try:
            return sc[2], conv(obj.read_typetree(self._nodes35(sc[0], full)))
        except Exception:
            return sc[2], None

    # ---- jerarquia de GameObjects ----
    def read_go(self, obj):
        """Lee un GameObject con todos sus componentes."""
        tt = obj.read_typetree()
        go = {'obj': obj, 'name': tt['m_Name'], 'active': bool(tt['m_IsActive']), 'layer': tt['m_Layer'],
              'tag': tag_name(tt['m_Tag']), 'comps': []}
        for c in tt['m_Component']:
            ptr = c[1] if isinstance(c, (tuple, list)) else c
            co = self.resolve(obj.assets_file, ptr['m_FileID'], ptr['m_PathID'])
            if co is None:
                continue
            tn = co.type.name
            d = {'type': tn, 'obj': co}
            if tn == 'MonoBehaviour':
                d['class'], d['fields'] = self.read_mono(co)
            else:
                try:
                    d['data'] = conv(co.read_typetree())
                except Exception as e:
                    d['data'] = None
                    d['err'] = repr(e)
            go['comps'].append(d)
            if tn == 'Transform':
                go['tr'] = d
        return go

    def go_tree(self, root_go_obj, out=None):
        """Recorre el arbol a partir de un GameObject; devuelve la lista en preorden con 'children'."""
        go = self.read_go(root_go_obj)
        go['children'] = []
        tr = go['tr']
        for ch in tr['data']['m_Children']:
            cto = self.resolve(tr['obj'].assets_file, ch[1], ch[2])
            if cto is None:
                continue
            ct = cto.read_typetree()
            gptr = ct['m_GameObject']
            gobj = self.resolve(cto.assets_file, gptr['m_FileID'], gptr['m_PathID'])
            if gobj is not None:
                go['children'].append(self.go_tree(gobj))
        return go

    def roots(self, fname):
        f = self.files[fname]
        res = []
        for o in f.objects.values():
            if o.type.name == 'Transform':
                t = o.read_typetree()
                if t['m_Father']['m_PathID'] == 0:
                    g = t['m_GameObject']
                    res.append(self.resolve(f, g['m_FileID'], g['m_PathID']))
        return res
