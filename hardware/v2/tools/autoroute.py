#!/usr/bin/env python3
"""
autoroute.py - two-layer grid router for the generated KiCad boards.

    python3 autoroute.py IN.kicad_pcb OUT.kicad_pcb [--pour GND] [--skip NET ...]

* grid 0.05 mm on F.Cu / B.Cu, 8 directions + vias, A* with via and bend cost
* exact clearance: every copper item (real pad polygons from KiCad, tracks,
  vias, NPTH holes) contributes a distance field; a cell is usable by net N
  when the nearest copper of ANOTHER net is >= clearance + half width;
  board-edge clearance, hole-to-hole distance and footprint keep-out areas
  (ESP32 antenna) are respected
* track width from the net class (KiCad project file next to the board);
  if a connection fails it is retried narrower (down to 0.2 mm)
* nets are routed as trees (Prim order); a connection ending on an existing
  track splits that track so KiCad sees the junction
* finally copper pours for --pour nets on both layers are added and filled
KiCad's DRC (tools/drc.py) is the judge of the result, not this script.
"""
import argparse, heapq, json, math, os, sys, time
import numpy as np
import pcbnew

ap = argparse.ArgumentParser()
ap.add_argument("inp"); ap.add_argument("out")
ap.add_argument("--pour", nargs="*", default=["GND"])
ap.add_argument("--skip", nargs="*", default=[])
ap.add_argument("--pitch", type=float, default=0.05)
a = ap.parse_args()

MM = pcbnew.FromMM
board = pcbnew.LoadBoard(a.inp)
ds = board.GetDesignSettings()
CLR = 0.2                                   # copper clearance [mm]
EDGE = pcbnew.ToMM(ds.m_CopperEdgeClearance)
H2H = pcbnew.ToMM(ds.m_HoleToHoleMin)
MARGIN = 0.02                               # grid / chord safety
VIA_D, VIA_DRILL = 0.6, 0.3
P = a.pitch
LAYERS = [pcbnew.F_Cu, pcbnew.B_Cu]

# ---------------------------------------------------------------- net classes (from the .kicad_pro)
PRO_PATH = os.path.splitext(a.out)[0] + ".kicad_pro"                 # project next to OUT
PRO_TEXT = open(PRO_PATH).read()     # SaveBoard rewrites the project file: restore it after each save
pro = json.loads(PRO_TEXT)
def save(path, b):
    pcbnew.SaveBoard(path, b)
    open(PRO_PATH, "w").write(PRO_TEXT)
cls_w = {c["name"]: c.get("track_width", 0.25) for c in pro["net_settings"]["classes"]}
import fnmatch
def net_width(name):
    for pt in pro["net_settings"].get("netclass_patterns", []):
        if fnmatch.fnmatchcase(name, pt["pattern"]):
            return min(cls_w.get(pt["netclass"], 0.25), 0.5)
    return 0.25

# ---------------------------------------------------------------- grid
bb = board.GetBoardEdgesBoundingBox()
X0, Y0 = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY())
X1, Y1 = X0 + pcbnew.ToMM(bb.GetWidth()), Y0 + pcbnew.ToMM(bb.GetHeight())
NX, NY = int(round((X1 - X0) / P)) + 1, int(round((Y1 - Y0) / P)) + 1
gx = X0 + np.arange(NX) * P
gy = Y0 + np.arange(NY) * P
BIG = np.float32(99.0)
d1 = [np.full((NY, NX), BIG, np.float32) for _ in LAYERS]       # nearest copper
n1 = [np.full((NY, NX), -9, np.int32) for _ in LAYERS]          # its net
d2 = [np.full((NY, NX), BIG, np.float32) for _ in LAYERS]       # nearest copper of a different net
dh = np.full((NY, NX), BIG, np.float32)                        # nearest drill (any net)
YY, XX = np.meshgrid(gy, gx, indexing="ij")
edge = np.minimum(np.minimum(XX - X0, X1 - XX), np.minimum(YY - Y0, Y1 - YY)).astype(np.float32)
keep = [np.full((NY, NX), BIG, np.float32) for _ in LAYERS]
del YY, XX

netcode = {}
def code(net):
    if net not in netcode: netcode[net] = len(netcode) + 1
    return netcode[net]

def win(x0, y0, x1, y1, r=1.2):
    i0 = max(0, int((x0 - r - X0) / P)); i1 = min(NX, int((x1 + r - X0) / P) + 2)
    j0 = max(0, int((y0 - r - Y0) / P)); j1 = min(NY, int((y1 + r - Y0) / P) + 2)
    return j0, j1, i0, i1

