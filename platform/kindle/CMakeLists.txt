# CrossInk for the Kindle 3 Keyboard.
#
# PlatformIO's `native` platform cannot cross-compile, so the simulator's
# platformio.ini [simulator-base] environment is reproduced here. The source
# set, defines and include paths below mirror it deliberately - when upstream
# changes that environment, this is the file that has to follow.
#
#   cmake -S platform/kindle -B build \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/kindle-toolchain.cmake
#   cmake --build build -j1
#
# See BUILD.md; this must run in a Linux container (the toolchain is x86-64 ELF).
#
# It lives here rather than at the repo root ON PURPOSE. PlatformIO's ESP32
# build does "Reading CMake configuration..." against the project root, so a
# root CMakeLists.txt is picked up by `pio run` and `pio check` and breaks
# both. Upstream has no root CMakeLists.txt; neither should this fork.

cmake_minimum_required(VERSION 3.16)
project(crossink-kindle LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "" FORCE)
endif()

# This repo IS CrossInk (a fork), so CROSSINK is the source root itself.
# freeink-sdk is CrossInk's own submodule; only the simulator is added by us.
get_filename_component(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(VENDOR    ${REPO}/vendor)
set(CROSSINK  ${REPO})
set(SDK       ${REPO}/freeink-sdk)
set(SIM       ${VENDOR}/simulator)
set(LIBDEPS   ${CROSSINK}/.pio/libdeps/simulator)
set(PLATFORM  ${CMAKE_CURRENT_SOURCE_DIR})

foreach(d ${CROSSINK} ${SDK} ${SIM} ${LIBDEPS})
  if(NOT EXISTS ${d})
    message(FATAL_ERROR "missing ${d}\n"
      "  freeink-sdk, vendor/simulator : git submodule update --init --recursive\n"
      "  .pio/libdeps/simulator        : pio pkg install -e simulator")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# Code generation
#
# PlatformIO runs these as pre: scripts. They emit headers into the CrossInk
# tree, so they must run before anything that includes them compiles.
# ---------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  foreach(gen gen_i18n build_web git_branch)
    if(EXISTS ${CROSSINK}/scripts/${gen}.py)
      execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CROSSINK}/scripts/${gen}.py
        WORKING_DIRECTORY ${CROSSINK}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
      if(NOT _rc EQUAL 0)
        # These scripts expect PlatformIO's SConscript environment. Failing is
        # expected and tolerable as long as their generated headers already
        # exist in the tree from a prior simulator build.
        message(STATUS "codegen ${gen}.py returned ${_rc} (using checked-in output)")
      endif()
    endif()
  endforeach()
else()
  message(STATUS "python3 not found - relying on checked-in generated headers")
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

