#!/usr/bin/env python3
"""The CJK font downloaded in Settings: let a font file Godot cannot find under <ANDROID_ROOT>/fonts be found there instead.

Godot 4 looks for each face that fonts.xml names at <ANDROID_ROOT>/fonts/<filename>, and skips an entry whose file is not there:

    String root = String(getenv("ANDROID_ROOT")).path_join("fonts");   // os_android.cpp:514, 652, 704
    if (da->file_exists(root.path_join(fi.filename))) { ... }

ANDROID_ROOT now names the android-system tree the app carries, which holds 205 faces but not NotoSansCJK-Regular.ttc -- the bundle
leaves the 20 MB CJK face out, and Settings downloads it to Application Support/Fonts instead. So the one entry that matters for
Chinese is the one Godot skips, and CJK text draws as boxes.

tl_path_resolve() already redirects an Android font path (/system/fonts/<name>) to the Settings folder when the bundled copy is
missing; Godot's path is not of that form. This adds the same fallback for any ".../fonts/<name>" that names no existing file: try
the Settings folder, and use it if the file is there.

Nothing changes for a name that does exist, or when no CJK font has been downloaded.
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

    edit(tl / "husk-tl-bionic-io.c",
         '        if (!strcmp(path, "/system/etc/fonts.xml") || !strcmp(path, "/system/etc/font_fallback.xml")) { snprintf(buf, n, "%s/etc%s", g_sysfonts, path + 11); return buf; }\n'
         '    }\n',
         '        if (!strcmp(path, "/system/etc/fonts.xml") || !strcmp(path, "/system/etc/font_fallback.xml")) { snprintf(buf, n, "%s/etc%s", g_sysfonts, path + 11); return buf; }\n'
         '        /* Godot 4 asks for <ANDROID_ROOT>/fonts/<name> rather than /system/fonts/<name> (os_android.cpp:514, 652, 704).\n'
         '         * The android-system tree has 205 faces but not the CJK one, which Settings downloads to g_extrafonts; let a\n'
         '         * name that is not beside the others be found there instead (this is where the CJK face the app says it has,\n'
         '         * but was not told where to look, finally is). */\n'
         '        static const char k_fonts[] = "/fonts/";\n'
         '        const char *fp = strstr(path, k_fonts);\n'
         '        if (fp && g_extrafonts[0] && access(path, F_OK) != 0) {\n'
         '            char extra[1100];\n'
         '            /* fp points at the "/" of "/fonts/"; keep it, so the name lands under <dir>/<name> */\n'
         '            snprintf(extra, sizeof(extra), "%s%s", g_extrafonts, fp + sizeof(k_fonts) - 2);\n'
         '            if (access(extra, F_OK) == 0) { snprintf(buf, n, "%s", extra); return buf; }\n'
         '        }\n'
         '    }\n')

    return 0


if __name__ == "__main__":
    sys.exit(main())
