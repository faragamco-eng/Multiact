"""Access to the official KiCad 7 symbol and footprint libraries."""
import copy, math, os, subprocess
from sexpr import parse, find, first, Sym


class SymbolLib:
    def __init__(self, root, local=None):
        self.root = root
        self.local = local          # directory with project-local libraries (Custom.kicad_sym)
        self._libs = {}

    def _lib(self, lib):
        if lib not in self._libs:
            path = os.path.join(self.root, f"{lib}.kicad_sym")
            if self.local and os.path.exists(os.path.join(self.local, f"{lib}.kicad_sym")):
                path = os.path.join(self.local, f"{lib}.kicad_sym")
            self._libs[lib] = parse(open(path).read())[0] if os.path.exists(path) else None
        return self._libs[lib]

    def raw(self, lib, name):
        L = self._lib(lib)
        if L is None:
            return None
        for e in find(L, "symbol"):
            if e[1] == name:
                return e
        return None

    def flat(self, lib, name):
        """Symbol with 'extends' resolved, renamed 'lib:name' (as stored in a schematic)."""
        e = self.raw(lib, name)
        if e is None:
            return None
        ext = first(e, "extends")
        if ext:
            base = self.flat(lib, ext[1])
            if base is None:
                return None
            base = copy.deepcopy(base)
            base_name = base[1].split(":")[1]
            # child properties override the parent's
            child_props = {p[1]: p for p in find(e, "property")}
            out = [base[0], f"{lib}:{name}"]
            for x in base[2:]:
                if isinstance(x, list) and x and x[0] == "property" and x[1] in child_props:
                    out.append(copy.deepcopy(child_props.pop(x[1])))
                elif isinstance(x, list) and x and x[0] == "symbol":
                    y = copy.deepcopy(x)
                    y[1] = y[1].replace(base_name, name, 1)
                    out.append(y)
                else:
                    out.append(x)
            for p in child_props.values():
                out.insert(len([z for z in out if not isinstance(z, list) or z[0] != "symbol"]), copy.deepcopy(p))
            return out
        out = copy.deepcopy(e)
        out[1] = f"{lib}:{name}"
        return out

    def pins(self, lib, name):
        """list of dicts: number, name, type, x, y, angle, length, unit"""
        s = self.flat(lib, name)
        if s is None:
            return None
        res = []
        for sub in find(s, "symbol"):
            parts = sub[1].rsplit("_", 2)
            unit = int(parts[-2]) if len(parts) >= 3 and parts[-2].isdigit() else 0
            for p in find(sub, "pin"):
                at = first(p, "at")
                res.append(dict(number=first(p, "number")[1], name=first(p, "name")[1], type=str(p[1]),
                                x=float(at[1]), y=float(at[2]), angle=float(at[3]) if len(at) > 3 else 0.0,
                                length=float(first(p, "length")[1]), unit=unit,
                                hidden=any(x == "hide" for x in p if not isinstance(x, list))))
        return res

    def units(self, lib, name):
        s = self.flat(lib, name)
        us = set()
        for sub in find(s, "symbol"):
            parts = sub[1].rsplit("_", 2)
            if len(parts) >= 3 and parts[-2].isdigit() and int(parts[-2]) > 0:
                us.add(int(parts[-2]))
        return sorted(us) or [1]


class FootprintLib:
    def __init__(self, repo, local=None):
        self.repo = repo
        self.local = local          # directory with <Lib>.pretty folders (project-local)
        out = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "--name-only", "HEAD"],
                             capture_output=True, text=True).stdout
        self.names = set()
        for line in out.splitlines():
            if line.endswith(".kicad_mod") and "/" in line:
                lib, f = line.split("/", 1)
                self.names.add(lib.replace(".pretty", "") + ":" + f[:-10])
        if local:
            for d in os.listdir(local):
                if d.endswith(".pretty"):
                    for f in os.listdir(os.path.join(local, d)):
                        if f.endswith(".kicad_mod"):
                            self.names.add(d[:-7] + ":" + f[:-10])
        self._cache = {}

    def exists(self, fp):
        return fp in self.names

    def get(self, fp):
        if fp not in self._cache:
            lib, name = fp.split(":")
            lp = os.path.join(self.local or "", f"{lib}.pretty", f"{name}.kicad_mod")
            if self.local and os.path.exists(lp):
                self._cache[fp] = parse(open(lp).read())[0]
                return copy.deepcopy(self._cache[fp])
            r = subprocess.run(["git", "-C", self.repo, "show", f"HEAD:{lib}.pretty/{name}.kicad_mod"],
                               capture_output=True, text=True)
            self._cache[fp] = parse(r.stdout)[0] if r.stdout else None
        return copy.deepcopy(self._cache[fp])

    def pads(self, fp):
        f = self.get(fp)
        return {str(p[1]) for p in find(f, "pad")} if f else set()

    def bbox(self, fp):
        """courtyard bounding box (x0, y0, x1, y1) in footprint coordinates"""
        f = self.get(fp)
        xs, ys = [], []
        for kind in ("fp_line", "fp_rect", "fp_circle", "fp_poly", "fp_arc"):
            for g in find(f, kind):
                layer = first(g, "layer")
                if not layer or layer[1] not in ("F.CrtYd", "B.CrtYd"):
                    continue
                if kind == "fp_circle":
                    c, e = first(g, "center"), first(g, "end")
                    r = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
                    xs += [float(c[1]) - r, float(c[1]) + r]; ys += [float(c[2]) - r, float(c[2]) + r]
                    continue
                for key in ("start", "end", "mid"):
                    v = first(g, key)
                    if v: xs.append(float(v[1])); ys.append(float(v[2]))
                pts = first(g, "pts")
                if pts:
                    for xy in find(pts, "xy"):
                        xs.append(float(xy[1])); ys.append(float(xy[2]))
        if not xs:
            for p in find(f, "pad"):
                at, size = first(p, "at"), first(p, "size")
                if at and size:
                    xs += [float(at[1]) - float(size[1]) / 2, float(at[1]) + float(size[1]) / 2]
                    ys += [float(at[2]) - float(size[2]) / 2, float(at[2]) + float(size[2]) / 2]
        if not xs:
            return (-1, -1, 1, 1)
        return (min(xs), min(ys), max(xs), max(ys))
