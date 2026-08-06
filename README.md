# CRT Launcher POC

Minimal SDL2 proof-of-concept for an arcade-cabinet frontend. This step
proves out, before any real launcher UI gets built:

1. Switching to a CRT Emudriver low-resolution display mode (and back to the
   desktop) without stretching or scaling.
2. Pixel-perfect, nearest-neighbor rendering (no filtering blur) at that
   resolution.
3. Reading real titles out of a LaunchBox database, navigating them with
   the keyboard, and actually launching the right emulator with the right
   ROM. Games that LaunchBox tracks as multiple versions/clones (e.g.
   several regional or hardware variants of the same arcade board) collapse
   into one row that opens a small modal to pick a specific version.

Pressing Enter resolves the selected game's emulator from
`Data\Emulators.xml` and spawns it for real (see "Known placeholder: game
launching" below for exactly what that does and doesn't handle -- it
covers the MAME-style command-line pattern this POC was built and tested
against, not every emulator LaunchBox supports). A real game-list UI (fast
filtering, platform tabs, art) is still out of scope.

## Project layout

```
CMakeLists.txt        Build configuration
config.ini             Default runtime config (edit this, no rebuild needed)
src/
  main.c               Entry point / event loop, wires the modules together
  config.h / config.c   Hand-rolled INI loader -> AppConfig
  display.h / display.c Window + display-mode switching (the core of this POC)
  render.h / render.c   SDL_Renderer setup, checkerboard grid, status text
  font_data.h            Placeholder 5x5 bitmap font glyph table
  launchbox.h / launchbox.c  Minimal LaunchBox Platform XML scan -> list of game titles/versions
  gamelist.h / gamelist.c    Selection/scroll state for the on-screen list (no rendering, no launching)
  xml_util.h / xml_util.c    Tiny shared substring-based XML field extraction, used by launchbox.c and launcher.c
  launcher.h / launcher.c    Parses Data\Emulators.xml and spawns the resolved emulator + ROM via CreateProcess
```

## Building (Windows)

