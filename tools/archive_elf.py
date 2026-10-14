# PlatformIO (extra_scripts): after every link, archives firmware.elf as
# build-archive/<sha256[:16]>.elf — the hash written into coredumps — so a crash can still
# be decoded after later builds.
import hashlib
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO)


def archive(source, target, env):
    elf = str(target[0])
    sha = hashlib.sha256(open(elf, "rb").read()).hexdigest()[:16]
    dest_dir = os.path.join(env.subst("$PROJECT_DIR"), "build-archive")
    os.makedirs(dest_dir, exist_ok=True)
    shutil.copy2(elf, os.path.join(dest_dir, f"{sha}.elf"))
    print(f"ELF archived: build-archive/{sha}.elf")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", archive)  # noqa: F821
