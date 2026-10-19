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

    # The test build's rendering diagnostics (not part of the fix): makes the renderer say which texture formats it has and any
    # GL error it raises, so a log can explain "assets load but do not draw".
    for part, name in (("diag", "husk-gl-diag.c"), ("diag", "husk-gl-texdiag.c"), ("diag", "husk-gl-probe.c"),
                       ("fix", "husk-gl-angle-retry.c")):
        src = pathlib.Path(__file__).resolve().parent.parent / part / name
        if src.exists():
            (tl / name).write_text(src.read_text())
            print(f"  {tl / name}: written ({part})")

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

    # A test build only (ci/diag): the renderer cannot say why an asset does not draw, and this makes it say.
    edit(tl / "husk-tl-godot.c",
         '    if (swapInterval) swapInterval(dpy, 1);\n',
         '    if (swapInterval) swapInterval(dpy, 1);\n'
         '    extern void husk_gl_diagnostics_install(void);\n'
         '    husk_gl_diagnostics_install();   /* ci/diag: prints the GPU\'s texture formats and turns on GL error output */\n')
    edit(tl / "husk-tl-godot.c",
         '        if (step(env, cls)) swapBuffers(dpy, surf);\n',
         '        if (step(env, cls)) swapBuffers(dpy, surf);\n'
         '        { extern void husk_gl_diagnostics_check(const char *); husk_gl_diagnostics_check("a frame"); }\n')

    # ci/diag, second pass: catch a GL_INVALID_ENUM at the texture call that raised it.
    edit(tl / "husk-tl-egl.c",
         '    if (n <= 3 || n % 600 == 0) {\n',
         '    { extern void husk_gl_diag_swap(unsigned long); husk_gl_diag_swap(n); }   /* ci/diag: the frame an error is quoted against */\n'
         '    if (n <= 3 || n % 600 == 0) {\n')

    edit(tl / "husk-tl-egl.c",
         'static EGLint (*r_eglClientWaitSyncKHR)(EGLDisplay, void *, EGLint, uint64_t);',
         'static EGLint (*r_eglClientWaitSyncKHR)(EGLDisplay, void *, EGLint, uint64_t);')

    edit(tl / "husk-tl-egl.c",
         '    { extern void husk_gl_diag_swap(unsigned long); husk_gl_diag_swap(n); }   /* ci/diag: the frame an error is quoted against */\n',
         '    { extern void husk_gl_diag_swap(unsigned long); husk_gl_diag_swap(n); }   /* ci/diag: the frame an error is quoted against */\n'
         '    { extern void husk_gl_frame_end(unsigned long); husk_gl_frame_end(n); }\n')

    edit(tl / "husk-tl-egl.c",
         '    if (!real) return NULL;\n',
         '    if (!real) return NULL;\n'
         '    { extern void *husk_gl_diag_install(const char *, void *); void *wrapped = husk_gl_diag_install(name, real);\n'
         '      if (wrapped != real) return wrapped; }\n')

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
