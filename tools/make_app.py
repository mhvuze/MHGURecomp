#!/usr/bin/env python3
"""Make a self-contained macOS app of a build: python3 tools/make_app.py <build dir> <game dir> <app path>

The app holds the runtime and the game code library of <build dir>, the game files (<game dir>/exefs and /romfs as APFS
clones: no extra space on the same disk), the libraries they need from Homebrew (rewritten to load from the app, so a
later `brew upgrade` can't break it), MoltenVK with its Vulkan driver manifest, and the game's own icon when the
extraction left one (<game dir>/icon.jpg). It is signed ad hoc for this Mac.

Everything the game writes goes to ~/Library/Application Support/MHGURecomp (runtime/platform/darwin.c): settings,
caches, mods/, cheats/, log.txt and the saves.

The app contains game code and data made from your own dump: it is for you, never for sharing.
"""
import json
import os
import plistlib
import shutil
import subprocess
import sys

BREW_PREFIXES = ("/opt/homebrew/", "/usr/local/")
EXE = "mhgurecomp"
GAMELIB = "mhgugame.dylib"


def run(*cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def deps(path):
    """The libraries a Mach-O file loads, as written in it (otool -L, without its own id)."""
    lines = run("otool", "-L", path).splitlines()[1:]
    out = [l.strip().split(" (")[0] for l in lines if l.strip()]
    own_id = run("otool", "-D", path).splitlines()[1:]
    return [d for d in out if not own_id or d != own_id[0].strip()]


def resolve(dep, loader):
    """A dependency's file: absolute, or relative to the library that loads it (@loader_path / @rpath)."""
    if dep.startswith("@loader_path/"):
        return os.path.realpath(os.path.join(os.path.dirname(loader), dep[len("@loader_path/"):]))
    if dep.startswith("@rpath/"):
        for base in (os.path.dirname(loader), os.path.join(os.path.dirname(loader), "..", "lib"), "/opt/homebrew/lib"):
            p = os.path.join(base, dep[len("@rpath/"):])
            if os.path.exists(p):
                return os.path.realpath(p)
        return None
    return os.path.realpath(dep) if os.path.exists(dep) else dep


def bundle_libraries(roots, frameworks):
    """Copy every Homebrew library the roots need (recursively) into frameworks; point every reference at the copy.
    roots: {file in the app: the file it was copied from} (whose folder @loader_path / @rpath references go by)."""
    origin = dict(roots)
    copied = {}  # real path -> name in Frameworks
    todo = list(roots)
    while todo:
        f = todo.pop()
        for d in deps(f):
            real = resolve(d, origin[f])
            if not real or not real.startswith(BREW_PREFIXES):
                continue
            name = os.path.basename(real)
            if real not in copied:
                copied[real] = name
                dst = os.path.join(frameworks, name)
                shutil.copy2(real, dst)
                os.chmod(dst, 0o755)
                run("install_name_tool", "-id", "@rpath/" + name, dst)
                origin[dst] = real
                todo.append(dst)
            run("install_name_tool", "-change", d, "@rpath/" + copied[real], f)
    return copied


def add_rpath(path, rpath):
    if rpath not in run("otool", "-l", path):
        run("install_name_tool", "-add_rpath", rpath, path)


def make_icon(jpg, resources):
    """The game's icon (a JPEG from its control data) as AppIcon.icns."""
    iconset = os.path.join(resources, "AppIcon.iconset")
    os.makedirs(iconset, exist_ok=True)
    for size in (16, 32, 64, 128, 256, 512):
        for scale in (1, 2):
            px = size * scale
            name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
            run("sips", "-s", "format", "png", "-z", str(px), str(px), jpg, "--out", os.path.join(iconset, name))
    run("iconutil", "-c", "icns", iconset, "-o", os.path.join(resources, "AppIcon.icns"))
    shutil.rmtree(iconset)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    build, game, app = (os.path.abspath(a) for a in sys.argv[1:])
    for f in (os.path.join(build, EXE), os.path.join(build, GAMELIB)):
        if not os.path.isfile(f):
            sys.exit(f"make_app: {f} is missing: build first (make GAME=...)")
    if not os.path.isfile(os.path.join(game, "exefs", "main")) or not os.path.isdir(os.path.join(game, "romfs")):
        sys.exit(f"make_app: no exefs/ and romfs/ in {game}")
    if os.path.exists(app):
        shutil.rmtree(app)
    contents = os.path.join(app, "Contents")
    macos, frameworks, resources = (os.path.join(contents, d) for d in ("MacOS", "Frameworks", "Resources"))
    for d in (macos, frameworks, resources):
        os.makedirs(d)

    # code
    exe, gamelib = os.path.join(macos, EXE), os.path.join(macos, GAMELIB)
    shutil.copy2(os.path.join(build, EXE), exe)
    shutil.copy2(os.path.join(build, GAMELIB), gamelib)
    moltenvk_src = os.path.realpath("/opt/homebrew/opt/molten-vk/lib/libMoltenVK.dylib")
    moltenvk = os.path.join(frameworks, "libMoltenVK.dylib")
    shutil.copy2(moltenvk_src, moltenvk)
    os.chmod(moltenvk, 0o755)
    run("install_name_tool", "-id", "@rpath/libMoltenVK.dylib", moltenvk)
    libs = bundle_libraries({exe: os.path.join(build, EXE), gamelib: os.path.join(build, GAMELIB), moltenvk: moltenvk_src},
                            frameworks)
    add_rpath(exe, "@executable_path/../Frameworks")
    for name in os.listdir(frameworks):
        add_rpath(os.path.join(frameworks, name), "@loader_path")
    print(f"make_app: {len(libs) + 1} libraries in Frameworks")

    # Vulkan driver manifest (the runtime points the loader at it: runtime/nvn/vulkan/device.c)
    icd_dir = os.path.join(resources, "vulkan", "icd.d")
    os.makedirs(icd_dir)
    with open("/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json") as f:
        icd = json.load(f)
    icd["ICD"]["library_path"] = "../../../Frameworks/libMoltenVK.dylib"
    with open(os.path.join(icd_dir, "MoltenVK_icd.json"), "w") as f:
        json.dump(icd, f, indent=2)

    # game files: APFS clones (cp -c), copies on other file systems
    data = os.path.join(resources, "data")
    os.makedirs(data)
    for d in ("exefs", "romfs"):
        subprocess.run(["cp", "-cR", os.path.join(game, d), os.path.join(data, d)], check=True)
    os.symlink("../Resources/data", os.path.join(macos, "data"))

    # icon and Info.plist
    icon = os.path.join(game, "icon.jpg")
    if os.path.isfile(icon):
        make_icon(icon, resources)
    plist = {
        "CFBundleIdentifier": "local.mhgurecomp.game",
        "CFBundleName": "MHGU Recomp",
        "CFBundleDisplayName": "MHGU Recomp",
        "CFBundleExecutable": EXE,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "1.4.0",
        "CFBundleVersion": run("git", "-C", os.path.dirname(os.path.abspath(__file__)), "rev-parse", "--short",
                               "HEAD").strip() or "1",
        "LSMinimumSystemVersion": "13.0",
        "LSApplicationCategoryType": "public.app-category.games",  # (Game Mode in full screen)
        "NSHighResolutionCapable": True,
        "GCSupportsControllerUserInteraction": True,
    }
    if os.path.isfile(os.path.join(resources, "AppIcon.icns")):
        plist["CFBundleIconFile"] = "AppIcon"
    with open(os.path.join(contents, "Info.plist"), "wb") as f:
        plistlib.dump(plist, f)

    # ad-hoc signature: the libraries first (install_name_tool invalidated theirs), then the app
    for name in sorted(os.listdir(frameworks)):
        run("codesign", "--force", "-s", "-", os.path.join(frameworks, name))
    run("codesign", "--force", "-s", "-", gamelib)
    run("codesign", "--force", "-s", "-", app)
    print(f"make_app: {app}")


if __name__ == "__main__":
    main()
