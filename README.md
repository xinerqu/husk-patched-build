# husk-patched-build

Test builds of [Husk](https://github.com/Leviidev/Husk) with a fix for issue **#140** (Godot 4.7 games: saving does not work, some
text drawn as Unicode code-point boxes), built on GitHub Actions. Husk's own source is not copied here: the workflow clones
`Leviidev/Husk`, applies `ci/scripts/patch_140.py`, and builds.

**Download**: the newest **[Release](../../releases/latest)**.

- `Husk-<sha>.zip` — unsigned IPA. SideStore / AltStore / Sideloadly re-sign it with your own certificate.
- `Husk-<sha>.tipa` — TrollStore (ad hoc signed, Husk's entitlements kept).
- `Husk-latest.*` — the same files under a name that does not change.

## What the patch changes

| file | change |
|---|---|
| `src/translation-layer-next/husk-tl-jni-godot-kotlin.c` (new) | Godot 4.6/4.7 moved `DirectoryAccessHandler` / `FileAccessHandler` to Kotlin and changed four signatures while `GodotLib` still asks for the old Java ones (`dirNext(II)` → `dirNext(I)`, `fileWrite …)V` → `…Z`, `fileResize(IJ)I` → `(IJ)V`). Every listing call threw `NoSuchMethodError`, so the engine believed no folder existed: no `user://shader_cache`, and the folder a game saved into could not be found or made. This file answers the Kotlin forms (and keeps the Java ones) from the same file ids, creates a folder `dirExists` is asked about (as the phone's `FilesystemDirectoryAccess.makeDir` does), and renames over an existing file (iOS's `rename` will not). |
| `husk-tl-jni-godot.c` | installs the above; answers `getScreenRefreshRate`, `getDisplaySafeArea`, `getLocale`, `getDataDir` on `android.content.res.AssetManager` as well (Godot 4.7 calls them there, and logged `GetArrayLength(returnArray) != 4` every frame). |
| `husk-tl-unity-app.c` | `ANDROID_ROOT` points at the app's `android-system` folder, which is where Godot 4.7 reads `etc/fonts.xml` and its fonts from (`OS_Android::_load_system_font_config`). Without it the engine opened `/etc/fonts.xml`, failed, and fell back to one font. |

The patch is applied by `ci/scripts/patch_140.py`; `ci/patches/140/` holds the new C file. `ci/test/` compiles that file against the
real `husk-tl-jni.h` with small stubs and checks the engine's calls on a real directory (write into a missing folder, list it, rename
over it); the workflow runs it before the build.

## Building / re-running

```sh
gh workflow run test-patched-build --repo xinerqu/husk-patched-build
```

`Leviidev/Husk` is cloned at `main` (input `husk_ref` to pick another branch), QEMU, ANGLE and the rest are compiled once and kept in
the Actions cache, and a change to the patch alone reuses that. When `macos` runner disk spaces are exceeded the first run fails;
re-running resumes from the same cache.

## Removing

Everything here is test scaffolding: delete the repository to undo it. Nothing in `Leviidev/Husk` is touched.
