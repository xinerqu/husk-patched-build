#!/usr/bin/env python3
"""Test build: make Godot write its own engine log, which a release export does not do.

Godot writes user://logs/godot.log only in debug builds; a game exported in release keeps its log on stderr alone. The file the
Files app shows under godot-data has no logs/ directory for exactly that reason -- so the engine's own view of this run has never
been read.

Godot takes --log-file <path> on the command line for any build. This adds it, pointing into the game's data directory (which is
user:// mapped to a real folder the app can write), so the next run leaves the engine's own log where it can be picked up.

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

    edit(tl / "husk-tl-godot.c",
         '    if (U.major >= 4 && !getenv("TL_GODOT_VULKAN") && n + 4 <= 32) {\n'
         '        static const char *gl[] = { "--rendering-driver", "opengl3", "--rendering-method", "gl_compatibility" };\n'
         '        for (int i = 0; i < 4; i++) list[n++] = strdup(gl[i]);\n'
         '    }\n',
         '    if (U.major >= 4 && !getenv("TL_GODOT_VULKAN") && n + 4 <= 32) {\n'
         '        static const char *gl[] = { "--rendering-driver", "opengl3", "--rendering-method", "gl_compatibility" };\n'
         '        for (int i = 0; i < 4; i++) list[n++] = strdup(gl[i]);\n'
         '    }\n'
         '    /* test build: a release export keeps its log on stderr only. Ask for a file, in the game data folder the Files app\n'
         '     * shows (user:// is mapped there). */\n'
         '    if (n + 2 <= 32) {\n'
         '        static char logpath[600];\n'
         '        snprintf(logpath, sizeof(logpath), "%s/godot.log", U.data);\n'
         '        list[n++] = strdup("--log-file");\n'
         '        list[n++] = strdup(logpath);\n'
         '    }\n')

    return 0


if __name__ == "__main__":
    sys.exit(main())
