#!/usr/bin/env python3
"""ANDROID_ROOT for a Godot game: the game data folder, which is where the Android runtime puts it too.

Godot 4's Android backend builds its font paths from getenv("ANDROID_ROOT"):

    String root = String(getenv("ANDROID_ROOT")).path_join("fonts");          // os_android.cpp:514
    parser->open(String(getenv("ANDROID_ROOT")).path_join("/etc/fonts.xml")); // os_android.cpp:516

Husk sets that variable inside java_boot() -- the branch that starts the Android runtime -- so a Godot game never runs it and the
variable is unset. Godot then reads "/etc/fonts.xml": a path with nothing behind it, and the engine starts with no font list at all:

    ERROR: Cannot open file '/etc/fonts.xml'.
    ERROR: Unable to load font config

The value the runtime uses is the game's data folder, so a Godot game gets the same one, set just before its engine starts. That
gives Godot a real, writable directory: put etc/fonts.xml there (the app's android-system tree carries one) and the engine finds it.

This only sets the variable; it changes nothing else about how the game runs.
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

    edit(tl / "husk-tl-unity-app.c",
         '        tl_log_line("godot: starting %s as %s, %dx%d", A.apk, A.package, A.width, A.height);\n'
         '        tl_audio_install();\n'
         '        ok = tl_godot_start(&cfg) && tl_godot_run();',
         '        /* Godot 4 builds its font paths from getenv("ANDROID_ROOT") (os_android.cpp: 514, 516). Husk sets it inside\n'
         '         * java_boot(), which a Godot game never runs, so the engine was reading "/etc/fonts.xml" -- a path with nothing\n'
         '         * behind it -- and started with no font list. The Android runtime uses the game data folder; give Godot the same. */\n'
         '        if (!getenv("ANDROID_ROOT")) setenv("ANDROID_ROOT", A.data, 1);\n'
         '        tl_log_line("godot: starting %s as %s, %dx%d (ANDROID_ROOT %s)", A.apk, A.package, A.width, A.height,\n'
         '                    getenv("ANDROID_ROOT") ? getenv("ANDROID_ROOT") : "(unset)");\n'
         '        tl_audio_install();\n'
         '        ok = tl_godot_start(&cfg) && tl_godot_run();')

    return 0


if __name__ == "__main__":
    sys.exit(main())
