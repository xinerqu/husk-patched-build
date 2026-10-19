/* Legacy LUMINANCE / LUMINANCE_ALPHA / ALPHA uploads, rewritten to formats GLES 3.0 requires drivers to support.
 *
 * What was seen: one texture upload in a run refused outright --
 *
 *     glTexImage2D(internal=GL_LUMINANCE_ALPHA, format=GL_LUMINANCE_ALPHA, type=GL_UNSIGNED_BYTE, 256x256) -> GL_INVALID_ENUM
 *
 * GL_LUMINANCE, GL_LUMINANCE_ALPHA and GL_ALPHA are GLES 2.0 formats. In a GLES 3.0 context they are not core, and ANGLE answers a
 * request for them with GL_INVALID_ENUM. The texture then exists with no content, the engine goes on drawing with it, and whatever
 * used that texture simply does not appear -- with no error from the engine, which is the shape of the report this was found in.
 *
 * The GLES 3.0 equivalents hold the same channels under different names, so the upload can be rewritten:
 *
 *     GL_LUMINANCE        (L)        -> GL_RED   (R)
 *     GL_LUMINANCE_ALPHA  (L, A)     -> GL_RG    (R, G)      -- alpha kept, see below
 *     GL_ALPHA            (A)        -> GL_RED   (R)
 *
 * The bytes are laid out per pixel in the same order, so for unsized internal formats the pixel data can be passed through unchanged;
 * only the enums change. LUMINANCE_ALPHA keeps both channels (RG rather than R) so nothing an engine sampled is lost -- with an
 * unsized GL_RG internal format the driver chooses RG8, two bytes a pixel, exactly what LUMINANCE_ALPHA is.
 *
 * A size change is all this cannot do: it keeps the same width, height and type, so no resampling is involved.
 *
 * Generic: every engine that still uploads these three formats gets a texture instead of a refused call.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef int GLint;

#define GL_ALPHA 0x1906
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_RED 0x1903
#define GL_RG 0x8227

/* The internal format to use instead, or the same value when nothing needs to change. */
GLenum husk_gl_legacy_internal(GLenum internal)
{
    switch (internal) {
    case GL_LUMINANCE: return GL_RED;
    case GL_LUMINANCE_ALPHA: return GL_RG;
    case GL_ALPHA: return GL_RED;
    default: return internal;
    }
}

/* The pixel format to use instead, or the same value when nothing needs to change. */
GLenum husk_gl_legacy_format(GLenum format)
{
    switch (format) {
    case GL_LUMINANCE: return GL_RED;
    case GL_LUMINANCE_ALPHA: return GL_RG;
    case GL_ALPHA: return GL_RED;
    default: return format;
    }
}

/* True when this upload uses a legacy format, so the caller knows it changed something worth counting. */
int husk_gl_legacy(GLenum internal, GLenum format)
{
    return husk_gl_legacy_internal(internal) != internal || husk_gl_legacy_format(format) != format;
}
