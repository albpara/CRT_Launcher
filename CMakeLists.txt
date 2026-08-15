cmake_minimum_required(VERSION 3.16)
project(crt_launcher C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (WIN32)
    enable_language(RC)
endif()

# OFF (default): a normal console-subsystem exe -- the terminal that opens
# alongside it is where SDL_Log's output goes, handy while developing.
# Dynamically linked against SDL2 (SDL2.dll copied next to the exe) since
# that keeps rebuilds, which happen constantly in this mode, fast.
# ON: the distribution/deployment build -- a WINDOWS-subsystem exe (no
# console ever, not even hidden, so no startup flash and SDL_Log has
# nowhere to write to) that also statically links SDL2 into the exe
# itself, so there's no SDL2.dll to keep track of: just crt_launcher.exe
# plus config.ini. Build a separate directory with this ON (e.g.
# `cmake -B build-noconsole -DCRT_LAUNCHER_NO_CONSOLE=ON`) once you're
# done watching the log and ready to copy the result to the cabinet; keep
# the default OFF build for day-to-day development.
option(CRT_LAUNCHER_NO_CONSOLE "Build as a WINDOWS-subsystem exe with no console window at all" OFF)

add_executable(crt_launcher
    src/main.c
    src/config.c
    src/display.c
    src/render.c
    src/launchbox.c
    src/gamelist.c
    src/xml_util.c
    src/launcher.c
    src/startup.c
    src/starfield.c
)

# Shown in the header and written into a generated config.ini, so the
# cabinet can say which build it's running. The release workflow passes the
# tag; a local build falls back to git describe, then "dev". Resolved at
# configure time, so it only refreshes when CMake re-runs.
set(CRT_LAUNCHER_VERSION "" CACHE STRING "Version string baked into the build")
if (NOT CRT_LAUNCHER_VERSION)
    find_package(Git QUIET)
    if (GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE CRT_LAUNCHER_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
endif()
if (NOT CRT_LAUNCHER_VERSION)
    set(CRT_LAUNCHER_VERSION "dev")
endif()
message(STATUS "CRT Launcher version: ${CRT_LAUNCHER_VERSION}")
target_compile_definitions(crt_launcher PRIVATE
    CRT_LAUNCHER_VERSION="${CRT_LAUNCHER_VERSION}")

# No -Werror here on purpose: a warning shouldn't stop the cabinet build.
# CI injects it instead.
if (MSVC)
    target_compile_options(crt_launcher PRIVATE /W4)
else()
    target_compile_options(crt_launcher PRIVATE
        -Wall -Wextra -Wformat=2 -Wshadow -Wconversion -Wsign-conversion)
endif()

if (CRT_LAUNCHER_NO_CONSOLE)
    set_target_properties(crt_launcher PROPERTIES WIN32_EXECUTABLE TRUE)
    # With no console, SDL_Log has nowhere to go -- main.c redirects it to
    # a file next to the exe so a cabinet problem is still diagnosable.
    target_compile_definitions(crt_launcher PRIVATE CRT_LAUNCHER_NO_CONSOLE)
endif()

# App icon (assets/icon.ico), embedded via a generated .rc resource script.
# configure_file() bakes in an absolute path to the .ico so windres/rc.exe
# never has to resolve a relative one against whatever directory it's
# invoked from.
if (WIN32)
    set(CRT_LAUNCHER_ICON_PATH "${CMAKE_SOURCE_DIR}/assets/icon.ico")
    configure_file(
        "${CMAKE_SOURCE_DIR}/assets/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY
    )
    target_sources(crt_launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

target_include_directories(crt_launcher PRIVATE src)

if (WIN32)
    # Reg* functions (startup.c's Windows-startup registry toggle) live in
    # advapi32 -- not linked by default under every generator/toolchain
    # (MSVC in particular needs it explicit), so name it rather than rely
    # on MinGW's default library set happening to include it.
    target_link_libraries(crt_launcher PRIVATE advapi32)
endif()

# Prefer the CMake-config SDL2 package (this is what vcpkg installs).
find_package(SDL2 CONFIG QUIET)

if (TARGET SDL2::SDL2)
    # SDL2main must be linked before SDL2 -- it calls into SDL2 and GNU ld
    # resolves symbols left-to-right, so the reverse order fails to link
    # under MinGW even though it's harmless under MSVC. SDL2main is always
    # a small static wrapper (just calls the real main from WinMain) --
    # the same target either way, static or dynamic SDL2 proper.
    if (TARGET SDL2::SDL2main)
        target_link_libraries(crt_launcher PRIVATE SDL2::SDL2main)
    endif()

    if (CRT_LAUNCHER_NO_CONSOLE AND TARGET SDL2::SDL2-static)
        # The no-console build doubles as the distribution/deployment
        # build (see the option's own comment above) -- statically link
        # SDL2 into the exe itself so there's no SDL2.dll to keep
        # alongside it when copying to the cabinet, just the one file.
        # SDL2-static's own CMake target already declares every Win32 lib
        # it needs (user32, gdi32, winmm, ...) via INTERFACE_LINK_LIBRARIES,
        # so nothing else has to be listed here. The dev build (default,
        # console-subsystem) stays dynamically linked -- rebuilding it on
        # every source change is far more frequent, and dynamic linking
        # keeps that fast.
        target_link_libraries(crt_launcher PRIVATE SDL2::SDL2-static)
    else()
        target_link_libraries(crt_launcher PRIVATE SDL2::SDL2)

        if (WIN32)
            # Drop SDL2.dll next to the exe so the build is runnable in-place.
            add_custom_command(TARGET crt_launcher POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:crt_launcher>"
            )
        endif()
    endif()
else()
    # Fallback for MinGW / manually-installed SDL2 dev packages that only
    # ship the classic FindSDL2 module (SDL2_INCLUDE_DIRS / SDL2_LIBRARIES).
    find_package(SDL2 REQUIRED)
    target_include_directories(crt_launcher PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(crt_launcher PRIVATE ${SDL2_LIBRARIES})
endif()

# Ship the default config next to the binary so it runs out of the box.
add_custom_command(TARGET crt_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.ini" "$<TARGET_FILE_DIR:crt_launcher>"
)
