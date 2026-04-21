# freertos_flags.py — PlatformIO extra_scripts (pre-build)
#
# The ststm32 platform constructs LINKFLAGS from board spec fields and does not
# forward user build_flags to the link step.  This script adds the FPU flags
# directly to both CCFLAGS and LINKFLAGS so all objects AND the final ELF agree
# on float ABI.  Without this, the linker rejects HAL objects (compiled by the
# platform with -mfloat-abi=hard) against a non-VFP firmware.elf.

Import("env")

fpu_flags = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]

# newlib-nano strips float printf/scanf by default.  The build_flags entry
# "-Wl,-u,_printf_float" is not reliably forwarded by ststm32 to the link
# step (same root cause as the FPU flags).  Force it here directly.
printf_float_flag = "-Wl,-u,_printf_float"

env.Append(
    CCFLAGS=fpu_flags,
    CXXFLAGS=fpu_flags,
    LINKFLAGS=fpu_flags + [printf_float_flag],
)
