import pathlib, sys

def edit(path, old, new):
    t = path.read_text()
    if new in t: print(f"  {path.name}: already"); return
    if t.count(old) != 1: sys.exit(f"{path}: {t.count(old)} matches for:\n{old[:150]}")
    path.write_text(t.replace(old, new)); print(f"  {path.name}: patched")

root = pathlib.Path(sys.argv[1]).resolve()
tl = root / "src" / "translation-layer-next"

# 1. A.setter for the MoltenVK path, next to the UE4 one
edit(tl / "husk-tl-unity-app.c",
     'void husk_ue4_set_vulkan(const char *dylib) { snprintf(A.vulkan, sizeof(A.vulkan), "%s", dylib ? dylib : ""); }\n',
     'void husk_ue4_set_vulkan(const char *dylib) { snprintf(A.vulkan, sizeof(A.vulkan), "%s", dylib ? dylib : ""); }\n'
     '/* The MoltenVK the app carries, for Godot: without this call Vulkan is not available and every engine falls back to ANGLE. */\n'
     'void husk_godot_set_vulkan(const char *dylib) { snprintf(A.vulkan, sizeof(A.vulkan), "%s", dylib ? dylib : ""); }\n')

# 2. configure it in the Godot branch, before the engine starts
edit(tl / "husk-tl-unity-app.c",
     '        tl_log_line("godot: starting %s as %s, %dx%d", A.apk, A.package, A.width, A.height);\n'
     '        tl_audio_install();\n'
     '        ok = tl_godot_start(&cfg) && tl_godot_run();',
     '        /* Godot 4 games are exported for Vulkan: let them have it, over the MoltenVK the app carries. */\n'
     '        if (A.vulkan[0]) tl_vk_configure(A.vulkan, NULL, 0);\n'
     '        tl_log_line("godot: starting %s as %s, %dx%d (Vulkan: %s)", A.apk, A.package, A.width, A.height, A.vulkan[0] ? A.vulkan : "none, OpenGL ES through ANGLE");\n'
     '        tl_audio_install();\n'
     '        ok = tl_godot_start(&cfg) && tl_godot_run();')

# 3. Swift: hand Godot the MoltenVK the app carries (the SDL branch already does this for SDL games)
edit(root / "src" / "app" / "Husk" / "TLUnityView.swift",
     '        case .godot: started = husk_godot_launch(apk, dataDir, layerPtr, Int32(width), Int32(height), angle, ca)\n',
     '        case .godot:\n'
     '            // Godot 4 games are exported for Vulkan. Give them the MoltenVK this app carries, as the SDL branch does.\n'
     '            if let fw = Bundle.main.privateFrameworksPath { husk_godot_set_vulkan(fw + "/MoltenVK.framework/MoltenVK") }\n'
     '            started = husk_godot_launch(apk, dataDir, layerPtr, Int32(width), Int32(height), angle, ca)\n')

# 3a. the header that declares tl_vk_available
edit(tl / "husk-tl-godot.c",
     '#include "husk-tl-godot.h"\n',
     '#include "husk-tl-godot.h"\n#include "husk-tl-vulkan.h"\n')

# 3. stop forcing the compatibility renderer when Vulkan is available
edit(tl / "husk-tl-godot.c",
     '    if (U.major >= 4 && !getenv("TL_GODOT_VULKAN") && n + 4 <= 32) {',
     '    if (U.major >= 4 && !getenv("TL_GODOT_VULKAN") && !tl_vk_available() && n + 4 <= 32) {')