def seg_dist(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    L2 = vx * vx + vy * vy
    t = np.clip(((px - ax) * vx + (py - ay) * vy) / L2, 0, 1) if L2 > 0 else 0.0
    return np.hypot(px - (ax + t * vx), py - (ay + t * vy))

def poly_dist(px, py, pts):
    d = np.full(px.shape, 1e9)
    inside = np.zeros(px.shape, bool)
    n = len(pts)
    for k in range(n):
        ax, ay = pts[k]; bx, by = pts[(k + 1) % n]
        d = np.minimum(d, seg_dist(px, py, ax, ay, bx, by))
        if (ay > py).any() or (by > py).any():
            c = ((ay > py) != (by > py)) & (px < (bx - ax) * (py - ay) / ((by - ay) if by != ay else 1e-12) + ax)
            inside ^= c
    return np.where(inside, -d, d)

def add_field(li, j0, j1, i0, i1, f, nc):
    """merge distance field f (window) of net nc into layer li"""
    D1, N1, D2_ = d1[li][j0:j1, i0:i1], n1[li][j0:j1, i0:i1], d2[li][j0:j1, i0:i1]
    f = f.astype(np.float32)
    same = N1 == nc
    D1[same] = np.minimum(D1[same], f[same])
    o = ~same
    better = o & (f < D1)
    D2_[better] = D1[better]; N1[better] = nc; D1[better] = f[better]
    rest = o & ~better
    D2_[rest] = np.minimum(D2_[rest], f[rest])

def grid_xy(j0, j1, i0, i1):
    return np.meshgrid(gx[i0:i1], gy[j0:j1])

def add_poly(li, pts, nc):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    j0, j1, i0, i1 = win(min(xs), min(ys), max(xs), max(ys))
    px, py = grid_xy(j0, j1, i0, i1)
    add_field(li, j0, j1, i0, i1, poly_dist(px, py, pts), nc)

def add_circle(li, cx, cy, r, nc):
    j0, j1, i0, i1 = win(cx - r, cy - r, cx + r, cy + r)
    px, py = grid_xy(j0, j1, i0, i1)
    add_field(li, j0, j1, i0, i1, np.hypot(px - cx, py - cy) - r, nc)

def add_hole(cx, cy, r):
    j0, j1, i0, i1 = win(cx - r, cy - r, cx + r, cy + r)
    px, py = grid_xy(j0, j1, i0, i1)
    dh[j0:j1, i0:i1] = np.minimum(dh[j0:j1, i0:i1], np.hypot(px - cx, py - cy) - r)

def add_seg(li, ax, ay, bx, by, w, nc):
    j0, j1, i0, i1 = win(min(ax, bx), min(ay, by), max(ax, bx), max(ay, by))
    px, py = grid_xy(j0, j1, i0, i1)
    add_field(li, j0, j1, i0, i1, seg_dist(px, py, ax, ay, bx, by) - w / 2, nc)

def polyset_pts(ps):
    out = []
    for k in range(ps.OutlineCount()):
        o = ps.Outline(k)
        out.append([(pcbnew.ToMM(o.CPoint(i).x), pcbnew.ToMM(o.CPoint(i).y)) for i in range(o.PointCount())])
    return out

# ---------------------------------------------------------------- obstacles from the board
pads_of_net = {}
pad_shape = {}                     # (ref, num, idx) -> {layer: [polys]}
t0 = time.time()
for fp in board.GetFootprints():
    for pad in fp.Pads():
        net = pad.GetNetname()
        nc = code(net) if net else -1 - len(netcode) - 1000   # unique "no net"
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            r = pcbnew.ToMM(max(pad.GetDrillSize().x, pad.GetDrillSize().y)) / 2
            c = pad.GetPosition()
            for li in range(2):
                add_circle(li, pcbnew.ToMM(c.x), pcbnew.ToMM(c.y), r + 0.05, -2)   # hole clearance 0.25
            add_hole(pcbnew.ToMM(c.x), pcbnew.ToMM(c.y), r)
            continue
        shapes = {}
        for li, L in enumerate(LAYERS):
            if not pad.IsOnLayer(L): continue
            ps = pcbnew.SHAPE_POLY_SET()
            pad.TransformShapeToPolygon(ps, L, 0, MM(0.005))
            polys = polyset_pts(ps)
            shapes[li] = polys
            for pl in polys: add_poly(li, pl, nc)
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            c = pad.GetPosition()
            add_hole(pcbnew.ToMM(c.x), pcbnew.ToMM(c.y), pcbnew.ToMM(pad.GetDrillSize().x) / 2)
        if net:
            c = pad.GetPosition()
            pads_of_net.setdefault(net, []).append(dict(ref=fp.GetReference(), num=pad.GetNumber(),
                                                        x=pcbnew.ToMM(c.x), y=pcbnew.ToMM(c.y), shapes=shapes))
    for z in fp.Zones():
        if not (z.GetIsRuleArea() and z.GetDoNotAllowTracks()): continue
        for li, L in enumerate(LAYERS):
            if not z.GetLayerSet().Contains(L): continue
            for pl in polyset_pts(z.Outline()):
                xs = [p[0] for p in pl]; ys = [p[1] for p in pl]
                j0, j1, i0, i1 = win(min(xs), min(ys), max(xs), max(ys))
                px, py = grid_xy(j0, j1, i0, i1)
                keep[li][j0:j1, i0:i1] = np.minimum(keep[li][j0:j1, i0:i1], poly_dist(px, py, pl))
print(f"fields built in {time.time() - t0:.1f} s, grid {NX} x {NY} x 2, {len(pads_of_net)} nets")
STATIC = ([x.copy() for x in d1], [x.copy() for x in n1], [x.copy() for x in d2], dh.copy())

# ---------------------------------------------------------------- routing
DIRS = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
        (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)]
