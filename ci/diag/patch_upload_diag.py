#!/usr/bin/env python3
"""Test-build diagnostics: attribute a GL error to the upload that caused it.

Not a fix, and not part of #140. Added to husk-tl-egl.c's k_adapt table -- the path eglGetProcAddress consults -- so it sees the
engine's own calls:

  - glTexImage2D / glCompressedTexImage2D: drain glGetError before the call, read it after, print the upload with its format names
    and the texture the target held at that moment (read from the driver, not cached). Only for uploads 512 px or more on a side.
  - Nothing is changed: every wrapper calls through and returns what the driver returned.

Run: python3 ci/diag/patch_upload_diag.py <husk-checkout>
"""
import pathlib
import sys

NEW = pathlib.Path(__file__).resolve().parent / "husk-gl-upload.c"
BATCH = pathlib.Path(__file__).resolve().parent / "husk-gl-batchsum.c"


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

    (tl / NEW.name).write_text(NEW.read_text())
    print(f"  {tl / NEW.name}: written")
    (tl / BATCH.name).write_text(BATCH.read_text())
    print(f"  {tl / BATCH.name}: written")

    edit(tl / "husk-tl-egl.c",
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n',
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n'
         '/* ci/diag: attribute a GL error to the upload that raised it (drain, call, read) and print the formats by name. */\n'
         'extern void husk_gldiag_upload_set_real(void *get_error, void *get_ivec);\n'
         'extern unsigned husk_gldiag_drain(void);\n'
         'extern void husk_gldiag_upload_result(const char *fn, unsigned target, unsigned internal, unsigned format,\n'
         '                                     unsigned type, int w, int h, unsigned err, int level);\n'
         'static void (*r_glTexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);\n'
         'static void (*r_glCompressedTexImage2D)(unsigned, int, unsigned, int, int, int, int, const void *);\n'
         'static void ci_upload_real(void)\n'
         '{\n'
         '    static int done;\n'
         '    if (done) return;\n'
         '    done = 1;\n'
         '    husk_gldiag_upload_set_real((void *)a_eglGetProcAddress("glGetError"), (void *)a_eglGetProcAddress("glGetIntegerv"));\n'
         '}\n'
         'static void w_glTexImage2D(unsigned t, int lv, int ifmt, int w, int h, int b, unsigned fmt, unsigned ty, const void *px)\n'
         '{\n'
         '    ci_upload_real();\n'
         '    husk_gldiag_drain();\n'
         '    r_glTexImage2D(t, lv, ifmt, w, h, b, fmt, ty, px);\n'
         '    husk_gldiag_upload_result("glTexImage2D", t, (unsigned)ifmt, fmt, ty, w, h, husk_gldiag_drain(), lv);\n'
         '}\n'
         'static void w_glCompressedTexImage2D(unsigned t, int lv, unsigned ifmt, int w, int h, int b, int sz, const void *px)\n'
         '{\n'
         '    ci_upload_real();\n'
         '    husk_gldiag_drain();\n'
         '    r_glCompressedTexImage2D(t, lv, ifmt, w, h, b, sz, px);\n'
         '    husk_gldiag_upload_result("glCompressedTexImage2D", t, ifmt, 0, 0, w, h, husk_gldiag_drain(), lv);\n'
         '}\n')

    edit(tl / "husk-tl-egl.c",
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n',
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n'
         '    ADAPT(glTexImage2D), ADAPT(glCompressedTexImage2D),\n')

    edit(tl / "husk-tl-egl.c",
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n',
         '    ADAPT(glUniformMatrix3fv), ADAPT(glUniformMatrix4fv), ADAPT(glSampleCoverage), ADAPT(glGetString), ADAPT(glGetStringi), ADAPT(glCompileShader), ADAPT(glShaderSource), ADAPT(glAttachShader), ADAPT(glLinkProgram), ADAPT(glGetIntegerv), ADAPT(glBindBufferBase), ADAPT(glBindBuffer), ADAPT(glBufferData), ADAPT(glBufferSubData), ADAPT(glBindBufferRange),\n'
         '    { "glDrawElements", (void *)w_glDrawElements, (void **)&r_glDrawElements2 },\n'
         '    { "glDrawArrays", (void *)w_glDrawArrays, (void **)&r_glDrawArrays2 },\n')
    edit(tl / "husk-tl-egl.c",
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n',
         '#define ADAPT(name) { #name, (void *)w_##name, (void **)&r_##name }\n'
         '/* ci/diag: one frame of draws, in order, with program/texture/count -- the missing group is a set of draws. */\n'
         'extern void husk_gldiag_sum_set_real(void *get_iv);\n'
         'extern void husk_gldiag_sum_frame(unsigned long swap);\n'
         'extern void husk_gldiag_sum_draw(unsigned mode, int count, unsigned type, const void *indices);\n'
         'static void (*r_glDrawElements2)(unsigned, int, unsigned, const void *);\n'
         'static void (*r_glDrawArrays2)(unsigned, int, int);\n'
         'static int ci_batch_ready;\n'
         'static void w_glDrawElements(unsigned m, int c, unsigned t, const void *i)\n'
         '{\n'
         '    r_glDrawElements2(m, c, t, i);\n'
         '    if (!ci_batch_ready) { ci_batch_ready = 1; husk_gldiag_sum_set_real((void *)a_eglGetProcAddress("glGetIntegerv")); }\n'
         '    husk_gldiag_sum_draw(m, c, t, i);\n'
         '}\n'
         'static void w_glDrawArrays(unsigned m, int f, int c)\n'
         '{\n'
         '    r_glDrawArrays2(m, f, c);\n'
         '    if (!ci_batch_ready) { ci_batch_ready = 1; husk_gldiag_sum_set_real((void *)a_eglGetProcAddress("glGetIntegerv")); }\n'
         '    husk_gldiag_sum_draw(m, c, 0, NULL);\n'
         '}\n')

    edit(tl / "husk-tl-egl.c",
         '    if (n <= 3 || n % 600 == 0) {\n',
         '    { extern void husk_gldiag_sum_frame(unsigned long); husk_gldiag_sum_frame(n); }\n'
         '    if (n <= 3 || n % 600 == 0) {\n')

    return 0


if __name__ == "__main__":
    sys.exit(main())
