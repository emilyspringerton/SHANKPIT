#!/usr/bin/env python3
# scripts/level_smoke_drive.py -- the X side of scripts/level_smoke_test.sh: real key events through the X server's
# XTEST extension (so SDL translates them exactly as it would a keyboard), screenshots of the root window, and the
# picture checks the smoke test makes. Needs python-xlib (pip install --user python-xlib) and Pillow.
#
#   level_smoke_drive.py key shift+m | key Return | key 1     press and release, modifiers held around the key
#   level_smoke_drive.py move X Y                             move the pointer
#   level_smoke_drive.py shot OUT.png                         the whole screen
#   level_smoke_drive.py nonblack A.png [B.png ...]           every picture has a mean brightness above 0.02
#   level_smoke_drive.py differ A.png B.png                   the two differ on average by more than 0.05
#   level_smoke_drive.py banner A.png R,G,B                   >= 40 pixels of that colour (+-48 per channel) in the
#                                                             level shell's hint strip (window rows 12..48)
import sys
import time

MODS = {"shift": "Shift_L", "ctrl": "Control_L", "alt": "Alt_L"}


def xconn():
    from Xlib import display
    return display.Display()


def keycode(d, name):
    from Xlib import XK
    return d.keysym_to_keycode(XK.string_to_keysym(name))


def key(spec):
    from Xlib import X
    from Xlib.ext import xtest
    d = xconn()
    names = spec.split("+")
    mods = [MODS[n] for n in names[:-1]]
    k = names[-1]
    for m in mods:
        xtest.fake_input(d, X.KeyPress, keycode(d, m))
        d.sync()
        time.sleep(0.12)
    xtest.fake_input(d, X.KeyPress, keycode(d, k))
    d.sync()
    time.sleep(0.12)
    xtest.fake_input(d, X.KeyRelease, keycode(d, k))
    d.sync()
    time.sleep(0.12)
    for m in reversed(mods):
        xtest.fake_input(d, X.KeyRelease, keycode(d, m))
        d.sync()
        time.sleep(0.12)


def move(x, y):
    from Xlib import X
    from Xlib.ext import xtest
    d = xconn()
    xtest.fake_input(d, X.MotionNotify, x=int(x), y=int(y))
    d.sync()


def shot(path):
    from Xlib import X
    from PIL import Image
    d = xconn()
    root = d.screen().root
    g = root.get_geometry()
    raw = root.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xFFFFFFFF)
    Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX").save(path)


def load(path):
    from PIL import Image
    return Image.open(path).convert("RGB")


def mean(img):
    small = img.resize((64, 36))
    px = list(small.getdata())
    return sum(sum(p) for p in px) / (len(px) * 3 * 255.0)


def nonblack(paths):
    bad = [p for p in paths if mean(load(p)) <= 0.02]
    for p in bad:
        print("BLACK: %s" % p)
    return 0 if not bad else 1


def differ(a, b):
    from PIL import ImageChops
    diff = mean(ImageChops.difference(load(a), load(b)))
    print("mean difference %.3f" % diff)
    return 0 if diff > 0.05 else 1


def banner(path, rgb):
    want = [int(float(c) * 255) for c in rgb.split(",")]
    img = load(path)
    w, _ = img.size
    hits = 0
    for y in range(12, 49):
        for x in range(0, w):
            p = img.getpixel((x, y))
            if all(abs(p[i] - want[i]) <= 48 for i in range(3)):
                hits += 1
    print("banner pixels %d" % hits)
    return 0 if hits >= 40 else 1


def main(argv):
    if len(argv) < 2:
        print(__doc__ or "usage: see the header", file=sys.stderr)
        return 2
    cmd = argv[1]
    if cmd == "key":
        key(argv[2])
    elif cmd == "move":
        move(argv[2], argv[3])
    elif cmd == "shot":
        shot(argv[2])
    elif cmd == "nonblack":
        return nonblack(argv[2:])
    elif cmd == "differ":
        return differ(argv[2], argv[3])
    elif cmd == "banner":
        return banner(argv[2], argv[3])
    else:
        print("unknown command %s" % cmd, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