VIA_COST = 1.2 / P                  # a via "costs" 1.2 mm of track
BEND = 0.4

def free_mask(li, nc, w, j0, j1, i0, i1):
    D1 = d1[li][j0:j1, i0:i1]; N1 = n1[li][j0:j1, i0:i1]; D2 = d2[li][j0:j1, i0:i1]
    other = np.where(N1 == nc, D2, D1)
    return (other >= CLR + w / 2 + MARGIN) & (edge[j0:j1, i0:i1] >= EDGE + w / 2 + MARGIN) & \
           (keep[li][j0:j1, i0:i1] >= w / 2 + MARGIN)

def via_mask(nc, j0, j1, i0, i1):
    r = VIA_D / 2
    m = dh[j0:j1, i0:i1] >= H2H + VIA_DRILL / 2 + MARGIN
    for li in range(2):
        D1 = d1[li][j0:j1, i0:i1]; N1 = n1[li][j0:j1, i0:i1]; D2 = d2[li][j0:j1, i0:i1]
        other = np.where(N1 == nc, D2, D1)
        m &= (other >= CLR + r + MARGIN) & (edge[j0:j1, i0:i1] >= EDGE + r + MARGIN) & (keep[li][j0:j1, i0:i1] >= r + MARGIN)
    return m

def pad_cells(pad, li, j0, j1, i0, i1):
    m = np.zeros((j1 - j0, i1 - i0), bool)
    if li not in pad["shapes"]: return m
    px, py = grid_xy(j0, j1, i0, i1)
    for pl in pad["shapes"][li]:
        m |= poly_dist(px, py, pl) <= -0.01
    return m

def pad_window(pad, li):
    """(j0, j1, i0, i1, mask) of the cells inside the pad on layer li"""
    pls = pad["shapes"].get(li, [])
    if not pls: return None
    xs = [p[0] for pl in pls for p in pl]; ys = [p[1] for pl in pls for p in pl]
    j0, j1, i0, i1 = win(min(xs), min(ys), max(xs), max(ys), 0.1)
    return j0, j1, i0, i1, pad_cells(pad, li, j0, j1, i0, i1)

def tree_add_pad(tree, pad):
    for li in range(2):
        r = pad_window(pad, li)
        if r: tree[li][r[0]:r[1], r[2]:r[3]] |= r[4]

def pad_on_tree(tree, pad):
    for li in range(2):
        r = pad_window(pad, li)
        if r and (tree[li][r[0]:r[1], r[2]:r[3]] & r[4]).any(): return True
    return False

