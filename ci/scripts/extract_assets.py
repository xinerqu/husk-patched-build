#!/usr/bin/env python3
"""Take build/java-runtime and build/android-system out of an official Husk IPA.

    extract_assets.py Husk.ipa <husk checkout>/ci/assets

Both folders are made by scripts that need a mounted Lineage system image (build_java_runtime.sh, build_android_system.sh), so a CI
runner takes them from the last official release instead. Everything else is compiled from source.
"""
import os
import shutil
import sys
import tempfile
import zipfile

PARTS = ("java-runtime", "android-system")


def main() -> int:
    ipa, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(ipa) as z:
        wanted = [i for i in z.infolist()
                  if not i.is_dir() and any(i.filename.startswith(f"Payload/Husk.app/{p}/") for p in PARTS)]
        z.extractall(tmp, members=wanted)
        for part in PARTS:
            src = os.path.join(tmp, "Payload", "Husk.app", part)
            if not os.path.isdir(src):
                print(f"warning: {part} is not in {ipa}", file=sys.stderr)
                continue
            dst = os.path.join(out, part)
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(src, dst)
            files = sum(len(f) for _, _, f in os.walk(dst))
            print(f"{part}: {files} files -> {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
