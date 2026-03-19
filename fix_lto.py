# fix_lto.py — PlatformIO extra_scripts POST-build hook
#
# The Jason2866 IDF53 platform's pioarduino-build.py adds -flto=auto / -flto
# to CCFLAGS, CXXFLAGS, and LINKFLAGS.  On Windows the toolchain ships Xtensa
# target-config plugins as Linux .so files (ELF), which Windows cannot load as
# DLLs.  lto1.exe aborts with "Unable to load DLL" when it tries to use the
# plugin during link-time optimisation.
#
# This post-build script runs AFTER pioarduino-build.py has set up the flags,
# and strips -flto* from every flag list so the linker never invokes lto-wrapper.
# The pre-compiled framework .a libraries contain fat LTO objects (regular code
# + GIMPLE IR), so linking without -flto falls back to the regular object code.

Import("env")
import os

for flag_list in ("CCFLAGS", "CFLAGS", "CXXFLAGS", "LINKFLAGS", "ASFLAGS"):
    flags = env.get(flag_list, [])
    cleaned = [f for f in flags if not str(f).startswith("-flto")]
    if len(cleaned) != len(flags):
        env[flag_list] = cleaned
        print(f"fix_lto.py: removed -flto from {flag_list}")

# Belt-and-suspenders: clear the XTENSA_GNU_CONFIG env var too
if "XTENSA_GNU_CONFIG" in os.environ:
    del os.environ["XTENSA_GNU_CONFIG"]
    print("fix_lto.py: cleared XTENSA_GNU_CONFIG")
else:
    print("fix_lto.py: LTO flags stripped (XTENSA_GNU_CONFIG was not in environment)")
