#!/usr/bin/env python3
"""Apply the #140 fix to a Husk checkout (Leviidev/Husk, main as of 2026-10-18).

Three changes:

  1. src/translation-layer-next/husk-tl-jni-godot-kotlin.c (new): Godot 4.6/4.7's Kotlin DirectoryAccessHandler / FileAccessHandler
     signatures, which GodotLib still asks for by their old Java ones, answered from the same file ids as the Java ones.
  2. husk-tl-jni-godot.c: install them (tl_godot_kotlin_install), and answer the four GodotIO methods Godot calls on
     android.content.res.AssetManager through Android (getScreenRefreshRate, getDisplaySafeArea, getLocale, getDataDir).
  3. husk-tl-jni-godot.c: GodotRenderView's pointer capture calls, which 4.7 makes and the table did not answer.

Fonts are left as they are: the missing CJK glyphs come from fonts the bundle does not carry, not from this bridge.

Run: python3 ci/scripts/patch_140.py <husk-checkout>
"""
import pathlib
import sys

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

    # ci/diag: a test build's GL hook, added to k_adapt below (diagnostics only).
    hook = pathlib.Path(__file__).resolve().parent.parent / "diag" / "husk-gl-bindhook.c"
    if not hook.exists():
        sys.exit("missing ci/diag/husk-gl-bindhook.c")
    (tl / hook.name).write_text(hook.read_text())
    print(f"  {tl / hook.name}: written (diagnostics)")

    edit(tl / "husk-tl-jni-godot.c",
         '    tl_jni_register_hle(bridge);\n',
         '    tl_jni_register_hle(bridge);\n'
         '    tl_godot_kotlin_install();\n')

    edit(tl / "husk-tl-jni-godot.c",
         'void tl_godot_hle_install(const char *pkg, const char *apk, const char *data, int w, int h)\n',
         'void tl_godot_kotlin_install(void);\n\n'
         'void tl_godot_hle_install(const char *pkg, const char *apk, const char *data, int w, int h)\n')

    # Godot 4.7 asks the render view for pointer capture on touch (#140's log showed both failing).
    edit(tl / "husk-tl-jni-godot.c",
         '    M("org/godotengine/godot/GodotRenderView", "getInputHandler", "()Lorg/godotengine/godot/input/GodotInputHandler;", RV_inputHandler),\n',
         '    M("org/godotengine/godot/GodotRenderView", "getInputHandler", "()Lorg/godotengine/godot/input/GodotInputHandler;", RV_inputHandler),\n'
         '    M("org/godotengine/godot/GodotRenderView", "requestPointerCapture", "()V", Noop),\n'
         '    M("org/godotengine/godot/GodotRenderView", "releasePointerCapture", "()V", Noop),\n')


    # ci/diag: the previous rounds wrapped GL through a path the engine never uses. k_adapt is the table eglGetProcAddress
    # consults, so this is where a wrapper is actually reached. Logs only.
    edit(tl / "husk-tl-egl.c",
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n',
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n'
         '/* ci/diag: reached through eglGetProcAddress, which is how the engine takes every GL entry point */\n'
         'extern void husk_gldiag_bind(unsigned, unsigned), husk_gldiag_texparam(unsigned, unsigned, long, unsigned);\n'
         'extern void husk_gldiag_gen(int), husk_gldiag_del(int);\n'
         'static void (*r_glBindTexture)(unsigned, unsigned);\n'
         'static void (*r_glGenTextures)(int, unsigned *);\n'
         'static void (*r_glDeleteTextures)(int, const unsigned *);\n'
         'static void (*r_glTexParameteri)(unsigned, unsigned, int);\n'
         'static void (*r_glTexParameterf)(unsigned, unsigned, float);\n'
         'static unsigned diag_take_error(void)\n'
         '{\n'
         '    static unsigned (*g)(void);\n'
         '    if (!g) g = (unsigned (*)(void))a_eglGetProcAddress("glGetError");\n'
         '    return g ? g() : 0;\n'
         '}\n'
         'static void w_glBindTexture(unsigned t, unsigned id)\n'
         '{\n'
         '    r_glBindTexture(t, id);\n'
         '    husk_gldiag_bind(t, id);\n'
         '    { unsigned e = diag_take_error(); if (e) husk_gldiag_texparam(t, 0, id, e); }\n'
         '}\n'
         'static void w_glGenTextures(int n, unsigned *ids) { r_glGenTextures(n, ids); husk_gldiag_gen(n); }\n'
         'static void w_glDeleteTextures(int n, const unsigned *ids) { r_glDeleteTextures(n, ids); husk_gldiag_del(n); }\n'
         'static void w_glTexParameteri(unsigned t, unsigned p, int v) { r_glTexParameteri(t, p, v); husk_gldiag_texparam(t, p, v, diag_take_error()); }\n'
         'static void w_glTexParameterf(unsigned t, unsigned p, float v) { r_glTexParameterf(t, p, v); husk_gldiag_texparam(t, p, (long)v, diag_take_error()); }\n')

    edit(tl / "husk-tl-egl.c",
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n',
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n'
         '    ADAPT(glBindTexture), ADAPT(glGenTextures), ADAPT(glDeleteTextures), ADAPT(glTexParameteri), ADAPT(glTexParameterf),\n'
         '    ADAPT(glTexParameteri), ADAPT(glTexParameterf),\n')

    edit(tl / "husk-tl-egl.c",
         '    a_glGetIntegerv = a_eglGetProcAddress("glGetIntegerv");\n',
         '    a_glGetIntegerv = a_eglGetProcAddress("glGetIntegerv");\n'
         '    { extern void husk_gldiag_set_real(void *); husk_gldiag_set_real((void *)a_glGetIntegerv); }\n')

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

    return 0


if __name__ == "__main__":
    sys.exit(main())
