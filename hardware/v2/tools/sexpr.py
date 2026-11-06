"""Minimal S-expression reader/writer for KiCad files."""
import re

_tok = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))')

class Sym(str):
    """bare (unquoted) atom"""
    pass

def parse(text):
    stack, cur = [], []
    pos = 0
    n = len(text)
    while pos < n:
        m = _tok.match(text, pos)
        if not m:
            if text[pos:].strip() == "": break
            raise ValueError(f"parse error at {pos}: {text[pos:pos+40]!r}")
        pos = m.end()
        if m.group(1):
            stack.append(cur); cur = []
        elif m.group(2):
            done = cur; cur = stack.pop(); cur.append(done)
        elif m.group(3) is not None:
            cur.append(m.group(3)[1:-1].replace('\\"', '"').replace("\\\\", "\\"))
        else:
            cur.append(Sym(m.group(4)))
    return cur

def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'

def dump(e, indent=0):
    if isinstance(e, list):
        if not e: return "()"
        simple = all(not isinstance(x, list) for x in e)
        if simple:
            return "(" + " ".join(dump(x) for x in e) + ")"
        out = "(" + dump(e[0])
        for x in e[1:]:
            if isinstance(x, list):
                out += "\n" + "  " * (indent + 1) + dump(x, indent + 1)
            else:
                out += " " + dump(x)
        return out + ")"
    if isinstance(e, Sym): return str(e)
    if isinstance(e, (int, float)):
        return f"{e:.4f}".rstrip("0").rstrip(".") if isinstance(e, float) else str(e)
    return q(e)

def find(e, key):
    return [x for x in e if isinstance(x, list) and x and x[0] == key]

def first(e, key):
    r = find(e, key)
    return r[0] if r else None
