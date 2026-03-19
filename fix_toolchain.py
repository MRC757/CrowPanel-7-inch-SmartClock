# fix_toolchain.py — PlatformIO extra_scripts post-build hook
#
# The pioarduino/platform-espressif32 uses toolchain-xtensa-esp-elf (GCC 14.2)
# which ships xtensa-esp32s3-elf-* binaries in its bin/ directory.
# The platform builder sets CC to just "xtensa-esp32s3-elf-gcc" (no full path),
# so the bin/ must be in PATH for subprocess invocations to resolve the name.
#
# This script prepends the toolchain bin dir to both os.environ["PATH"]
# (inherited by all spawned processes) and env["ENV"]["PATH"] (SCons subprocess env).

Import("env")
import os

toolchain_dir = env.PioPlatform().get_package_dir("toolchain-xtensa-esp-elf")
if toolchain_dir:
    bin_dir = os.path.join(toolchain_dir, "bin")
    os.environ["PATH"] = bin_dir + os.pathsep + os.environ.get("PATH", "")
    env.PrependENVPath("PATH", bin_dir)
    print(f"fix_toolchain.py: added to PATH -> {bin_dir}")
else:
    print("fix_toolchain.py: toolchain-xtensa-esp-elf not found")
