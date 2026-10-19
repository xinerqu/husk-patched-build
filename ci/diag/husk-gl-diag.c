/* A test build's rendering diagnostics, for the "assets load but do not draw" report that came after #140's directory fix. Nothing
 * here changes what the game draws: it only makes the renderer say what it is doing -- which GPU features it sees, which texture
 * formats it falls back from, and any OpenGL error it raises -- so a log can tell "not drawn" from "drawn where it cannot be seen".
 *
 * Godot's Compatibility renderer only talks about this under --verbose, and only registers its GL debug callback on desktop GL
 * (rasterizer_gles3.cpp's GLAD branch); on ES it leaves the Android branch to `eglGetProcAddress` + glDebugMessageCallback, which
 * ANGLE exports. So the same three Windows controls are registered here, at start-up, for the log's sake.
 *
 * Diagnostic build only: not part of the fix proposed in #140.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "husk-tl-bionic.h"
#include "husk-tl-jni.h"

void *tl_egl_resolve(const char *name);

/* --------------------------------------------------------------- GL, by handle */

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef unsigned GLsizei;
typedef unsigned GLbitfield;

static GLenum (*x_glGetError)(void);
static const unsigned char *(*x_glGetString)(GLenum);
static void (*x_glGetIntegerv)(GLenum, GLint *);
static void (*x_glEnable)(GLenum);

static void *x_glDebugMessageCallback;
static void *x_glDebugMessageControl;

#define GL_EXTENSIONS 0x1F03
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_3D_TEXTURE_SIZE 0x8073
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_DEBUG_OUTPUT 0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_DEBUG_SOURCE_API 0x8246
#define GL_DEBUG_SOURCE_OTHER 0x824B
#define GL_DONT_CARE 0x1100
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#define GL_DEBUG_SEVERITY_LOW 0x9148

static int g_errors_logged;

typedef void (*gl_debug_proc)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *user);
typedef void (*gl_callback_fn)(gl_debug_proc callback, const void *user);
typedef void (*gl_control_fn)(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, bool enabled);

static void on_gl_message(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *user)
{
    (void)type; (void)id; (void)user;
    if (g_errors_logged++ > 200) return;
    tl_log_line("gl: [source %#x severity %#x] %.*s", source, severity, (int)(length > 0 ? length : (GLsizei)strlen(message)), message);
}

/* ------------------------------------------------------------ what the GPU has */

/* The texture compressions a Compatibility project can be exported with; whether the device has each decides both whether a
 * texture is uploaded compressed or decompressed, and what the game sees when it asks. */
static const char *const k_formats[] = {
    "GL_EXT_texture_compression_s3tc", "GL_EXT_texture_compression_dxt1", "GL_ANGLE_texture_compression_dxt3",
    "GL_ANGLE_texture_compression_dxt5", "GL_EXT_texture_compression_etc2", "GL_OES_compressed_ETC1_RGB8_texture",
    "GL_EXT_texture_compression_astc_ldr", "GL_EXT_texture_compression_astc_sliced_3d", "GL_EXT_texture_compression_rgtc",
    "GL_EXT_texture_compression_bptc", "GL_OES_texture_compression_astc", "GL_KHR_texture_compression_astc_ldr",
    "GL_EXT_sRGB", "GL_OES_texture_float", "GL_EXT_color_buffer_float", "GL_EXT_texture_filter_anisotropic",
    "GL_EXT_texture_storage", "GL_OES_texture_storage_multisample_2d_array", "GL_EXT_multisampled_render_to_texture",
    NULL
};

static bool ext_has(const char *all, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = all; p && *p; ) {
        const char *e = strchr(p, ' ');
        size_t m = e ? (size_t)(e - p) : strlen(p);
        if (m == n && !strncmp(p, name, n)) return true;
        p += m; if (e) p++;
    }
    return false;
}

static void report_gpu(void)
{
    if (!x_glGetString || !x_glGetIntegerv) { tl_log_line("gl: diagnostics unavailable (no glGetString)"); return; }
    const char *ext = (const char *)x_glGetString(GL_EXTENSIONS);
    GLint max2d = 0, max3d = 0, units = 0, combined = 0;
    x_glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max2d);
    x_glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3d);
    x_glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    x_glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combined);
    tl_log_line("gl: max_texture_size=%d max_3d=%d units=%d combined=%d", max2d, max3d, units, combined);
    char line[1024]; size_t k = 0; line[0] = 0;
    for (int i = 0; k_formats[i]; i++) {
        const char *fmt = k_formats[i] + 3;     /* without GL_ */
        if (ext && ext_has(ext, k_formats[i])) k += (size_t)snprintf(line + k, sizeof(line) - k, "%s ", fmt);
    }
    tl_log_line("gl: texture formats present: %s", k ? line : "(none of the compressed ones)");
    size_t total = ext ? strlen(ext) : 0;
    tl_log_line("gl: %zu extensions", total);
}

/* ------------------------------------------------- Godot's own GL error channel */

static void enable_debug_output(void)
{
    if (!x_glDebugMessageCallback) x_glDebugMessageCallback = tl_egl_resolve("glDebugMessageCallback");
    if (!x_glDebugMessageCallback) x_glDebugMessageCallback = tl_egl_resolve("glDebugMessageCallbackKHR");
    if (!x_glDebugMessageControl) x_glDebugMessageControl = tl_egl_resolve("glDebugMessageControl");
    if (!x_glDebugMessageControl) x_glDebugMessageControl = tl_egl_resolve("glDebugMessageControlKHR");
    if (!x_glDebugMessageCallback || !x_glEnable) { tl_log_line("gl: this ANGLE has no glDebugMessageCallback; GL errors are not reported"); return; }
    ((gl_callback_fn)x_glDebugMessageCallback)(on_gl_message, NULL);
    x_glEnable(GL_DEBUG_OUTPUT);
    x_glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    if (x_glDebugMessageControl) {
        gl_control_fn ctl = (gl_control_fn)x_glDebugMessageControl;
        ctl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, true);
        ctl(GL_DEBUG_SOURCE_API, GL_DEBUG_SOURCE_OTHER, GL_DONT_CARE, 0, NULL, true);
    }
    tl_log_line("gl: GL debug output enabled (errors will be logged as they happen)");
}

/* ------------------------------------------------------- what the renderer does */

/* Called once, from the GL thread just after the context is current, before Godot's first frame: this is where the driver's
 * entry points can be resolved. Always on in a build that carries this file (the test build of #140). */
void husk_gl_diagnostics_install(void)
{
    x_glGetError = tl_egl_resolve("glGetError");
    x_glGetString = tl_egl_resolve("glGetString");
    x_glGetIntegerv = tl_egl_resolve("glGetIntegerv");
    x_glEnable = tl_egl_resolve("glEnable");
    report_gpu();
    enable_debug_output();
}

/* Anything Godot leaves unread: reported at the end of a frame, so a failing draw call has a name. */
void husk_gl_diagnostics_check(const char *where)
{
    if (!x_glGetError) return;
    /* Once a second: glGetError can stall a pipeline, and a log entry per frame teaches nothing more than one per second. */
    static long calls;
    if ((calls++ % 30) != 0) return;
    GLenum e = x_glGetError();
    if (e && g_errors_logged++ < 200) tl_log_line("gl: error %#x after %s", e, where ? where : "a frame");
}
