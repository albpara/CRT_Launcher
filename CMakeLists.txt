cmake_minimum_required(VERSION 3.16)
project(crt_launcher C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# OFF (default): a normal console-subsystem exe -- the terminal that opens
# alongside it is where SDL_Log's output goes, and config.ini's
# [debug] show_console lets you hide that window at runtime without
# rebuilding, handy while developing.
# ON: a WINDOWS-subsystem exe -- Windows never creates a console for it at
# all, so there's no window to hide and no startup flash either, but
# SDL_Log has nowhere to write to. Build a separate directory with this ON
# (e.g. `cmake -B build-noconsole -DCRT_LAUNCHER_NO_CONSOLE=ON`) for real
# cabinet deployment once you're done watching the log; keep the default
# OFF build for day-to-day development.
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
)

if (CRT_LAUNCHER_NO_CONSOLE)
    set_target_properties(crt_launcher PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(crt_launcher PRIVATE src)

# Prefer the CMake-config SDL2 package (this is what vcpkg installs).
find_package(SDL2 CONFIG QUIET)

if (TARGET SDL2::SDL2)
    # SDL2main must be linked before SDL2 -- it calls into SDL2 and GNU ld
    # resolves symbols left-to-right, so the reverse order fails to link
    # under MinGW even though it's harmless under MSVC.
    if (TARGET SDL2::SDL2main)
        target_link_libraries(crt_launcher PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(crt_launcher PRIVATE SDL2::SDL2)

    if (WIN32)
        # Drop SDL2.dll next to the exe so the build is runnable in-place.
        add_custom_command(TARGET crt_launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:crt_launcher>"
        )
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