file(GLOB_RECURSE CROSSINK_SRC CONFIGURE_DEPENDS
  ${CROSSINK}/src/*.cpp ${CROSSINK}/src/*.c)

# build_src_filter from [simulator-base]: firmware-update paths stay out of a
# hosted build, and the efuse shim is ESP32-only.
list(FILTER CROSSINK_SRC EXCLUDE REGEX "/network/FirmwareFlasher\\.cpp$")
list(FILTER CROSSINK_SRC EXCLUDE REGEX "/network/OtaBootSwitch\\.cpp$")
list(FILTER CROSSINK_SRC EXCLUDE REGEX "/network/OtaUpdater\\.cpp$")
list(FILTER CROSSINK_SRC EXCLUDE REGEX "/platform/skip_efuse_blk_check\\.c$")

# Replaced by platform/kindle: the original includes <SDL.h> unconditionally,
# though its only SDL use is behind SIMULATOR_DEVICE_X4PRO. See the copy there.
list(FILTER CROSSINK_SRC EXCLUDE REGEX "/simulator/SimulatorHomeKeyInput\\.cpp$")

# Simulator shims: Arduino, ESP-IDF, FreeRTOS, and the seven Hal* classes we
# keep. The three SDL-bound files are replaced by platform/kindle.
file(GLOB SIM_SRC CONFIGURE_DEPENDS ${SIM}/src/*.cpp)
list(FILTER SIM_SRC EXCLUDE REGEX "/HalDisplay\\.cpp$")
list(FILTER SIM_SRC EXCLUDE REGEX "/HalGPIO\\.cpp$")
list(FILTER SIM_SRC EXCLUDE REGEX "/simulator_main\\.cpp$")
# Wholly redundant at our pinned SHA: MySerialImpl comes from
# lib/Logging/Logging.cpp under SIMULATOR, and uzlib_adler32/uzlib_crc32 from
# lib/uzlib/src/uzlib_checksums.c. Both of this file's comments claiming those
# are missing upstream are stale.
list(FILTER SIM_SRC EXCLUDE REGEX "/firmware_link_stubs\\.cpp$")

# CrossInk's own libraries under lib/. PlatformIO's dependency finder adds
# these automatically; here they have to be enumerated.
#
# lib_ignore from [simulator-base]: `hal` is the Arduino/ESP32 HAL whose
# same-named classes we replace at link time - compiling it would collide with
# platform/kindle. HalClockSim is likewise superseded.
set(LIB_IGNORE hal HalClockSim)

set(CROSSINK_LIB_SRC "")
set(CROSSINK_LIB_INC "")
file(GLOB _libdirs RELATIVE ${CROSSINK}/lib ${CROSSINK}/lib/*)
foreach(_lib ${_libdirs})
  if(NOT IS_DIRECTORY ${CROSSINK}/lib/${_lib})
    continue()
  endif()
  if(_lib IN_LIST LIB_IGNORE)
    continue()
  endif()
  # Flat layout for most; a few use include/ + src/.
  list(APPEND CROSSINK_LIB_INC ${CROSSINK}/lib/${_lib})
  foreach(_sub include src)
    if(IS_DIRECTORY ${CROSSINK}/lib/${_lib}/${_sub})
      list(APPEND CROSSINK_LIB_INC ${CROSSINK}/lib/${_lib}/${_sub})
    endif()
  endforeach()
  file(GLOB_RECURSE _s CONFIGURE_DEPENDS
    ${CROSSINK}/lib/${_lib}/*.cpp ${CROSSINK}/lib/${_lib}/*.c)
  # third_party/ holds amalgamated sources that a src/ wrapper #includes
  # directly (miniz_impl.c does `#include "../third_party/miniz.c"`).
  # Compiling them separately duplicates every symbol.
  list(FILTER _s EXCLUDE REGEX "/third_party/")
  list(APPEND CROSSINK_LIB_SRC ${_s})
endforeach()

file(GLOB_RECURSE SDK_SRC CONFIGURE_DEPENDS
  ${SDK}/libs/ui/FreeInkUI/src/*.cpp
  ${SDK}/libs/assets/Icons/src/*.cpp
  ${SDK}/libs/network/NearbyTransfer/src/*.cpp)

# PNGdec vendors its own zlib (inflate.c, crc32.c, adler32.c, inftrees.c,
# zutil.c, ...), so the .c files are required - globbing only *.cpp leaves
# inflate/crc32 undefined at link time. Note the .S file alongside them is
# ESP32-S3 SIMD assembly and must stay out of an ARMv6 build.
file(GLOB_RECURSE THIRDPARTY_SRC CONFIGURE_DEPENDS
  ${LIBDEPS}/QRCode/src/*.c
  ${LIBDEPS}/PNGdec/src/*.cpp ${LIBDEPS}/PNGdec/src/*.c
  ${LIBDEPS}/JPEGDEC/src/*.cpp ${LIBDEPS}/JPEGDEC/src/*.c)

# Non-recursive on purpose: platform/kindle/tools/ holds standalone probes
# with their own main(), which must not be linked into the reader.
file(GLOB PLATFORM_SRC CONFIGURE_DEPENDS ${PLATFORM}/*.cpp ${PLATFORM}/*.c)

add_executable(crossink
  ${CROSSINK_SRC}
  ${CROSSINK_LIB_SRC}
  ${SIM_SRC}
  ${SDK_SRC}
  ${THIRDPARTY_SRC}
  ${PLATFORM_SRC})

# ---------------------------------------------------------------------------
# Includes
#
# platform/kindle MUST come first: its EInkDisplay.h shadows the simulator's,
# which is what sets the panel geometry to 600x800 portrait.
# ---------------------------------------------------------------------------

target_include_directories(crossink SYSTEM PRIVATE
  ${LIBDEPS}/ArduinoJson/src
  ${LIBDEPS}/QRCode/src
  ${LIBDEPS}/PNGdec/src
  ${LIBDEPS}/JPEGDEC/src)

target_include_directories(crossink PRIVATE
  ${PLATFORM}
  ${CROSSINK_LIB_INC}
  ${SIM}/src
  ${CROSSINK}/src
  ${CROSSINK}/include
  ${SDK}/libs/ui/FreeInkUI/include
  ${SDK}/libs/ui/FreeInkUI/src
  ${SDK}/libs/assets/Icons/include
  ${SDK}/libs/display/FreeInkDisplay/include
  ${SDK}/libs/network/NearbyTransfer/include)

# ---------------------------------------------------------------------------
# Defines - [simulator-base] build_flags, minus SDL, plus the Kindle target
# ---------------------------------------------------------------------------

target_compile_definitions(crossink PRIVATE
  SIMULATOR                       # selects the POSIX shims throughout the tree
  # Disables the simulator's own web server (it is #ifndef-guarded on this),
  # leaving CrossInk's src/network/CrossPointWebServer.cpp as the only copy.
  CROSSPOINT_SIMULATOR_PROJECT_WEBSERVER
  CROSSINK_KINDLE=1               # ours: guards Kindle-only branches
  # The K3 has a real 5-way d-pad. Upstream's UI assumes a device without one
  # and aliases Left/Right onto Up/Down; this turns that off. See
  # src/util/ButtonNavigator.h.
  CROSSINK_HAS_DPAD=1
  CROSSINK_APP_CAP_TOUCH=0
  CROSSINK_FIRMWARE_DEVICE_TYPE="kindle3"
  CROSSINK_VERSION="1.5.0-kindle"
  ENABLE_SERIAL_LOG
  LOG_LEVEL=2
  EINK_DISPLAY_SINGLE_BUFFER_MODE=1
  MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  USE_UTF8_LONG_NAMES=1
  PNG_MAX_BUFFERED_PIXELS=16416
  DISABLE_FS_H_WARNING=1
  DESTRUCTOR_CLOSES_FILE=1
  CROSSPOINT_SIM_USE_NATIVE_DECODERS
  memcpy_P=memcpy)

# Force-include the libc compat shim into every TU, C and C++ alike, so no
# vendored source has to be patched for the 2010 glibc.
target_compile_options(crossink PRIVATE
  -include ${PLATFORM}/kindle_compat.h
  -Wno-c++11-narrowing
  -Wno-unused-parameter
  -Wno-psabi
  -ffunction-sections
  -fdata-sections)

target_link_options(crossink PRIVATE -Wl,--gc-sections)

find_package(Threads REQUIRED)
target_link_libraries(crossink PRIVATE Threads::Threads rt m)

# ---------------------------------------------------------------------------
# Post-build audit: the two things that silently break on a 2010 device.
# ---------------------------------------------------------------------------

add_custom_command(TARGET crossink POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "--- GLIBC symbol versions required ---"
  COMMAND sh -c "${CMAKE_OBJDUMP} -T $<TARGET_FILE:crossink> | grep -o 'GLIBC_[0-9.]*' | sort -u || true"
  COMMAND ${CMAKE_COMMAND} -E echo "--- syscalls that ENOSYS on kernel 2.6.26 ---"
  COMMAND sh -c "${CMAKE_NM} -uC $<TARGET_FILE:crossink> | grep -E 'accept4|pipe2|epoll_create1|eventfd2|dup3|inotify_init1' || echo '  none'"
  VERBATIM)
