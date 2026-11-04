import gzip, sys
from PIL import PcfFontFile

src = sys.argv[1]
f = PcfFontFile.PcfFontFile(gzip.open(src, "rb"))
out = {}
for code in range(32, 256):
    g = f.glyph[code]
    if g is None:
        continue
    try:
        cp = ord(bytes([code]).decode("koi8_u"))
    except Exception:
        continue
    if code >= 0x80 and not (0x400 <= cp <= 0x4FF):
        continue
    if code == 127 or 0x80 <= code < 0xA0 and cp < 0x400:
        continue
    d, dst, srcb, im = g
    w, h = im.size
    rows = []
    ox, oy = dst[0], dst[1]
    canvas = [[0]*8 for _ in range(16)]
    im = im.convert("1")
    px = im.load()
    for y in range(h):
        for x in range(w):
            cx = x
            cy = y
            if 0 <= cx < 8 and 0 <= cy < 16 and px[x, y]:
                canvas[cy][cx] = 1
    out[cp] = [sum(b << (7-i) for i, b in enumerate(r)) for r in canvas]

def rows_first(g):
    for i, r in enumerate(g):
        if r:
            return i
    return 0

def mirror(g):
    return [int("{:08b}".format(r)[::-1], 2) for r in g]

def diaeresis_cap(g):
    g = list(g)
    r0 = rows_first(g)
    if r0 >= 2:
        g[r0 - 2] = 0x24
    return g

def diaeresis_low(g):
    g = list(g)
    i = rows_first(g)
    while i < 16 and g[i]:
        g[i] = 0x24
        i += 1
    return g

def tick(g):
    g = list(g)
    r0 = rows_first(g)
    if r0 >= 1:
        g[r0 - 1] |= 0x02
    return g

L = lambda c: out[ord(c)]
extra = {
    0x406: L("I"), 0x456: L("i"),
    0x407: diaeresis_cap(L("I")), 0x457: diaeresis_low(L("i")),
    0x404: mirror(out[0x42D]), 0x454: mirror(out[0x44D]),
    0x490: tick(out[0x413]), 0x491: tick(out[0x433]),
}
for k, v in extra.items():
    out.setdefault(k, v)

cps = sorted(out)
with open(sys.argv[2], "w") as fo:
    fo.write("static const unsigned short font_cp[] = {\n")
    for i in range(0, len(cps), 12):
        fo.write("\t" + ",".join("0x%04x" % c for c in cps[i:i+12]) + ",\n")
    fo.write("};\nstatic const unsigned char font_bits[][16] = {\n")
    for c in cps:
        fo.write("\t{" + ",".join("0x%02x" % b for b in out[c]) + "},\n")
    fo.write("};\n#define FONT_N %d\n" % len(cps))
for c in (0x407, 0x457, 0x404, 0x491, 0x490):
    print(hex(c))
    for r in out[c]:
        print("".join("#" if r & (0x80 >> i) else "." for i in range(8)))
print(len(cps))
