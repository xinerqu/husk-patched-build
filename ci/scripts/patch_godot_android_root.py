#!/usr/bin/env python3
"""ANDROID_ROOT for a Godot game: the android-system folder the app itself carries.

Godot 4's Android backend builds its font paths from getenv("ANDROID_ROOT"):

    String root = String(getenv("ANDROID_ROOT")).path_join("fonts");          // os_android.cpp:514, 680
    parser->open(String(getenv("ANDROID_ROOT")).path_join("/etc/fonts.xml")); // os_android.cpp:516

Husk sets that variable only inside java_boot() -- the branch that starts the Android runtime -- so a Godot game never runs it and
the variable is unset. Godot then reads "/etc/fonts.xml": a path with nothing behind it, and the engine starts with no font list:

    ERROR: Cannot open file '/etc/fonts.xml'.
    ERROR: Unable to load font config

The app already carries exactly the tree Godot wants: android-system/etc/fonts.xml and android-system/fonts/ (205 faces), handed to
husk_native_set_system_files() at start-up. Remember that path and make it ANDROID_ROOT just before a Godot engine starts, and the
engine finds its font list with nothing copied anywhere.

What this does not cover: the CJK face. fonts.xml names NotoSansCJK-Regular.ttc, which is not in android-system/fonts (the bundle
does not carry it); Godot skips an entry whose file is missing, so text the game's own font lacks still draws as boxes. The CJK font
downloaded in Settings lives in Application Support/Fonts, and reaching it is a separate question.

Only the variable is set; nothing else about how the game runs changes.
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

    # 1. remember the android-system folder the app passes in
    edit(tl / "husk-tl-unity-app.c",
         '    char apk[1024], data[1024], package[160], angle[1024], ca[1024], vulkan[1024];',
         '    char apk[1024], data[1024], package[160], angle[1024], ca[1024], vulkan[1024], sysroot[1024];')

    edit(tl / "husk-tl-unity-app.c",
         'void husk_native_set_system_files(const char *cacerts_dir, const char *fonts_root)\n'
         '{\n'
         '    if (cacerts_dir) tl_set_cacerts_dir(cacerts_dir);\n'
         '    if (fonts_root) tl_set_system_fonts_dir(fonts_root);\n'
         '}\n',
         'void husk_native_set_system_files(const char *cacerts_dir, const char *fonts_root)\n'
         '{\n'
         '    if (cacerts_dir) tl_set_cacerts_dir(cacerts_dir);\n'
         '    if (fonts_root) { tl_set_system_fonts_dir(fonts_root); snprintf(A.sysroot, sizeof(A.sysroot), "%s", fonts_root); }\n'
         '}\n')

    # 2. Godot needs ANDROID_ROOT to name a folder that has etc/fonts.xml and fonts/ in it
    edit(tl / "husk-tl-unity-app.c",
         '        tl_log_line("godot: starting %s as %s, %dx%d", A.apk, A.package, A.width, A.height);\n'
         '        tl_audio_install();\n'
         '        ok = tl_godot_start(&cfg) && tl_godot_run();',
         '        /* Godot 4 builds its font paths from getenv("ANDROID_ROOT") (os_android.cpp: 514, 516). Husk sets that variable\n'
         '         * only inside java_boot(), which a Godot game never runs, so the engine was reading "/etc/fonts.xml" -- a path\n'
         '         * with nothing behind it. The android-system tree this app carries is exactly what it wants: etc/fonts.xml and\n'
         '         * fonts/. */\n'
         '        if (A.sysroot[0]) setenv("ANDROID_ROOT", A.sysroot, 1);\n'
         '        tl_log_line("godot: starting %s as %s, %dx%d (ANDROID_ROOT %s)", A.apk, A.package, A.width, A.height,\n'
         '                    getenv("ANDROID_ROOT") ? getenv("ANDROID_ROOT") : "(unset, no fonts.xml)");\n'
         '        tl_audio_install();\n'
         '        ok = tl_godot_start(&cfg) && tl_godot_run();')

    return 0


if __name__ == "__main__":
    sys.exit(main())