You need a C compiler and SDL2's development package. The easiest path on
Windows is [vcpkg](https://github.com/microsoft/vcpkg) + MSVC.

### Option A: MSVC + vcpkg (recommended)

```bash
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg install sdl2:x64-windows
```

Then configure and build, pointing CMake at vcpkg's toolchain file:

```bash
cmake -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug
```

The executable and `SDL2.dll` land in `build/Debug/` (or `build/Release/`
for a Release build), alongside a copy of `config.ini`.

### Option B: MinGW via MSYS2 (lightweight, no Visual Studio)

Install [MSYS2](https://www.msys2.org/) (`winget install MSYS2.MSYS2` works),
then from an MSYS2 shell install the toolchain, CMake, and SDL2 as regular
packages -- no manual SDL2 zip download needed, MSYS2's package already
ships a modern CMake config (`SDL2::SDL2` target):

```bash
pacman -Syu   # first run updates core packages and may ask to restart the shell; run it again if so
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2
```

Add `C:\msys64\mingw64\bin` to your PATH (persist it via
`setx PATH "%PATH%;C:\msys64\mingw64\bin"` in a normal terminal, or set it
through Windows' environment variable settings), then from any terminal:

```bash
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

CMake finds SDL2 automatically via `find_package(SDL2 CONFIG)` since it's
already on `CMAKE_PREFIX_PATH` through the MinGW install. The executable,
`SDL2.dll`, and `config.ini` all land directly in `build/`.

### Optional: a no-console build for deployment

The default build is a console-subsystem exe -- that's the terminal window
you see alongside the app, where `SDL_Log` output goes. `config.ini`'s
`[debug] show_console=false` can hide that window, but since Windows
creates it before the app's own code runs, there's an unavoidable brief
flash first. For a build where no console is ever created at all -- no
flash, nothing -- configure a *separate* build directory with
`CRT_LAUNCHER_NO_CONSOLE=ON`:

```bash
cmake -B build-noconsole -G "MinGW Makefiles" -DCRT_LAUNCHER_NO_CONSOLE=ON
cmake --build build-noconsole
```

This produces a WINDOWS-subsystem `crt_launcher.exe` in `build-noconsole/`
with the same `config.ini`/`SDL2.dll` copy step as the normal build. Trade-
off: `SDL_Log` has nowhere to write to in this build, so there's no log at
all, visible or otherwise -- keep the regular `build/` around for
day-to-day development where you actually want to see it, and only use
this one once you're happy and ready to deploy to the cabinet.

## Running

```bash
build\crt_launcher.exe
```

(With the MSVC/vcpkg path in Option A, the binary instead lands in
`build\Debug\crt_launcher.exe` or `build\Release\crt_launcher.exe`.)

Run it from a directory that has `config.ini` next to the executable (the
build already copies it there). Watch the console -- every resolution
request, what actually got applied, and any fallback is logged there.

- **On a normal dev monitor**: the configured low-res mode (e.g. 320x240)
  almost certainly won't exist as a real display mode. You'll see a
  `[display] WARNING: no display mode matching ...` log line, and the app
  falls back to an ordinary windowed surface at that exact pixel size —
  still no stretching, just not exclusive-fullscreen.
- **On the cabinet with CRT Emudriver**: the exact mode should be found and
  applied as real exclusive fullscreen, logged as
  `[display] APPLIED: exclusive fullscreen low-res mode ...`.
- Press the configured hotkey (default **F5**) to toggle to the desktop's
  native resolution (fullscreen-desktop / borderless) and back.
- If `launchbox_dir` is configured (see below), the game list (sorted
  alphabetically, case-insensitive) is navigable: **Up/Down** move the
  highlighted selection one row at a time, **Left/Right** jump to the start
  of the previous/next letter group, and both repeat while held.
  - A row showing `(N)>` after the title has N versions grouped together
    (see "Known placeholder: the LaunchBox scan" below for how they're
    grouped); plain titles have no suffix at all. **Shift+Enter** opens a
    small modal over the (dimmed) list showing just that game's versions --
    **Up/Down** move within it, wrapping at the ends without touching the
    list underneath, and **Shift+Enter** again (or **Esc**) closes it back
    to the list.
  - **Enter** actually launches: it resolves the selected version's
    emulator (its own, or the platform's default) from `Data\Emulators.xml`
    and spawns it with the ROM. On a still-closed multi-version row it
    launches that game's *default* version (its own primary `<Game>` entry,
    not a guess) rather than an arbitrary clone -- Shift+Enter first if you
    want a specific one; inside the open modal, Enter launches whichever
    version is highlighted. Every resolved command line and working
    directory is logged before spawning, and a failed resolve or
    `CreateProcess` logs a warning instead of crashing.
- Press **Esc** to close the version-picker modal if one is open, or quit
  otherwise; closing the window also quits.

The on-screen text always shows the currently active mode and resolution,
so you can visually confirm state when toggling.

## Editing config.ini

```ini
[display]
width=320
height=240
refresh_rate=60

[input]
toggle_hotkey=F5
nav_repeat_delay_ms=250
nav_repeat_interval_ms=40

[debug]
show_console=true

[launchbox]
launchbox_dir=
```

- `width` / `height` / `refresh_rate` describe the low-res mode to request.
  `refresh_rate` can be `0` to match any refresh rate at that resolution.
- `toggle_hotkey` accepts any name `SDL_GetKeyFromName()` understands (e.g.
  `F1`-`F12`, `A`-`Z`, `0`-`9`, `Space`, `Escape`, `Tab`, ...). An
  unrecognized name logs a warning and falls back to `F5`.
- `nav_repeat_delay_ms` / `nav_repeat_interval_ms` control how holding
  Up/Down/Left/Right repeats while browsing the list -- timed by the app
  itself (not the OS's keyboard repeat rate), so they're actually tunable.
  Delay is how long a direction must be held before it starts repeating;
  interval is the time between repeats once it's going (lower = faster).
- `show_console` set to `false` hides the console window that opens
  alongside the app on Windows (SDL_Log still logs to it, it's just not
  visible) -- there's an unavoidable brief flash on startup before it hides,
  since Windows creates the console before the app's own code can run.
- `launchbox_dir` is an optional path to a **LaunchBox install root** -- the
  folder containing `LaunchBox.exe` and a `Data` subfolder (e.g.
  `E:\LaunchBox`), not a single export file. Pointing at the root rather
  than one XML is what lets the same setting drive both the game list
  (`Data\Platforms\*.xml`) and emulator resolution (`Data\Emulators.xml`)
  from one place. If set, the app scans every `*.xml` file directly inside
  `<launchbox_dir>\Data\Platforms` and `Data\Emulators.xml` -- and nothing
  else under the install -- sorts the resulting game list alphabetically
  (case-insensitive), and shows the count/list both on screen and in the
  log. Leave blank to skip -- the screen just shows
  `LAUNCHBOX: NOT CONFIGURED` instead, and Enter does nothing.

Changes take effect on the next launch -- there's no hot-reload.

## Known placeholder: the LaunchBox scan

`launchbox.c` is a substring search, not a real XML parser. For each
`*.xml` file found directly inside `Data\Platforms` (via `FindFirstFile`/
`FindNextFile`, Windows-only, no recursion into subfolders) it walks every
`<Game>...</Game>` block and, within that one block's bounds, reads five
fields: `<Title>`, `<DatabaseID>`, `<ApplicationPath>` (the ROM path),
`<ID>`, and `<Emulator>`. It also reads matching `<AdditionalApplication>`
blocks' `<ApplicationPath>` and `<EmulatorId>` (a different field name for
the same concept -- LaunchBox itself isn't consistent here). The platform
name (e.g. `"Arcade"`) is just the XML filename, not a field read from
inside it. All of that -- ROM path, emulator, platform -- is what
`launcher.c` needs later to actually start the game, not just display it.

All the records from every platform file are then sorted case-insensitively
by title (ROM filename as a tiebreaker), and consecutive entries sharing a
non-empty `DatabaseID` are collapsed into one `LaunchboxGameGroup` with
multiple `LaunchboxVersion` entries -- that's the field real LaunchBox
databases use to mark clones/regional variants as "the same game" (e.g. six
different arcade `Tetris` ROMs all carrying the same `DatabaseID`).

Two things worth knowing if this looks wrong for some game:

- **Grouping assumes same `DatabaseID` implies same `Title`.** It's a
  linear scan over the title-sorted array, not a real group-by: it only
  merges entries that are *adjacent* after sorting. Every real LaunchBox
  database checked while building this held that assumption, but it's not
  enforced or validated -- there's no fallback if a same-`DatabaseID` pair
  ever had different titles.
- **Version labels use LaunchBox's own `Region`/`Version` text** when a
  version comes from an `<AdditionalApplication>` (e.g. `(World 940223)`),
  falling back to the ROM filename (e.g. `atetris`) when those fields are
  empty, or when the version is a `<Game>`'s own primary entry rather than
  an `<AdditionalApplication>`.

Sorting is by raw title text, not LaunchBox's own `SortTitle` field (which
strips leading articles like "The"; this scanner never reads it). Reads
are hard-scoped to `<launchbox_dir>\Data\Platforms`; nothing else under
the LaunchBox install (Roms, Images, ThirdParty, etc.) is ever opened by
this scanner -- `launcher.c` separately reads `Data\Emulators.xml`, see
below.

## Known placeholder: game launching

`launcher.c` parses `Data\Emulators.xml` (again a substring search, not a
real XML parser -- and again `Data\`-only, nothing else under the install
is read) into `<Emulator>` records (ID, `ApplicationPath`, a base
`CommandLine` template, quoting flags) and `<EmulatorPlatform>` records
(which emulator handles which platform, with an optional per-platform
`CommandLine` override -- e.g. RetroArch's core selection). One nuance
worth knowing: `<EmulatorPlatform>` blocks contain a *child field* also
literally named `<Emulator>` (the ID being mapped), so parsing has to walk
the file recognizing `<EmulatorPlatform>` blocks first and jump past them
whole -- a naive global search for `<Emulator>` would misparse that child
field as the start of an entire emulator record.

At launch time, the version's own `emulator_id` is used if set, else the
platform's `Default` `EmulatorPlatform` entry; the resolved command line
has `%romlocation%` replaced with the ROM's containing folder, and the ROM
itself is appended per that emulator's quoting/path rules (`NoQuotes`,
`FileNameWithoutExtensionAndPath`) before spawning via `CreateProcess`.
This was built against and tested with MAME's actual command-line pattern
(`-rompath %romlocation%` plus a bare ROM name) -- it should work for any
emulator using that same %romlocation%-plus-filename convention, but
**does not replicate the rest of what LaunchBox itself does around a
launch**: no `AutoHotkeyScript` nag-screen skipping (MAME's config in this
database presses Space a few times on startup via one), no pause/save-state
key handling, no achievements login, no per-emulator startup delays or
window hiding. It fires the process and returns; it doesn't wait for it,
check whether the game actually loaded, or handle it exiting.

## Known placeholder: the game list UI

`gamelist.c` + `render.c` are a bare but now single-level list: one row
per unique title (with a `(N)>` suffix when it has multiple versions --
see above), a solid highlight bar on whichever row is selected,
scroll-into-view so the highlight never runs off screen, and Left/Right
jump-to-letter (an O(n) linear scan per keypress over unique titles, fine
at a few thousand but not the data structure a real jump list would use).
The list itself never nests anything inline anymore -- versions live
entirely in the modal (see below), so `gamelist_scroll_into_view` is just
plain single-level scrolling with no "how deep into an expanded row am I"
math to get wrong.

The version picker is `render_draw_modal_list()` in `render.c` (declared
in `render.h`, not `static`): a dim overlay plus a centered, bordered box
sized to fit the longest string passed in, listing plain C strings with
one highlighted. It takes no game/version types at all -- just a title,
an array of strings, and a selected index -- specifically so it's reusable
for any future modal list (a settings menu, an emulator picker, etc.)
without changes. `render_frame()` is the only caller so far, building the
`items` array from the selected group's `LaunchboxVersion` labels into a
fixed 128-entry stack array (comfortably above any real game's version
count -- the largest observed while building this was 9) before calling
it.

There's still no paging, no text search, no thumbnails/box art, and no
grouping by platform -- with a real database that's several thousand
unique titles, so even with letter-jump, getting to one specific title
can take some hunting. That's expected for this step; a real game-list UI
(fast filtering, platform tabs, art) is future work.

## Known placeholder: the font

`font_data.h` is a hand-authored 5x5-pixel block font covering just
`A-Z 0-9 space - : . ( ) >`, drawn as filled rectangles rather than sampled
from a texture. `>` is reused as a disclosure-arrow icon (see the game
list above), not literal punctuation. That's intentional for this POC — it's crisp by
construction and needed no font asset or download. It is **not** the final
font pipeline: replace it with a real BMFont-style bitmap font (image +
glyph descriptor), e.g. a licensed pixel font like "Press Start 2P"
(SIL Open Font License) exported as a sprite sheet, once the actual
game-list UI work starts.
