/* Test build: are the wrappers actually reached?
 *
 * Earlier rounds wrapped glBindTexture / glGenTextures / glTexParameter* by hooking husk's own resolve path, and the logs showed
 * they never ran: Godot's GLAD takes every GL entry point through eglGetProcAddress, and husk only wraps the names listed in
 * k_adapt. These are added to k_adapt -- the one place eglGetProcAddress consults -- so they are reached however the guest asks.
 *
 * What one run answers:
 *   - is glBindTexture reached at all, and does husk's binding match what ANGLE reports right after it
 *   - what the binding is when the glTexParameteri that fails happens
 *   - whether texture names are generated and deleted as often as the earlier logs suggested
 *
 * Diagnostic only: every wrapper calls through and returns what the driver returned.
 * Not part of the fix proposed in #140.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;

static void (*R_GetIntegerv)(GLenum, GLint *);

#define GL_BINDING_2D 0x8069
#define GL_BINDING_3D 0x806A
#define GL_BINDING_CUBE 0x8514

static long g_binds, g_texparams, g_gens, g_dels, g_reported;
static int g_first_lines;

void husk_gldiag_set_real(void *get_ivec) { R_GetIntegerv = (void (*)(GLenum, GLint *))get_ivec; }

static void angle_binding(GLint *twod, GLint *threed, GLint *cube)
{
    *twod = *threed = *cube = -1;
    if (!R_GetIntegerv) return;
    R_GetIntegerv(GL_BINDING_2D, twod);
    R_GetIntegerv(GL_BINDING_3D, threed);
    R_GetIntegerv(GL_BINDING_CUBE, cube);
}

void husk_gldiag_bind(GLenum target, GLuint id)
{
    g_binds++;
    if (g_first_lines < 8) {
        g_first_lines++;
        GLint a = -1, b = -1, c = -1;
        angle_binding(&a, &b, &c);
        tl_log_line("gl: BINDCALL target=%#x id=%u -> ANGLE says 2D=%d 3D=%d cube=%d", target, id, a, b, c);
    }
    if (g_binds - g_reported >= 2000) {
        g_reported = g_binds;
        GLint a = -1, b = -1, c = -1;
        angle_binding(&a, &b, &c);
        tl_log_line("gl: BINDCALL %ld bindings, %ld params, %ld names generated, %ld deleted (ANGLE 2D=%d)",
                    g_binds, g_texparams, g_gens, g_dels, a);
    }
}

void husk_gldiag_texparam(GLenum target, GLenum pname, long value, unsigned err)
{
    g_texparams++;
    if (!err) return;
    GLint a = -1, b = -1, c = -1;
    angle_binding(&a, &b, &c);
    tl_log_line("gl: TEXPARAM FAILED target=%#x pname=%#x value=%ld err=%#x | ANGLE 2D=%d 3D=%d cube=%d | %ld params",
                target, pname, value, err, a, b, c, g_texparams);
}

void husk_gldiag_gen(int n) { g_gens += n; }
void husk_gldiag_del(int n) { g_dels += n; }

void husk_gldiag_installed(void) { tl_log_line("gl: bind / texparam / name hooks installed (test build)"); }
