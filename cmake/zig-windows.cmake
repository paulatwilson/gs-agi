# Cross toolchain: Windows (MinGW-w64 ABI) from any host using `zig cc`.
#   cmake -S . -B build-windows-x86_64 -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake -DGSAGI_WINDOWS_ARCH=x86_64
# GSAGI_WINDOWS_ARCH: x86_64 (default), x86 or aarch64.
# Needs `zig` on PATH. See README "Cross-compiling for Windows".
set(CMAKE_SYSTEM_NAME Windows)

if(NOT GSAGI_WINDOWS_ARCH)
    set(GSAGI_WINDOWS_ARCH x86_64)
endif()
set(CMAKE_SYSTEM_PROCESSOR ${GSAGI_WINDOWS_ARCH})

set(_target ${GSAGI_WINDOWS_ARCH}-windows-gnu)

find_program(GSAGI_ZIG zig REQUIRED)

# CMake wants single-file tool paths, so wrap zig.
set(_wrap ${CMAKE_BINARY_DIR}/zigwrap)
file(MAKE_DIRECTORY ${_wrap})
file(WRITE ${_wrap}/cc "#!/bin/sh\nexec \"${GSAGI_ZIG}\" cc -target ${_target} \"$@\"\n")
file(WRITE ${_wrap}/ar "#!/bin/sh\nexec \"${GSAGI_ZIG}\" ar \"$@\"\n")
file(WRITE ${_wrap}/ranlib "#!/bin/sh\nexec \"${GSAGI_ZIG}\" ranlib \"$@\"\n")
file(WRITE ${_wrap}/rc "#!/bin/sh\nexec \"${GSAGI_ZIG}\" rc \"$@\"\n")
file(CHMOD ${_wrap}/cc ${_wrap}/ar ${_wrap}/ranlib ${_wrap}/rc
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

set(CMAKE_C_COMPILER ${_wrap}/cc)
set(CMAKE_AR ${_wrap}/ar)
set(CMAKE_RANLIB ${_wrap}/ranlib)
set(CMAKE_RC_COMPILER ${_wrap}/rc)

# Keep host (Homebrew, /usr/local) headers and libs out of the target build.
set(CMAKE_IGNORE_PREFIX_PATH /opt/homebrew /usr/local)
set(ENV{CPPFLAGS} "")
set(ENV{LDFLAGS} "")
set(ENV{CFLAGS} "")
set(ENV{PKG_CONFIG_PATH} "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
