#!/usr/bin/env python3
"""A font file Godot cannot find under <ANDROID_ROOT>/fonts is looked for in the Settings download folder.

Godot 4 asks for each face that fonts.xml names at <ANDROID_ROOT>/fonts/<filename>, and skips an entry whose file is not there:

    String root = String(getenv("ANDROID_ROOT")).path_join("fonts");   // os_android.cpp:514, 652, 704
    if (da->file_exists(root.path_join(fi.filename))) { ... }

ANDROID_ROOT names the android-system tree the app carries, which holds 205 faces but not NotoSansCJK-Regular.ttc -- the bundle
leaves the 20 MB CJK face out, and Settings downloads it to Application Support/Fonts instead. So the one entry that matters for
Chinese is the one Godot skips, and CJK text draws as boxes.

The fallback goes at the end of tl_path_resolve, OUTSIDE the g_sysfonts block: it must work whether or not the app supplied the
android-system tree, because it depends only on the Settings folder. A ".../fonts/<name>" that names no existing file is looked for
as <Settings folder>/<name>; a name that exists, or one that is nowhere, is left exactly as it was.

(An earlier version of this patch sat inside the g_sysfonts block, so it did nothing whenever that variable was empty -- which is
one of the things the trace line this patch adds would have shown.)
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
         'const char *tl_path_resolve(const char *path, char *buf, size_t n)\n'
         '{\n'
         '    if (!path) return path;\n'
         '    if (g_sysfonts[0]) {\n',
         'const char *tl_path_resolve(const char *path, char *buf, size_t n)\n'
         '{\n'
         '    if (!path) return path;\n'
         '    /* A font the app says it has, downloaded to the Settings folder rather than carried in android-system. Godot 4 asks\n'
         '     * for <ANDROID_ROOT>/fonts/<name> (os_android.cpp:514, 652, 704) and skips a face whose file is not there, so the CJK\n'
         '     * face -- 20 MB, not in the bundle -- is the one it skips. Look for a name that is not on disk in the Settings folder\n'
         '     * too. Outside the g_sysfonts block on purpose: this depends on nothing but g_extrafonts. */\n'
         '    {\n'
         '        static const char k_fonts[] = "/fonts/";\n'
         '        const char *fp = strstr(path, k_fonts);\n'
         '        if (fp && g_extrafonts[0] && access(path, F_OK) != 0) {\n'
         '            char extra[1100];\n'
         '            /* fp points at the "/" of "/fonts/"; keep it, so the name lands under <dir>/<name> */\n'
         '            snprintf(extra, sizeof(extra), "%s%s", g_extrafonts, fp + sizeof(k_fonts) - 2);\n'
         '            /* test build: one line per distinct font path, so a lookup that misses says why */\n'
         '            { static char seen[24][256]; static int nseen; int known = 0;\n'
         '              for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], path)) { known = 1; break; }\n'
         '              if (!known && nseen < 24) {\n'
         '                  snprintf(seen[nseen++], 256, "%s", path);\n'
         '                  tl_log_line("font: want=%s exists=0 extra=%s hit=%d", path, g_extrafonts, access(extra, F_OK) == 0 ? 1 : 0);\n'
         '              } }\n'
         '            if (access(extra, F_OK) == 0) { snprintf(buf, n, "%s", extra); return buf; }\n'
         '        }\n'
         '    }\n'
         '    if (g_sysfonts[0]) {\n')

    return 0


if __name__ == "__main__":
    sys.exit(main())
