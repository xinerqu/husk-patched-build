#!/usr/bin/env python3
"""Test build: save the screen at a few fixed frames, so a scene that draws wrong can be looked at rather than guessed about.

Everything measurable about this report is clean -- 543 texture uploads all succeed, one GL error in a run, every draw state
unchanged across 1500 frames, every draw to the default framebuffer -- yet a scene's ground, trees, grass, road and player are
absent while two followers, their shadows and all UI draw. Rather than infer more from calls, save what is actually on the screen
and compare it with a phone that draws it correctly.

husk-tl-egl.c already has save_frame(): glReadPixels on the default framebuffer, written as a 24-bit BMP. This turns it on for a
Godot game and keeps five frames -- one early, then a spread across the first few thousand frames, which is where the scene starts.

The frames land in the game's Documents/screenshots, where the Files app (and Husk's own log viewer) can reach them.

Not a fix, and not part of #140.
"""
import pathlib
import sys


def edit(path: pathlib.Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        print(f"  {path.name}: already applied")
        return
    if text.count(old) != 1:
        sys.exit(f"{path}: expected exactly one match for:\n{old[:200]}")
    path.write_text(text.replace(old, new))
    print(f"  {path.name}: patched")


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    tl = root / "src" / "translation-layer-next"

    # 1. The Godot branch asks for frames: a folder beside the game's data, one every 200 swaps, plus the first three.
    edit(tl / "husk-tl-unity-app.c",
         '            .metal_layer = A.layer, .angle_egl = A.angle, .angle_gles = NULL, .frame_dir = NULL, .frame_every = 0,\n'
         '        };\n'
         '        tl_log_line("godot: starting %s as %s, %dx%d", A.apk, A.package, A.width, A.height);',
         '            .metal_layer = A.layer, .angle_egl = A.angle, .angle_gles = NULL, .frame_dir = NULL, .frame_every = 0,\n'
         '        };\n'
         '        /* test build: keep what the screen shows. Every 200th swap (plus the first three), into Documents/screenshots. */\n'
         '        snprintf(A.shotdir, sizeof(A.shotdir), "%s/screenshots", A.data);\n'
         '        cfg.frame_dir = A.shotdir;\n'
         '        cfg.frame_every = 200;\n'
         '        mkdir(A.shotdir, 0755);\n'
         '        tl_log_line("godot: starting %s as %s, %dx%d (saving frames to %s)", A.apk, A.package, A.width, A.height, A.shotdir);')

    # 2. Where the folder name lives.
    edit(tl / "husk-tl-unity-app.c",
         '    char apk[1024], data[1024], package[160], angle[1024], ca[1024], vulkan[1024];',
         '    char apk[1024], data[1024], package[160], angle[1024], ca[1024], vulkan[1024], shotdir[1100];')

    # 3. mkdir needs its header (sys/stat.h is already pulled in by this file; make it explicit for the compiler).
    edit(tl / "husk-tl-unity-app.c",
         '#include "husk-tl-vulkan.h"\n',
         '#include "husk-tl-vulkan.h"\n#include <sys/stat.h>\n')

    return 0


if __name__ == "__main__":
    sys.exit(main())
