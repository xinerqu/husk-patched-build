/* Stand-ins for the parts of Husk's JNI layer the Godot Kotlin handlers touch, so the file can be exercised on a machine with a C
 * compiler: a tiny jobj world (strings only), an HLE registry that behaves like husk-tl-jni.c's lookup (by class, name and
 * signature), and the few paths the directory and file calls use. Not part of any build of Husk. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "husk-tl-jni.h"

/* One scratch buffer per jobj, for the address/capacity fields a direct ByteBuffer carries. */
typedef struct { void *address; int64_t capacity; } testbuf;
static testbuf *buf(jobj *o)
{
    if (!o->native) o->native = calloc(1, sizeof(testbuf));
    return o->native;
}

jobj *tl_jni_new_string(const char *s)
{
    jobj *o = calloc(1, sizeof(*o));
    o->kind = TL_K_STRING;
    o->str.utf8 = strdup(s ? s : "");
    return o;
}

const char *tl_jni_string(const jobj *o) { return o ? o->str.utf8 : NULL; }

void tl_jni_unref(jobj *o) { if (o) { free(o->str.utf8); free(o->native); free(o); } }

/* The two ByteBuffer fields the file calls read: the buffer's address (the bytes) and its capacity. jobj carries both. */
void tl_jni_set_field(jobj *o, const char *name, const char *sig, jvalue v)
{
    (void)sig;
    if (!strcmp(name, "address")) buf(o)->address = v.l;
    if (!strcmp(name, "capacity")) buf(o)->capacity = v.j;
}
jvalue tl_jni_get_field(jobj *o, const char *name, const char *sig)
{
    jvalue v; v.j = 0;
    (void)sig;
    if (!strcmp(name, "address")) v.l = buf(o)->address;
    if (!strcmp(name, "capacity")) v.j = buf(o)->capacity;
    return v;
}

void tl_jni_throw(const char *cls, const char *msg) { (void)cls; (void)msg; }

void tl_log_line(const char *fmt, ...)
{
    if (!getenv("TEST_VERBOSE")) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap);
}

/* Like hle_find in husk-tl-jni.c: the class, the name, and the exact signature. */
static const tl_jhle *g_hle[8];
static size_t g_nhle;
static void (*g_install)(void);

/* Like husk-tl-jni.c: every table registered, searched in order. */
void tl_jni_register_hle(const tl_jhle *table)
{
    if (g_nhle < sizeof(g_hle) / sizeof(g_hle[0])) g_hle[g_nhle++] = table;
}
void test_set_install(void (*fn)(void)) { g_install = fn; }

tl_jhle_fn test_find(const char *cls, const char *name, const char *sig)
{
    if (!g_nhle && g_install) g_install();
    for (size_t t = 0; t < g_nhle; t++)
        for (const tl_jhle *e = g_hle[t]; e && e->cls; e++)
            if (!strcmp(e->cls, cls) && !strcmp(e->name, name) && !strcmp(e->sig, sig)) return e->fn;
    return NULL;
}

/* husk-tl-bionic-io.c's mapping of Android's paths, cut down to what the handlers meet: the app's own files. */
const char *tl_path_resolve(const char *path, char *buf, size_t n)
{
    snprintf(buf, n, "%s", path);
    return path;
}
