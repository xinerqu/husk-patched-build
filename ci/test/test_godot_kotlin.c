/* Exercises the Godot 4.7 bridge additions the way the engine uses them, on a real directory:
 *
 *   - the Kotlin signatures are found (dirOpen(I), dirNext(I), fileWrite->Z, fileResize->V ...), and the Java ones stay;
 *   - a save written through fileOpen/fileWrite into a folder that does not exist yet lands there (the case that failed: user://
 *     cannot be made, so the folder game data lives in is missing);
 *   - listing that folder finds the save, and rename replaces an existing file (iOS's rename does not);
 *   - dirExists makes a folder it is asked about (what the phone's FilesystemDirectoryAccess.makeDir does).
 *
 *   cc -D_GNU_SOURCE -I<husk>/src/translation-layer-next -I<husk>/src/translation-layer -I. \
 *      test_godot_kotlin.c stubs.c <patch>/husk-tl-jni-godot-kotlin.c -o test && ./test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "husk-tl-jni.h"

void test_set_install(void (*fn)(void));
tl_jhle_fn test_find(const char *cls, const char *name, const char *sig);

extern void tl_godot_kotlin_install(void);

#define DA "org/godotengine/godot/io/directory/DirectoryAccessHandler"
#define FA "org/godotengine/godot/io/file/FileAccessHandler"

static int failures;

static jvalue V(int64_t j) { jvalue v; v.j = j; return v; }

/* Call an implementation the way husk-tl-jni.c's invoke() does: the arguments in the signature's order. */
static jvalue call(const char *cls, const char *name, const char *sig, jvalue *args)
{
    tl_jhle_fn fn = test_find(cls, name, sig);
    if (!fn) { fprintf(stderr, "no implementation for %s.%s%s\n", cls, name, sig); failures++; jvalue z; z.j = 0; return z; }
    tl_jcall c = { .self = NULL, .cls = NULL, .args = args, .ret = V(0) };
    fn(&c);
    return c.ret;
}

static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static int slurp(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(out, 1, n - 1, f);
    fclose(f);
    out[got] = 0;
    return (int)got;
}

int main(void)
{
    char root[] = "/tmp/husk-kotlin-test.XXXXXX";
    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
    test_set_install(tl_godot_kotlin_install);

    char save[1100], tmp[1200], dir[1100];
    snprintf(save, sizeof(save), "%s/files/com.pokefight/save0.dat", root);   /* user://<package>/save0.dat */
    snprintf(tmp, sizeof(tmp), "%s.tmp", save);
    snprintf(dir, sizeof(dir), "%s/files/com.pokefight", root);

    /* 1. The signatures the engine asks for are all there. */
    static const char *const want[][3] = {
        { DA, "dirOpen", "(I)I" }, { DA, "dirOpen", "(ILjava/lang/String;)I" },
        { DA, "dirNext", "(I)Ljava/lang/String;" }, { DA, "dirClose", "(I)V" }, { DA, "dirIsDir", "(I)Z" },
        { DA, "isCurrentHidden", "(I)Z" },
        { FA, "fileOpen", "(Ljava/lang/String;I)I" }, { FA, "fileRead", "(ILjava/nio/ByteBuffer;)I" },
        { FA, "fileWrite", "(ILjava/nio/ByteBuffer;)Z" }, { FA, "fileResize", "(IJ)V" },
        { FA, "fileWrite", "(ILjava/nio/ByteBuffer;)V" },
    };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        char what[200];
        snprintf(what, sizeof(what), "finds %s.%s%s", want[i][0], want[i][1], want[i][2]);
        check(test_find(want[i][0], want[i][1], want[i][2]) != NULL, what);
    }

    /* 2. A save into a folder that is not there yet. */
    jvalue id;
    {
        jvalue a[2] = { { .l = tl_jni_new_string(save) }, V(2) };
        id = call(FA, "fileOpen", "(Ljava/lang/String;I)I", a);
        check(id.i > 0, "fileOpen(WRITE) into a missing folder returns a file id");
        static const char data[] = "slot=1;coins=42";
        jobj *b = tl_jni_new_string("");
        tl_jni_set_field(b, "address", "J", (jvalue){ .l = (void *)data });
        tl_jni_set_field(b, "capacity", "J", V((int64_t)strlen(data)));
        jvalue wargs[2] = { id, { .l = b } };
        check(call(FA, "fileWrite", "(ILjava/nio/ByteBuffer;)Z", wargs).z == 1, "fileWrite answers true (Z)");
        jvalue cargs[1] = { id };
        call(FA, "fileClose", "(I)V", cargs);
        char back[64];
        check(slurp(save, back, sizeof(back)) == (int)strlen(data) && !strcmp(back, data), "the save is on disk, unchanged");
    }

    /* 3. Listing the folder by id alone (Kotlin's dirNext) finds it. */
    {
        jvalue a[2] = { V(1), { .l = tl_jni_new_string(dir) } };
        jvalue did = call(DA, "dirOpen", "(ILjava/lang/String;)I", a);
        check(did.i > 0, "dirOpen finds the folder");
        int seen = 0;
        for (int i = 0; i < 10 && did.i > 0; i++) {
            jvalue nargs[1] = { did };
            jvalue name = call(DA, "dirNext", "(I)Ljava/lang/String;", nargs);
            if (!name.l || !tl_jni_string(name.l)[0]) break;
            if (!strcmp(tl_jni_string(name.l), "save0.dat")) seen = 1;
        }
        check(seen, "dirNext lists save0.dat");
        jvalue cargs[1] = { did };
        call(DA, "dirClose", "(I)V", cargs);
    }

    /* 4. Rename over an existing file (a save written to a temporary, then moved into place). */
    {
        FILE *f = fopen(tmp, "wb"); fputs("slot=1;coins=99", f); fclose(f);
        jvalue a[3] = { V(1), { .l = tl_jni_new_string(tmp) }, { .l = tl_jni_new_string(save) } };
        jvalue ok = call(DA, "rename", "(ILjava/lang/String;Ljava/lang/String;)Z", a);
        char back[64];
        check(ok.z == 1 && slurp(save, back, sizeof(back)) > 0 && !strcmp(back, "slot=1;coins=99"),
              "rename replaces the existing save");
    }

    /* 5. dirExists makes the folder when it is asked with a full path. */
    {
        char deep[1200];
        snprintf(deep, sizeof(deep), "%s/files/com.pokefight/screens", root);
        unlink(deep); rmdir(deep);
        jvalue a[2] = { V(1), { .l = tl_jni_new_string(deep) } };
        call(DA, "dirExists", "(ILjava/lang/String;)Z", a);
        struct stat st;
        check(stat(deep, &st) == 0 && S_ISDIR(st.st_mode), "dirExists creates the folder it is asked about");
    }

    /* 6. fileResize truncates. */
    {
        jvalue a[2] = { { .l = tl_jni_new_string(save) }, V(3) };
        jvalue fid = call(FA, "fileOpen", "(Ljava/lang/String;I)I", a);
        jvalue r[2] = { fid, V(4) };
        call(FA, "fileResize", "(IJ)V", r);
        struct stat st;
        stat(save, &st);
        check(st.st_size == 4, "fileResize truncates");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
