# PlatformIO (extra_scripts) : après chaque édition de liens, archive firmware.elf sous
# build-archive/<sha256[:16]>.elf — l'empreinte inscrite dans les coredumps — pour pouvoir
# décoder un plantage même après d'autres compilations.
import hashlib
import os
import shutil

Import("env")  # noqa: F821  (fourni par PlatformIO)


def archive(source, target, env):
    elf = str(target[0])
    sha = hashlib.sha256(open(elf, "rb").read()).hexdigest()[:16]
    dest_dir = os.path.join(env.subst("$PROJECT_DIR"), "build-archive")
    os.makedirs(dest_dir, exist_ok=True)
    shutil.copy2(elf, os.path.join(dest_dir, f"{sha}.elf"))
    print(f"ELF archivé : build-archive/{sha}.elf")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", archive)  # noqa: F821
