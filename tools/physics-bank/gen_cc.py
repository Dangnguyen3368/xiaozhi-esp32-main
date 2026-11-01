import json, os
from PIL import Image
b = json.load(open("physics_bank.json", encoding="utf-8"))
topics = ["co","nhiet","dien","quang","thuchanh"]
FIG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fig")
def c(s):
    return '"' + s.replace("\\","\\\\").replace('"','\\"').replace("\n","\\n") + '"'
L = ["// Generated from physics_bank.json by gen_cc.py. Do not edit by hand.",
     "// Keep the order stable: progress bits in NVS are indexed by position, so add new",
     "// problems only at the end.",
     '#include "physics_bank.h"', ""]
# Screen images, 1-bit, packed MSB first, 1 = ink. A problem with a figure (fig/b_<name>.png) shows
# it with the data lines ("givens") printed underneath; a problem without one shows the data lines
# alone. Every problem gets its own image: kFig_<id>.
from PIL import ImageDraw, ImageFont
FONT = ImageFont.truetype(os.path.join(FIG_DIR, "DejaVuSans.ttf"), 13)
LINE_H = 16
MAX_W = 236
def wrap(text):
    # Break a data line at spaces to fit the screen; continuation lines are indented.
    out, cur = [], ""
    for word in text.split(" "):
        trial = (cur + " " + word) if cur else word
        if FONT.getlength(trial) <= MAX_W or not cur:
            cur = trial
        else:
            out.append(cur)
            cur = "   " + word
    out.append(cur)
    for t in out:
        assert FONT.getlength(t) <= MAX_W, t
    return out
def screen_image(p):
    lines = [w for t in (p.get("givens") or []) for w in wrap(t)]
    fig = None
    if p.get("figure"):
        fig = Image.open(os.path.join(FIG_DIR, "b_%s.png" % p["figure"])).convert("1")
    if not lines:
        return fig
    text_w = max(int(FONT.getlength(t)) + 2 for t in lines)
    fig_h = fig.height + 6 if fig else 0
    w = max(fig.width if fig else 0, text_w)
    out = Image.new("1", (w, fig_h + LINE_H * len(lines)), 1)
    d = ImageDraw.Draw(out); d.fontmode = "1"
    if fig:
        out.paste(fig, ((w - fig.width) // 2, 0))
        d.line([(0, fig.height + 2), (w - 1, fig.height + 2)], fill=0)
    for i, t in enumerate(lines):
        d.text((1, fig_h - 1 + i * LINE_H), t, font=FONT, fill=0)
    return out
total = 0
for p in b:
    im = screen_image(p)
    if im is None:
        continue
    name = p["id"]
    if p.get("givens"):
        im.save(os.path.join(FIG_DIR, "g_%s.png" % name))
    w, h = im.size; px = im.load(); stride = (w + 7) // 8
    data = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:
                data[y * stride + x // 8] |= 0x80 >> (x % 8)
    total += len(data)
    L.append("static const uint8_t kFigBits_%s[] = {" % name)
    for i in range(0, len(data), 24):
        L.append("    " + ", ".join("0x%02x" % v for v in data[i:i + 24]) + ",")
    L += ["};", "static const PhysicsFigure kFig_%s = {%d, %d, kFigBits_%s, %s};" % (
        name, w, h, name, "true" if p.get("figure") else "false"), ""]
    p["screen"] = name
print("image bytes", total)
L.append("const PhysicsProblem kPhysicsProblems[] = {")
for p in b:
    h = p["hints"] + [""]*(3-len(p["hints"]))
    fig = "&kFig_%s" % p["screen"] if p.get("screen") else "nullptr"
    L.append("    {%s, %d, %d, %s,\n     %s,\n     %s,\n     {%s, %s, %s}, %d,\n     %s,\n     %s}," % (
        c(p["id"]), topics.index(p["topic"]), p["level"], c(p["title"]), c(p["question"]), c(p["answer"]),
        c(h[0]), c(h[1]), c(h[2]), len(p["hints"]), c(p["solution"]), fig))
L += ["};", "", "const size_t kPhysicsProblemCount = sizeof(kPhysicsProblems) / sizeof(kPhysicsProblems[0]);", ""]

# Formula sheets: the title, a rule, then the wrapped lines; the same text goes to the model.
from formulas import FORMULAS
def bits_of(im):
    w, h = im.size; px = im.load(); stride = (w + 7) // 8
    data = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:
                data[y * stride + x // 8] |= 0x80 >> (x % 8)
    return data
for key, title, lines in FORMULAS:
    rows = [w for t in lines for w in wrap(t)]
    w = MAX_W + 2
    im = Image.new("1", (w, LINE_H * (len(rows) + 1) + 4), 1)
    d = ImageDraw.Draw(im); d.fontmode = "1"
    d.text((1, -1), title.upper(), font=FONT, fill=0)
    d.line([(0, LINE_H + 1), (w - 1, LINE_H + 1)], fill=0)
    for i, t in enumerate(rows):
        d.text((1, LINE_H + 3 + i * LINE_H), t, font=FONT, fill=0)
    im.save(os.path.join(FIG_DIR, "g_formula_%s.png" % key))
    data = bits_of(im)
    total += len(data)
    L.append("static const uint8_t kFormulaBits_%s[] = {" % key)
    for i in range(0, len(data), 24):
        L.append("    " + ", ".join("0x%02x" % v for v in data[i:i + 24]) + ",")
    L += ["};", "static const PhysicsFigure kFormulaFig_%s = {%d, %d, kFormulaBits_%s, false};" % (
        key, im.width, im.height, key), ""]
L.append("const PhysicsFormulaSheet kPhysicsFormulas[] = {")
for key, title, lines in FORMULAS:
    L.append("    {%d, %s,\n     %s,\n     &kFormulaFig_%s}," % (
        topics.index(key), c(title), c("\n".join(lines)), key))
L += ["};", "", "const size_t kPhysicsFormulaCount = sizeof(kPhysicsFormulas) / sizeof(kPhysicsFormulas[0]);", ""]
print("images incl. formulas", total)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "main", "study", "physics_bank_data.cc")
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(L))
print(len(b))