def astar(nc, w, src_pad, tree, target_xy, margin):
    """route from src_pad to any cell of tree[li] (bool full-size arrays)"""
    tx, ty = target_xy
    xs = [src_pad["x"], tx]; ys = [src_pad["y"], ty]
    j0, j1, i0, i1 = win(min(xs), min(ys), max(xs), max(ys), margin)
    H, W = j1 - j0, i1 - i0
    free = [free_mask(li, nc, w, j0, j1, i0, i1) for li in range(2)]
    vm = via_mask(nc, j0, j1, i0, i1)
    goal = [tree[li][j0:j1, i0:i1] & free[li] for li in range(2)]
    if not (goal[0].any() or goal[1].any()): return None
    tix, tiy = (tx - X0) / P - i0, (ty - Y0) / P - j0
    INF = 1e18
    g = np.full((2, H, W), INF)
    par = {}
    pq = []
    for li in range(2):
        st = pad_cells(src_pad, li, j0, j1, i0, i1) & free[li]
        for (y, x) in zip(*np.nonzero(st)):
            g[li, y, x] = 0.0
            heapq.heappush(pq, (1.2 * math.hypot(x - tix, y - tiy), 0.0, li, y, x, -1))
            par[(li, y, x)] = None
    fl = [f.tolist() for f in free]; vl = vm.tolist(); gl = [gg.tolist() for gg in goal]
    gg = g
    n = 0
    while pq:
        f, c, li, y, x, dprev = heapq.heappop(pq)
        if c > gg[li, y, x]: continue
        n += 1
        if gl[li][y][x] and par[(li, y, x)] is not None:
            path = [(li, y, x)]
            k = (li, y, x)
            while par[k] is not None:
                k = par[k]; path.append(k)
            return [(l, yy + j0, xx + i0) for l, yy, xx in reversed(path)]
        if n > 600000: return None
        for di, (dx, dy, cost) in enumerate(DIRS):
            nx_, ny_ = x + dx, y + dy
            if 0 <= nx_ < W and 0 <= ny_ < H and fl[li][ny_][nx_]:
                # diagonal moves must not cut a blocked corner
                if dx and dy and not (fl[li][y][nx_] and fl[li][ny_][x]): continue
                nc_ = c + cost + (BEND if dprev not in (-1, di) else 0.0)
                if nc_ < gg[li, ny_, nx_]:
                    gg[li, ny_, nx_] = nc_
                    par[(li, ny_, nx_)] = (li, y, x)
                    heapq.heappush(pq, (nc_ + 1.2 * math.hypot(nx_ - tix, ny_ - tiy), nc_, li, ny_, nx_, di))
        if vl[y][x]:
            ol = 1 - li
            if fl[ol][y][x]:
                nc_ = c + VIA_COST
                if nc_ < gg[ol, y, x]:
                    gg[ol, y, x] = nc_
                    par[(ol, y, x)] = (li, y, x)
                    heapq.heappush(pq, (nc_ + 1.2 * math.hypot(x - tix, y - tiy), nc_, ol, y, x, -1))
    return None

tracks_of_net = {}                  # net -> list of [li, ax, ay, bx, by, w, PCB_TRACK]
vias_of_net = {}                    # net -> list of [x, y, PCB_VIA]

def rebuild_fields():
    """static (pads, holes) + every track and via still on the board"""
    for li in range(2):
        d1[li][:] = STATIC[0][li]; n1[li][:] = STATIC[1][li]; d2[li][:] = STATIC[2][li]
    dh[:] = STATIC[3]
    for net, trs in tracks_of_net.items():
        for li, ax, ay, bx, by, w, t in trs: add_seg(li, ax, ay, bx, by, w, code(net))
    for net, vs in vias_of_net.items():
        for x_, y_, v in vs:
            for li in range(2): add_circle(li, x_, y_, VIA_D / 2, code(net))
            add_hole(x_, y_, VIA_DRILL / 2)

def rip(net):
    for tr in tracks_of_net.pop(net, []): board.Remove(tr[6])
    for v in vias_of_net.pop(net, []): board.Remove(v[2])

def xy(j, i): return round(float(X0 + int(i) * P), 4), round(float(Y0 + int(j) * P), 4)

