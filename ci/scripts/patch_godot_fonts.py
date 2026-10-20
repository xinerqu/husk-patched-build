#!/usr/bin/env python3
"""Godot's system fonts: point ANDROID_ROOT at the android-system folder the app carries.

What goes wrong: Godot 4's Android backend builds its font fallback from getenv("ANDROID_ROOT") + "/etc/fonts.xml"
(OS_Android::_load_system_font_config). Husk sets ANDROID_ROOT to the game's data directory for the Android runtime's own paths, so
Godot looks for <game data>/etc/fonts.xml -- which does not exist. The engine then has no font list at all:

    ERROR: Cannot open file '/etc/fonts.xml'.
    ERROR: Unable to load font config

CJK text the game's own font lacks then draws as missing-glyph boxes, even with the CJK font downloaded in Settings, because nothing
tells the engine the file is there.

Husk already hands the app's android-system folder to husk_native_set_system_files() at start-up (it holds etc/fonts.xml, and
tl_path_resolve already falls back to the Settings CJK font for the /system/fonts paths that file names). Keep that path, and set
ANDROID_ROOT to it just before a Godot game starts -- not globally, because the Android runtime is started with ANDROID_ROOT
pointing at the game data folder and must keep that.
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

    # 2. a Godot game needs ANDROID_ROOT to name a folder that has etc/fonts.xml in it
    edit(tl / "husk-tl-unity-app.c",
         '        tl_log_line("godot: starting %s as %s, %dx%d", A.apk, A.package, A.width, A.height);\n'
         '        tl_audio_install();\n'
         '        ok = tl_godot_start(&cfg) && tl_godot_run();',
         '        /* Godot 4 reads its font fallback from getenv("ANDROID_ROOT") + "/etc/fonts.xml". ANDROID_ROOT names the game data\n'
         '         * folder (the Android runtime is started with it), which holds no etc/; the app\'s android-system tree does. The\n'
         '         * /system/fonts paths fonts.xml names are resolved by tl_path_resolve, which falls back to the CJK font from Settings. */\n'
         '        if (A.sysroot[0]) setenv("ANDROID_ROOT", A.sysroot, 1);\n'
         '        tl_log_line("godot: starting %s as %s, %dx%d (fonts from %s)", A.apk, A.package, A.width, A.height,\n'
         '                    A.sysroot[0] ? A.sysroot : "the game data folder, no fonts.xml");\n'
         '        tl_audio_install();\n'
         '        ok = tl_godot_start(&cfg) && tl_godot_run();')

    return 0


if __name__ == "__main__":
    sys.exit(main())
