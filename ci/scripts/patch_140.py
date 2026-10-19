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

    st = pathlib.Path(__file__).resolve().parent.parent / "diag" / "husk-gl-state.c"
    if st.exists():
        (tl / st.name).write_text(st.read_text())
        print(f"  {tl / st.name}: written (diagnostics)")

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


    # ci/diag (test build): the areas that are missing raise no error; watch the state that can make a draw vanish silently.
    edit(tl / "husk-tl-egl.c",
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n',
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n'
         '/* ci/diag: scissor, blending, colour mask and the draw count -- none of these errors, so nothing reports them */\n'
         'extern void husk_gldiag_scissor(int, int, int, int);\n'
         'extern void husk_gldiag_blend_enabled(int);\n'
         'extern void husk_gldiag_blend_mode(unsigned, unsigned);\n'
         'extern void husk_gldiag_draw(void);\n'
         'static void (*r_glScissor)(int, int, int, int);\n'
         'static void (*r_glBlendFunc)(unsigned, unsigned);\n'
         'static void (*r_glEnable)(unsigned);\n'
         'static void (*r_glDisable)(unsigned);\n'
         'static void w_glScissor(int x, int y, int w, int h) { r_glScissor(x, y, w, h); husk_gldiag_scissor(x, y, w, h); }\n'
         'static void w_glBlendFunc(unsigned s, unsigned d) { r_glBlendFunc(s, d); husk_gldiag_blend_mode(s, d); }\n'
         
         'static void w_glEnable(unsigned c) { r_glEnable(c); if (c == 0x0BE2) husk_gldiag_blend_enabled(1); }\n'
         'static void w_glDisable(unsigned c) { r_glDisable(c); if (c == 0x0BE2) husk_gldiag_blend_enabled(0); }\n'
         'static void (*r_glDrawElements)(unsigned, int, unsigned, const void *);\n'
         'static void (*r_glDrawArrays)(unsigned, int, int);\n'
         'static void w_glDrawElements(unsigned m, int c, unsigned t, const void *i) { husk_gldiag_draw(); r_glDrawElements(m, c, t, i); }\n'
         'static void w_glDrawArrays(unsigned m, int f, int c) { husk_gldiag_draw(); r_glDrawArrays(m, f, c); }\n')

    edit(tl / "husk-tl-egl.c",
         '    if (n <= 3 || n % 600 == 0) {\n',
         '    { extern void husk_gldiag_frame(unsigned long); husk_gldiag_frame(n); }\n'
         '    if (n <= 3 || n % 600 == 0) {\n')

    edit(tl / "husk-tl-egl.c",
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n',
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n'
         '    ADAPT(glScissor), ADAPT(glBlendFunc), ADAPT(glEnable), ADAPT(glDisable),\n'
         '    ADAPT(glDrawElements), ADAPT(glDrawArrays),\n')

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
