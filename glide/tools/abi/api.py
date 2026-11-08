"""Reader for abi/glide2x.api, the MGA-Glide master function table."""
import re
from collections import namedtuple

Func = namedtuple("Func", "name ret args tier used status nbytes decorated params")

_PARAM = re.compile(r"^(?P<type>.*?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?P<arr>\[\s*\d*\s*\])?$")


def parse_params(args):
    """Split a C argument list into (type, name) pairs; arrays become pointers."""
    args = args.strip()
    if args in ("", "void"):
        return []
    out = []
    for i, a in enumerate(x.strip() for x in args.split(",")):
        m = _PARAM.match(a)
        t, n = m.group("type").strip(), m.group("name")
        if m.group("arr"):
            t += " *"
        if not t:  # bare type, no name
            t, n = n, "a%d" % i
        out.append((t, n))
    return out


def load_api(path):
    funcs = []
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        name, ret, args, tier, used, status = [x.strip() for x in line.split("|")]
        params = parse_params(args)
        n = 4 * len(params)
        funcs.append(Func(name, ret, args, int(tier), used, status, n,
                          "_%s@%d" % (name.upper(), n), params))
    return funcs