def commit(net, nc, w, path, tree):
    """turn a cell path into tracks and vias, update fields and the net's tree"""
    pts = []
    for k, (li, j, i) in enumerate(path):
        tree[li][j, i] = True
        pts.append((li, j, i))
    # split into runs per layer, compress collinear steps
    runs, cur = [], [pts[0]]
    for p in pts[1:]:
        if p[0] != cur[-1][0]:
            runs.append(cur); cur = [p]
        else:
            cur.append(p)
    runs.append(cur)
    netinfo = board.FindNet(net)
    for r in runs:
        li = r[0][0]
        verts = [r[0]]
        for k in range(1, len(r) - 1):
            d0 = (r[k][1] - r[k - 1][1], r[k][2] - r[k - 1][2]); d1_ = (r[k + 1][1] - r[k][1], r[k + 1][2] - r[k][2])
            if d0 != d1_: verts.append(r[k])
        if len(r) > 1: verts.append(r[-1])
        for (l, ja, ia), (_, jb, ib) in zip(verts, verts[1:]):
            ax, ay = xy(ja, ia); bx, by = xy(jb, ib)
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(pcbnew.VECTOR2I(MM(ax), MM(ay))); t.SetEnd(pcbnew.VECTOR2I(MM(bx), MM(by)))
            t.SetWidth(MM(w)); t.SetLayer(LAYERS[li]); t.SetNet(netinfo)
            board.Add(t)
            tracks_of_net.setdefault(net, []).append([li, ax, ay, bx, by, w, t])
            add_seg(li, ax, ay, bx, by, w, nc)
    for (la, ja, ia), (lb, jb, ib) in zip(pts, pts[1:]):
        if la != lb:
            x_, y_ = xy(ja, ia)
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(MM(x_), MM(y_))); v.SetWidth(MM(VIA_D)); v.SetDrill(MM(VIA_DRILL))
            v.SetNet(netinfo)
            board.Add(v)
            vias_of_net.setdefault(net, []).append([x_, y_, v])
            for li in range(2): add_circle(li, x_, y_, VIA_D / 2, nc)
            add_hole(x_, y_, VIA_DRILL / 2)
    # the end of the path touches the tree: split a track there so KiCad sees a junction
    le, je, ie = path[-1]
    ex, ey = xy(je, ie)
    for tr in list(tracks_of_net.get(net, [])):
        li, ax, ay, bx, by, tw, t = tr
        if li != le: continue
        if math.hypot(ex - ax, ey - ay) < 1e-6 or math.hypot(ex - bx, ey - by) < 1e-6: continue
        if abs((bx - ax) * (ey - ay) - (by - ay) * (ex - ax)) < 1e-6 and \
           min(ax, bx) - 1e-6 <= ex <= max(ax, bx) + 1e-6 and min(ay, by) - 1e-6 <= ey <= max(ay, by) + 1e-6:
            t.SetEnd(pcbnew.VECTOR2I(MM(ex), MM(ey))); tr[3], tr[4] = ex, ey
            t2 = pcbnew.PCB_TRACK(board)
            t2.SetStart(pcbnew.VECTOR2I(MM(ex), MM(ey))); t2.SetEnd(pcbnew.VECTOR2I(MM(bx), MM(by)))
            t2.SetWidth(t.GetWidth()); t2.SetLayer(t.GetLayer()); t2.SetNet(t.GetNet())
            board.Add(t2)
            tracks_of_net[net].append([li, ex, ey, bx, by, tw, t2])
            break

def route_net(net):
    pads = pads_of_net[net]
    nc = code(net)
    w0 = net_width(net)
    tree = [np.zeros((NY, NX), bool) for _ in range(2)]
    # Prim order from the pad nearest to the net's centroid
    cx = sum(p["x"] for p in pads) / len(pads); cy = sum(p["y"] for p in pads) / len(pads)
    first = min(pads, key=lambda p: math.hypot(p["x"] - cx, p["y"] - cy))
    done = [first]
    todo = [p for p in pads if p is not first]
    tree_add_pad(tree, first)
    fails = []
    retried = []
    while todo:
        cand = [p for p in todo if p not in retried] or todo
        p, q = min(((p, q) for p in cand for q in done), key=lambda pq: math.hypot(pq[0]["x"] - pq[1]["x"], pq[0]["y"] - pq[1]["y"]))
        todo.remove(p)
        # same pad position already part of the tree (stacked pads like USB GND A1/B12)
        if pad_on_tree(tree, p):
            done.append(p); continue
        path = None
        for w in sorted({w0, 0.3, 0.25, 0.2}, reverse=True):
            if w > w0: continue
            for margin in (4.0, 12.0):
                path = astar(nc, w, p, tree, (q["x"], q["y"]), margin)
                if path: break
            if path: break
        if not path:
            if p not in retried:            # try again once more pads of the net are connected
                retried.append(p); todo.append(p)
                continue
            fails.append((net, p, q))
            continue
        commit(net, nc, w, path, tree)
        tree_add_pad(tree, p)
        done.append(p)
    return fails

def span(net):
    ps = pads_of_net[net]
    return (max(p["x"] for p in ps) - min(p["x"] for p in ps)) + (max(p["y"] for p in ps) - min(p["y"] for p in ps))

