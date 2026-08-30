# Cross-compilation toolchain for the Kindle 3 Keyboard.
#
# Target: ARMv6 (ARM1136JF-S), Linux 2.6.26, glibc 2.5.
# Toolchain: koxtoolchain `kindle` target -> arm-kindle-linux-gnueabi.
#
# The toolchain ships x86-64 Linux ELF binaries, so on Apple Silicon this runs
# inside a container under Rosetta. See BUILD.md.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED KINDLE_TOOLCHAIN_ROOT)
  set(KINDLE_TOOLCHAIN_ROOT "/opt/x-tools/arm-kindle-linux-gnueabi")
endif()

set(_tc "${KINDLE_TOOLCHAIN_ROOT}/bin/arm-kindle-linux-gnueabi")

set(CMAKE_C_COMPILER   "${_tc}-gcc")
set(CMAKE_CXX_COMPILER "${_tc}-g++")
set(CMAKE_AR           "${_tc}-ar"     CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_tc}-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP        "${_tc}-strip"  CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH "${KINDLE_TOOLCHAIN_ROOT}/arm-kindle-linux-gnueabi/sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ARM1136JF-S: ARMv6 with VFP, no NEON, no Thumb-2.
set(KINDLE_ARCH_FLAGS "-march=armv6j -mtune=arm1136jf-s -mfpu=vfp -mfloat-abi=softfp")

set(CMAKE_C_FLAGS_INIT   "${KINDLE_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${KINDLE_ARCH_FLAGS}")

# Statically link the C++ runtime so the binary does not depend on a newer
# libstdc++ than the 2010 rootfs carries. This keeps the maximum symbol
# requirement at GLIBC_2.4, verified against the device.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
