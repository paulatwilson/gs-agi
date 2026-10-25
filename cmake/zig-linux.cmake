# Cross toolchain: Linux from any host using `zig cc`.
#   cmake -S . -B build-linux-x86_64  -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux.cmake -DGSAGI_LINUX_ARCH=x86_64
#   cmake -S . -B build-linux-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux.cmake -DGSAGI_LINUX_ARCH=aarch64
# Needs `zig` on PATH. See README "Cross-compiling for Linux".
set(CMAKE_SYSTEM_NAME Linux)

if(NOT GSAGI_LINUX_ARCH)
    set(GSAGI_LINUX_ARCH x86_64)
endif()
set(CMAKE_SYSTEM_PROCESSOR ${GSAGI_LINUX_ARCH})

# glibc 2.31 floor: runs on Debian 11 / Ubuntu 20.04 and newer.
set(_target ${GSAGI_LINUX_ARCH}-linux-gnu.2.31)

find_program(GSAGI_ZIG zig REQUIRED)

# CMake wants single-file compiler/archiver paths, so wrap zig.
set(_wrap ${CMAKE_BINARY_DIR}/zigwrap)
file(MAKE_DIRECTORY ${_wrap})
file(WRITE ${_wrap}/cc "#!/bin/sh\nexec \"${GSAGI_ZIG}\" cc -target ${_target} \"$@\"\n")
file(WRITE ${_wrap}/ar "#!/bin/sh\nexec \"${GSAGI_ZIG}\" ar \"$@\"\n")
file(WRITE ${_wrap}/ranlib "#!/bin/sh\nexec \"${GSAGI_ZIG}\" ranlib \"$@\"\n")
file(CHMOD ${_wrap}/cc ${_wrap}/ar ${_wrap}/ranlib
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

set(CMAKE_C_COMPILER ${_wrap}/cc)
set(CMAKE_AR ${_wrap}/ar)
set(CMAKE_RANLIB ${_wrap}/ranlib)

# Target headers and libs for SDL2's X11/ALSA backends (cmake/linux-sysroot.sh).
set(GSAGI_LINUX_SYSROOT ${CMAKE_CURRENT_LIST_DIR}/../build-linux-sysroot/${GSAGI_LINUX_ARCH}
    CACHE PATH "")
if(EXISTS ${GSAGI_LINUX_SYSROOT}/usr/include)
    set(CMAKE_C_FLAGS_INIT "-isystem ${GSAGI_LINUX_SYSROOT}/usr/include")
    set(CMAKE_FIND_ROOT_PATH ${GSAGI_LINUX_SYSROOT})
    set(CMAKE_LIBRARY_ARCHITECTURE ${GSAGI_LINUX_ARCH}-linux-gnu)
else()
    message(WARNING "No ${GSAGI_LINUX_SYSROOT}: run cmake/linux-sysroot.sh ${GSAGI_LINUX_ARCH}; SDL2 would build without X11/ALSA")
endif()

# Keep host (Homebrew, /usr/local) headers and libs out of the target build.
set(CMAKE_IGNORE_PREFIX_PATH /opt/homebrew /usr/local)
set(ENV{CPPFLAGS} "")
set(ENV{LDFLAGS} "")
set(ENV{CFLAGS} "")
set(ENV{PKG_CONFIG_PATH} "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