order = [n for n in pads_of_net if len(pads_of_net[n]) > 1 and n not in a.skip and not n.startswith("NC_")]
# power nets (wide) first, then signals by length, the pour net last
order.sort(key=lambda n: (n in a.pour, 0 if net_width(n) >= 0.5 else 1, span(n)))
t0 = time.time()
fails = []
for k, net in enumerate(order):
    f = route_net(net)
    fails += f
    print(f"[{k + 1}/{len(order)}] {net:12s} pads {len(pads_of_net[net]):3d}  {'FAILED ' + str(len(f)) if f else 'ok'}  ({time.time() - t0:.0f} s)", flush=True)

# ---------------------------------------------------------------- rip-up and reroute
def blockers(net, p, q):
    """nets whose tracks/vias block the path that exists with pads only"""
    saved = ([x.copy() for x in d1], [x.copy() for x in n1], [x.copy() for x in d2], dh.copy())
    for li in range(2):
        d1[li][:] = STATIC[0][li]; n1[li][:] = STATIC[1][li]; d2[li][:] = STATIC[2][li]
    dh[:] = STATIC[3]
    tree = [np.zeros((NY, NX), bool) for _ in range(2)]
    tree_add_pad(tree, q)
    path = None
    for w in (0.25, 0.2):
        path = astar(code(net), w, p, tree, (q["x"], q["y"]), 12.0)
        if path: break
    for li in range(2):
        d1[li][:] = saved[0][li]; n1[li][:] = saved[1][li]; d2[li][:] = saved[2][li]
    dh[:] = saved[3]
    if not path: return None
    nc = code(net)
    inv = {v: k for k, v in netcode.items()}
    found = set()
    for li, j, i in path:
        if n1[li][j, i] != nc and d1[li][j, i] < CLR + 0.2 and n1[li][j, i] > 0:
            m = inv.get(int(n1[li][j, i]))
            if m in tracks_of_net or m in vias_of_net: found.add(m)
    return found

ripped_count = {}
stuck = 0
for it in range(60):
    if not fails or stuck >= len(fails): break
    net, p, q = fails[0]
    b = blockers(net, p, q)
    b = {x for x in (b or set()) if x != net and ripped_count.get(x, 0) < 3}
    print(f"rip-up {it + 1}: {net} {p['ref']}.{p['num']} blocked by {sorted(b) if b else 'nothing removable'}", flush=True)
    if not b:
        fails.append(fails.pop(0))
        stuck += 1
        continue
    stuck = 0
    for x in b | {net}:
        ripped_count[x] = ripped_count.get(x, 0) + 1
        rip(x)
    rebuild_fields()
    fails = [f_ for f_ in fails if f_[0] not in b | {net}]
    for x in [net] + sorted(b, key=span):
        f = route_net(x)
        fails += f
        print(f"   reroute {x:12s} {'FAILED ' + str(len(f)) if f else 'ok'}", flush=True)
fails = [f"{n_}: {p_['ref']}.{p_['num']} -> {q_['ref']}.{q_['num']}" for n_, p_, q_ in fails]

# ---------------------------------------------------------------- pours
for net in a.pour:
    ni = board.FindNet(net)
    if not ni: continue
    for L in LAYERS:
        z = pcbnew.ZONE(board)
        z.SetLayer(L); z.SetNet(ni)
        z.SetLocalClearance(MM(0.3)); z.SetMinThickness(MM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(MM(0.3)); z.SetThermalReliefSpokeWidth(MM(0.4))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        ol = z.Outline(); ol.NewOutline()
        for x_, y_ in ((X0 + 0.3, Y0 + 0.3), (X1 - 0.3, Y0 + 0.3), (X1 - 0.3, Y1 - 0.3), (X0 + 0.3, Y1 - 0.3)):
            ol.Append(MM(x_), MM(y_))
        board.Add(z)
save(a.out, board)
# fill the pours with the project's rules (.kicad_pro / .kicad_dru must sit next to OUT)
board = pcbnew.LoadBoard(a.out)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
save(a.out, board)
board.BuildConnectivity()
uc = board.GetConnectivity().GetUnconnectedCount(True) if hasattr(board.GetConnectivity(), "GetUnconnectedCount") else -1
print(f"routed {len(order)} nets in {time.time() - t0:.0f} s; failed connections {len(fails)}; unconnected after pours {uc}")
for f in fails: print("  FAIL", f)
