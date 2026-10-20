#!/usr/bin/env python3
"""Test build: say what Godot actually asks for when it looks up a font, and what the fallback decides.

The ANDROID_ROOT fix removed the font-config errors, and the Settings folder holds the CJK face, yet CJK text still draws as boxes.
The fallback added for that case (tl_path_resolve: a ".../fonts/<name>" that is not on disk is looked for in g_extrafonts) either
never runs or misses, and there is nothing in the log to tell which.

This prints, once per distinct font path:

    font: want=<what Godot asked for> exists=<is it on disk> extra=<the Settings folder> hit=<did the fallback find it>

A first line with extra=(none) means the Settings folder was never handed to the native side; a want= without /fonts/ means the
path is not shaped the way the fallback expects; a hit=0 with a real extra= means the file name differs between the two.
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
         '        static const char k_fonts[] = "/fonts/";\n'
         '        const char *fp = strstr(path, k_fonts);\n'
         '        if (fp && g_extrafonts[0] && access(path, F_OK) != 0) {\n',
         '        static const char k_fonts[] = "/fonts/";\n'
         '        const char *fp = strstr(path, k_fonts);\n'
         '        /* test build: one line per distinct font path, so a lookup that misses says why */\n'
         '        { static char seen[24][256]; static int nseen;\n'
         '          int known = 0;\n'
         '          for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], path)) { known = 1; break; }\n'
         '          if (!known && nseen < 24 && (strstr(path, ".ttf") || strstr(path, ".otf") || strstr(path, ".ttc"))) {\n'
         '              snprintf(seen[nseen++], 256, "%s", path);\n'
         '              char probe[1100] = "";\n'
         '              if (fp && g_extrafonts[0]) snprintf(probe, sizeof(probe), "%s%s", g_extrafonts, fp + sizeof(k_fonts) - 2);\n'
         '              tl_log_line("font: want=%s exists=%d extra=%s hit=%d", path, access(path, F_OK) == 0 ? 1 : 0,\n'
         '                          g_extrafonts[0] ? g_extrafonts : "(none)", probe[0] ? (access(probe, F_OK) == 0 ? 1 : 0) : -1);\n'
         '          } }\n'
         '        if (fp && g_extrafonts[0] && access(path, F_OK) != 0) {\n')

    return 0


if __name__ == "__main__":
    sys.exit(main())
