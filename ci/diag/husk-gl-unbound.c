/* Test build: an upload with nothing bound -- who leaves the binding at zero?
 *
 * The one refused call in this scene is:
 *
 *     glTexImage2D(target=GL_TEXTURE_2D, internal=GL_RG, format=GL_RG, type=GL_UNSIGNED_BYTE, 256x256) -> GL_INVALID_ENUM
 *     with no texture bound (tex=0)
 *
 * Rewriting the format (GL_LUMINANCE_ALPHA -> GL_RG) did not help, because the format was never the problem: an upload with the
 * default texture bound to its target is refused whatever the format. The texture then holds nothing, the engine goes on drawing
 * with it, and only the artwork that uses it disappears -- which is what the report describes (every other screen draws normally;
 * one scene's background does not).
 *
 * So record the state at every upload that finds nothing bound: the caller's return address (to tell whose upload it is, from the
 * binary's symbol table), what was bound to the other targets, and the last few bindings before it. A binding that is set, then
 * cleared, immediately before the upload says who cleared it.
 *
 * Reached through husk's k_adapt table. Diagnostic only.
 *
 * Not part of #140's directory fix.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;

void *tl_egl_resolve(const char *name);

#define GL_BINDING_2D 0x8069
#define GL_BINDING_3D 0x806A
#define GL_BINDING_CUBE 0x8514

static void (*get_ivec)(GLenum, GLint *);
static int g_reports, g_budget = 10;

/* The ring of the last bindings: id and a marker of the target, so the log can show a bind followed by an unbind. */
#define RING 16
static struct { unsigned target; unsigned id; } g_bind_ring[RING];
static int g_bind_n;

void husk_gldiag_bind_note(unsigned target, unsigned id)
{
    g_bind_ring[g_bind_n % RING].target = target;
    g_bind_ring[g_bind_n % RING].id = id;
    g_bind_n++;
}

static void ensure(void)
{
    if (!get_ivec) get_ivec = (void (*)(GLenum, GLint *))tl_egl_resolve("glGetIntegerv");
}

unsigned husk_gldiag_bound_of(unsigned target)
{
    ensure();
    GLint v = 0;
    if (!get_ivec) return 0xFFFFFFFFu;
    switch (target) {
    case 0x0DE1: get_ivec(GL_BINDING_2D, &v); break;
    case 0x806F: get_ivec(GL_BINDING_3D, &v); break;
    case 0x8513: get_ivec(GL_BINDING_CUBE, &v); break;
    default: get_ivec(GL_BINDING_2D, &v); break;
    }
    return (GLuint)v;
}

void husk_gldiag_bound_state(unsigned target, unsigned bound, unsigned caller)
{
    if (bound != 0) return;                      /* a normal upload */
    g_reports++;
    if (g_budget-- <= 0) return;

    GLint t2 = -1, t3 = -1, cube = -1;
    if (get_ivec) { get_ivec(GL_BINDING_2D, &t2); get_ivec(GL_BINDING_3D, &t3); get_ivec(GL_BINDING_CUBE, &cube); }
    tl_log_line("gl: UNBOUND UPLOAD #%d target=%#x caller=%#x | now bound 2D=%d 3D=%d cube=%d | reports so far %d",
                g_reports, target, caller, t2, t3, cube, g_reports);

    char seq[320]; size_t k = 0; seq[0] = 0;
    for (int i = 1; i <= RING && i <= g_bind_n; i++) {
        int idx = (g_bind_n - i) % RING;
        k += (size_t)snprintf(seq + k, sizeof(seq) - k, "%s%#x:%u", i == 1 ? "" : " <- ", g_bind_ring[idx].target, g_bind_ring[idx].id);
        if (k + 32 >= sizeof(seq)) break;
    }
    tl_log_line("gl:   last bindings (newest first): %s", k ? seq : "(none)");
}
