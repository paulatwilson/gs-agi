cmake_minimum_required(VERSION 3.16)
project(gsagi C)

# Host (SDL2) build of the engine: macOS first, then Linux and Windows.
# The Apple IIgs build is done by hand in ORCA Shell; see README.md.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_CROSSCOMPILING)
    # No target SDL2 to find: build it from source, static. The SDL
    # video/audio libs (X11, ALSA, ...) are dlopen()ed at run time, so
    # only their headers are needed at build time; see cmake/linux-sysroot.sh.
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz)
    FetchContent_MakeAvailable(SDL2)
else()
    find_package(SDL2 REQUIRED CONFIG)
endif()

add_executable(gsagi
    src/main.c
    src/util.c
    src/vocab.c
    src/object.c
    src/logic.c
    src/keyboard.c
    src/sound.c
    src/gfx.c
    src/pic.c
    src/view.c
    src/motion.c
    src/sdl/video_sdl.c
    src/sdl/keyboard_sdl.c
    src/sdl/sound_sdl.c
)

if(TARGET SDL2::SDL2-static)
    target_link_libraries(gsagi PRIVATE SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    target_link_libraries(gsagi PRIVATE SDL2::SDL2)
else()
    target_include_directories(gsagi PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(gsagi PRIVATE ${SDL2_LIBRARIES})
endif()

if(NOT WIN32)
    target_link_libraries(gsagi PRIVATE m)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gsagi PRIVATE -Wall -Wno-unused-parameter)
endif()

# Host-side authoring tools (and the demo): not built when cross-compiling,
# since they must run on the build machine. Build game/ with a host build.
if(CMAKE_CROSSCOMPILING)
    return()
endif()

# Host-side authoring tools. They share the engine's decoders and
# src/resfmt.h so encoder and decoder cannot drift apart.
add_executable(picc tools/picc.c)
add_executable(viewc tools/viewc.c)
add_executable(agic tools/agic.c)
add_executable(sndc tools/sndc.c)
add_executable(picview tools/picview.c src/pic.c src/util.c)

# ---- the demo game ----
# `cmake --build build` also compiles the demo's sources in game-src/demo
# into game/, which `./build/gsagi game/` plays.
set(DEMO_SRC ${CMAKE_SOURCE_DIR}/game-src/demo)
set(GAME_DIR ${CMAKE_SOURCE_DIR}/game)
file(MAKE_DIRECTORY ${GAME_DIR})
set(DEMO_OUTPUTS)

foreach(n 1 2 3)
    add_custom_command(OUTPUT ${GAME_DIR}/PIC.${n}
        COMMAND picc ${DEMO_SRC}/pic/pic${n}.pic ${GAME_DIR}/PIC.${n}
        DEPENDS picc ${DEMO_SRC}/pic/pic${n}.pic)
    list(APPEND DEMO_OUTPUTS ${GAME_DIR}/PIC.${n})
    add_custom_command(OUTPUT ${GAME_DIR}/SOUND.${n}
        COMMAND sndc ${DEMO_SRC}/snd/sound${n}.snd ${GAME_DIR}/SOUND.${n}
        DEPENDS sndc ${DEMO_SRC}/snd/sound${n}.snd)
    list(APPEND DEMO_OUTPUTS ${GAME_DIR}/SOUND.${n})
endforeach()

foreach(n 0 1 2 3 4 5 6 7)
    add_custom_command(OUTPUT ${GAME_DIR}/VIEW.${n}
        COMMAND viewc ${DEMO_SRC}/view/view${n}.view ${GAME_DIR}/VIEW.${n}
        DEPENDS viewc ${DEMO_SRC}/view/view${n}.view)
    list(APPEND DEMO_OUTPUTS ${GAME_DIR}/VIEW.${n})
endforeach()

add_custom_command(
    OUTPUT ${GAME_DIR}/LOGIC.0 ${GAME_DIR}/LOGIC.1 ${GAME_DIR}/LOGIC.2
           ${GAME_DIR}/WORDS.TOK ${GAME_DIR}/OBJECT
    COMMAND agic -w ${DEMO_SRC}/words.txt -o ${DEMO_SRC}/objects.txt -d ${GAME_DIR}
            ${DEMO_SRC}/logic0.agi ${DEMO_SRC}/logic1.agi ${DEMO_SRC}/logic2.agi
    DEPENDS agic ${DEMO_SRC}/words.txt ${DEMO_SRC}/objects.txt ${DEMO_SRC}/defs.agi
            ${DEMO_SRC}/logic0.agi ${DEMO_SRC}/logic1.agi ${DEMO_SRC}/logic2.agi)
list(APPEND DEMO_OUTPUTS ${GAME_DIR}/LOGIC.0 ${GAME_DIR}/WORDS.TOK ${GAME_DIR}/OBJECT)

add_custom_target(demo ALL DEPENDS ${DEMO_OUTPUTS})
