#!/usr/bin/env python3
"""Apply the #140 fix to a Husk checkout (Leviidev/Husk, main as of 2026-10-18).

Three changes:

  1. src/translation-layer-next/husk-tl-jni-godot-kotlin.c (new): Godot 4.6/4.7's Kotlin DirectoryAccessHandler / FileAccessHandler
     signatures, which GodotLib still asks for by their old Java ones, answered from the same file ids as the Java ones.
  2. husk-tl-jni-godot.c: install them (tl_godot_kotlin_install), and answer the four GodotIO methods Godot calls on
     android.content.res.AssetManager through Android (getScreenRefreshRate, getDisplaySafeArea, getLocale, getDataDir).
  3. husk-tl-unity-app.c: ANDROID_ROOT points at the app's android-system folder before any engine starts, which is where Godot 4.7
     reads its font list from (and where tl_path_resolve maps /system/fonts the CJK download into).

Run: python3 ci/scripts/patch_140.py <husk-checkout>
"""
import pathlib
import sys

SWIFT_OLD = '        // The CA certificates and system fonts Android keeps under /system, as the app carries them.\n        if let sys = Bundle.main.resourcePath.map({ $0 + "/android-system" }), FileManager.default.fileExists(atPath: sys) {\n            husk_native_set_system_files(sys + "/cacerts", sys)\n        }'

SWIFT_NEW = '        // The CA certificates and system fonts Android keeps under /system, as the app carries them. Read from the bundle when the\n        // bundle is readable; under a host that relocates the app (LiveContainer reports a resourcePath whose directory a sandboxed\n        // process cannot open) a copy in Documents is used instead, made once from the bundle.\n        if let bundle = Bundle.main.resourcePath.map({ $0 + "/android-system" }), FileManager.default.fileExists(atPath: bundle + "/etc/fonts.xml") {\n            let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("android-system")\n            let sys: URL\n            if FileManager.default.isReadableFile(atPath: bundle + "/etc/fonts.xml") {\n                sys = URL(fileURLWithPath: bundle)\n            } else {\n                if !FileManager.default.fileExists(atPath: docs.appendingPathComponent("etc/fonts.xml").path) {\n                    try? FileManager.default.removeItem(at: docs)\n                    try? FileManager.default.copyItem(at: URL(fileURLWithPath: bundle), to: docs)\n                }\n                sys = docs\n            }\n            husk_native_set_system_files(sys.appendingPathComponent("cacerts").path, sys.path)\n        }'

NEW_FILE = pathlib.Path(__file__).resolve().parent.parent / "patches" / "140" / "husk-tl-jni-godot-kotlin.c"


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

    dest = tl / NEW_FILE.name
    dest.write_text(NEW_FILE.read_text())
    print(f"  {dest}: written")

    edit(tl / "husk-tl-jni-godot.c",
         '    tl_jni_register_hle(bridge);\n',
         '    tl_jni_register_hle(bridge);\n'
         '    tl_godot_kotlin_install();\n')

    edit(tl / "husk-tl-jni-godot.c",
         'void tl_godot_hle_install(const char *pkg, const char *apk, const char *data, int w, int h)\n',
         'void tl_godot_kotlin_install(void);\n\n'
         'void tl_godot_hle_install(const char *pkg, const char *apk, const char *data, int w, int h)\n')

    edit(tl / "husk-tl-jni-godot.c",
         '    tl_jni_register_hle(k_godot);\n',
         '    tl_jni_register_hle(k_godot);\n'
         '    /* Godot 4.7 calls these four on the AssetManager it was given, not on GodotIO: same answers there (#140). */\n'
         '    static const tl_jhle k_assets[] = {\n'
         '        M("android/content/res/AssetManager", "getScreenRefreshRate", "(D)D", IO_refresh),\n'
         '        M("android/content/res/AssetManager", "getDisplaySafeArea", "()[I", IO_safeArea),\n'
         '        M("android/content/res/AssetManager", "getLocale", "()Ljava/lang/String;", IO_locale),\n'
         '        M("android/content/res/AssetManager", "getDataDir", "()Ljava/lang/String;", IO_dataDir),\n'
         '        { NULL, NULL, NULL, NULL }\n'
         '    };\n'
         '    tl_jni_register_hle(k_assets);\n')

    edit(tl / "husk-tl-unity-app.c",
         'void husk_native_set_system_files(const char *cacerts_dir, const char *fonts_root)\n'
         '{\n'
         '    if (cacerts_dir) tl_set_cacerts_dir(cacerts_dir);\n'
         '    if (fonts_root) tl_set_system_fonts_dir(fonts_root);\n'
         '}\n',
         'void husk_native_set_system_files(const char *cacerts_dir, const char *fonts_root)\n'
         '{\n'
         '    if (cacerts_dir) tl_set_cacerts_dir(cacerts_dir);\n'
         '    if (fonts_root) tl_set_system_fonts_dir(fonts_root);\n'
         '    /* Godot 4.7 (OS_Android::_load_system_font_config) builds its font fallback from getenv("ANDROID_ROOT") '
         'path_join("/etc/fonts.xml")\n'
         '     * and the files that lists under the same root ("fonts"). Android\'s own /system reaches an app as this '
         'folder, so point\n'
         '     * ANDROID_ROOT at it: without that the engine opens /etc/fonts.xml, fails, and draws what its font lacks as '
         'code points (#140). */\n'
         '    if (fonts_root) setenv("ANDROID_ROOT", fonts_root, 1);\n'
         '}\n')
    edit(root / "src" / "app" / "Husk" / "TLUnityView.swift", SWIFT_OLD, SWIFT_NEW)

    return 0


if __name__ == "__main__":
    sys.exit(main())
