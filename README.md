# CRT Launcher POC

Minimal SDL2 proof-of-concept for an arcade-cabinet frontend, growing
feature-by-feature toward eventually replacing LaunchBox/BigBox. This step
proves out, before any polished launcher UI gets built:

1. Switching to a CRT Emudriver low-resolution display mode (and back to the
   desktop) without stretching or scaling.
2. Pixel-perfect, nearest-neighbor rendering (no filtering blur) at that
   resolution.
3. Reading real titles out of a LaunchBox database, navigating them (with a
   keyboard or a calibrated joystick/controller), and actually launching
   the right emulator with the right ROM. Games that LaunchBox tracks as
   multiple versions/clones (e.g. several regional or hardware variants of
   the same arcade board) collapse into one row that opens a small modal
   to pick a specific version.
4. Favorites (highlighted, sorted first), a per-platform show/hide filter,
   and in-app controller calibration (keyboard, buttons, hats, and analog
   axes) so the cabinet doesn't need a keyboard plugged in day-to-day.

SELECT resolves the selected game's emulator from `Data\Emulators.xml` and
spawns it for real (see "Known placeholder: game launching" below for
exactly what that does and doesn't handle -- it covers the MAME-style
command-line pattern this POC was built and tested against, not every
emulator LaunchBox supports). There's still no fast text search, thumbnails,
or box art -- see "Known placeholder: the game list UI" below.

## Project layout

```
CMakeLists.txt        Build configuration
config.ini             Default runtime config (edit this, no rebuild needed)
src/
  main.c               Entry point / event loop, wires the modules together
  config.h / config.c   Hand-rolled INI loader -> AppConfig, plus input-binding
                         parsing/persistence (calibration, platform filter)
  display.h / display.c Window + display-mode switching (the core of this POC)
  render.h / render.c   SDL_Renderer setup, checkerboard grid, game list, modals
  font_data.h            Placeholder 5x5 bitmap font glyph table
  launchbox.h / launchbox.c  Minimal LaunchBox Platform XML scan -> list of game titles/versions
  gamelist.h / gamelist.c    Selection/scroll state for the on-screen list (system menu,
                             platform filter, game rows) -- no rendering, no launching
  xml_util.h / xml_util.c    Tiny shared substring-based XML field extraction, used by launchbox.c and launcher.c
  launcher.h / launcher.c    Parses Data\Emulators.xml and spawns the resolved emulator + ROM via CreateProcess
  startup.h / startup.c      Windows-startup registration via the HKCU Run registry key (not config.ini)
assets/
  icon.ico               App icon (16/32/48/64/128/256px), embedded into the exe via app.rc.in
  app.rc.in              Resource script template -- CMake fills in an absolute path to icon.ico
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
you see alongside the app, where `SDL_Log` output goes. For a build where
no console is ever created at all, configure a *separate* build directory
with `CRT_LAUNCHER_NO_CONSOLE=ON`:

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
  native resolution (fullscreen-desktop / borderless) and back. This one's
  a literal key, not a calibratable action -- see `toggle_hotkey` below.

### Controls

Everything else is driven by four logical actions -- **UP/DOWN/LEFT/RIGHT**,
**SELECT**, **BACK**, and **MODIFIER** -- resolved each frame against
whatever's bound in `config.ini`'s `[bindings]` section (keyboard key,
joystick button, hat direction, or analog axis push), *plus* a hardcoded
keyboard fallback (arrows, Enter, Escape, Left/Right Shift) that's always
live underneath, regardless of what's calibrated -- so a keyboard plugged
into the cabinet always works as an escape hatch. The examples below use
the fallback's keyboard names for brevity; on a calibrated cabinet, read
"Enter" as "whatever SELECT is bound to", etc.

- **On an install with no `[bindings]` section yet** (a fresh `config.ini`,
  or one that's never been calibrated), the app drops straight into
  **CALIBRATE CONTROLS** on launch instead of the game list -- there's no
  reliable way to navigate to it otherwise. Press whatever you want bound
  to each action in turn (a key, a controller button, a hat direction, or
  push a stick past its deadzone for an axis binding); **Escape** cancels
  calibration at any point without saving. Once all seven actions are
  captured, the result is written to `config.ini`'s `[bindings]` section
  and a confirmation message tells you where to find CALIBRATE CONTROLS
  again (any key/press dismisses it).
- Once calibrated (or on the fallback), the game list (sorted
  alphabetically, case-insensitive, favorites highlighted and sorted
  first) is navigable: **Up/Down** move the highlighted selection one row
  at a time, **Left/Right** jump to the start of the previous/next letter
  group, and both repeat while held.
  - Scrolling up past the top of the list reveals a hidden-by-default
    section: **CALIBRATE CONTROLS** (re-run it any time to recalibrate --
    it overwrites the whole `[bindings]` section), then **ADD TO
    STARTUP**/**REMOVE FROM STARTUP** (the label itself reflects live
    state -- **SELECT** toggles whether Windows launches CRT Launcher at
    sign-in, via a `"CRT Launcher"` value under the current user's
    `HKCU\...\Run` registry key; this is deliberately *not* stored in
    `config.ini` at all -- the registry is the only source of truth, so it
    stays in sync even if toggled from outside the app, e.g. Task
    Manager's Startup tab), then one checkbox-style row per platform
    LaunchBox reported (`X ` prefix = checked). **SELECT**
    on a platform row toggles it on/off and immediately hides/shows that
    platform's games in the list below -- the choice is saved to
    `config.ini`'s `selected_platforms` automatically, so it's still in
    effect next launch. If every platform ends up unchecked (or there's no
    LaunchBox data at all), the list shows a plain `NO GAMES` line instead
    of looking broken/empty.
  - A row showing `>` after the title has multiple versions grouped
    together (see "Known placeholder: the LaunchBox scan" below for how
    they're grouped); plain titles have no suffix at all -- the count
    itself isn't shown, just that there's a choice to make.
    **MODIFIER+SELECT** opens
    a small modal over the (dimmed) list showing just that game's versions
    -- **Up/Down** move within it, wrapping at the ends without touching
    the list underneath, and **MODIFIER+SELECT** again (or **BACK**) closes
    it back to the list.
  - **SELECT** actually launches: it resolves the selected version's
    emulator (its own, or the platform's default) from `Data\Emulators.xml`
    and spawns it with the ROM. On a still-closed multi-version row it
    launches that game's *default* version (its own primary `<Game>` entry,
    not a guess) rather than an arbitrary clone -- MODIFIER+SELECT first if
    you want a specific one; inside the open modal, SELECT launches
    whichever version is highlighted. Every resolved command line and
    working directory is logged before spawning, and a failed resolve or
    `CreateProcess` logs a warning instead of crashing.
- **BACK** closes the version-picker modal if one is open. At the top
  level of the main list, it instead opens an "EXIT" confirmation modal
  (**SELECT** confirms and quits, **BACK** backs out without quitting) --
  it's reachable from a controller button that's easy to bump by accident,
  so it doesn't quit immediately. Closing the window also quits, no
  confirmation needed there.

The on-screen list always shows a `GAME <N> OF <M>` (or `SETTINGS` /
`PLATFORMS`) counter, right-aligned above the list, reflecting the
currently *filtered* view -- not the full LaunchBox database if some
platforms are unchecked.

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

[launchbox]
launchbox_dir=
selected_platforms=All
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
- `launchbox_dir` is an optional path to a **LaunchBox install root** -- the
  folder containing `LaunchBox.exe` and a `Data` subfolder (e.g.
  `E:\LaunchBox`), not a single export file. Pointing at the root rather
  than one XML is what lets the same setting drive both the game list
  (`Data\Platforms\*.xml`) and emulator resolution (`Data\Emulators.xml`)
  from one place. If set, the app scans every `*.xml` file directly inside
  `<launchbox_dir>\Data\Platforms` and `Data\Emulators.xml` -- and nothing
  else under the install -- sorts the resulting game list alphabetically
  (case-insensitive), and shows the count/list both on screen and in the
  log. If left blank, the app also tries to auto-detect a LaunchBox install
  as a sibling of its own folder (e.g. `Cabinet\LaunchBox` next to
  `Cabinet\CRT Launcher`) before giving up; set it explicitly if your
  install lives somewhere else. If neither finds anything, the screen just
  shows `LAUNCHBOX: NOT CONFIGURED` instead, and SELECT does nothing.
- `selected_platforms` controls which platforms' games show up in the list
  -- `All` (the default) shows every platform, `None` shows nothing, or a
  comma-separated list of platform names (matching the `*.xml` filenames in
  `Data\Platforms` minus the extension, e.g. `Arcade,SNES`) shows only
  those. Normally you don't hand-edit this -- toggling a platform's
  checkbox row in the app (see above) rewrites it for you.
- A `[bindings]` section maps each of the seven actions (`up`, `down`,
  `left`, `right`, `select`, `back`, `modifier`) to `KEYBOARD <key name>`,
  `JOYBUTTON <index>`, `JOYHAT <hat> <UP|DOWN|LEFT|RIGHT>`, or
  `JOYAXIS <axis> <POSITIVE|NEGATIVE>`. Its *absence* is what tells the app
  "never calibrated" and makes it launch straight into CALIBRATE CONTROLS
  (see "Controls" above); once written by calibration (or hand-edited in
  that same format), it's used instead of the keyboard-only defaults --
  though the hardcoded fallback (arrows/Enter/Escape/Shift) stays live
  underneath regardless, so a keyboard always works as a backup.
  Recalibrating overwrites the whole section; nothing else in `config.ini`
  is touched.

Changes take effect on the next launch -- there's no hot-reload.

## Known placeholder: the LaunchBox scan

`launchbox.c` is a substring search, not a real XML parser. For each
`*.xml` file found directly inside `Data\Platforms` (via `FindFirstFile`/
`FindNextFile`, Windows-only, no recursion into subfolders) it walks every
`<Game>...</Game>` block and, within that one block's bounds, reads
`<Title>`, `<DatabaseID>`, `<ApplicationPath>` (the ROM path), `<ID>`,
`<Emulator>`, `<Favorite>`, `<Region>`, and `<Version>` (the last two feed
its version label the same way an `<AdditionalApplication>`'s do -- see
below). It also reads matching `<AdditionalApplication>` blocks'
`<ApplicationPath>`, `<EmulatorId>` (a different field name for the same
concept -- LaunchBox itself isn't consistent here), `<Region>`, and
`<Version>`. The platform name (e.g. `"Arcade"`) is just the XML filename,
not a field read from inside it. All of that -- ROM path, emulator,
platform -- is what `launcher.c` needs later to actually start the game,
not just display it.

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
- **Version labels use LaunchBox's own `Version` text** (e.g.
  `(World 940223)`), falling back to `Region` (e.g. `North America`), then
  the ROM filename (e.g. `atetris`) if both are empty -- the same fallback
  order whether the entry is a `<Game>`'s own primary entry or one of its
  `<AdditionalApplication>` siblings.

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

**If no emulator can be resolved at all** (no `emulator_id` on the game, and
no `Default` `EmulatorPlatform` mapping for its platform) **and its
`ApplicationPath` ends in `.exe`**, it's launched directly instead --
no command line, no `%romlocation%` substitution, just the exe itself with
its own containing folder as the working directory. `ApplicationPath` is
used exactly as LaunchBox stored it here, *not* joined onto
`launchbox_dir` the way a relative ROM path is -- Windows-platform entries
store their own absolute path already (e.g. `D:\Fightcade\Fightcade2.exe`,
nowhere near the LaunchBox install). This is how LaunchBox's
"Windows" platform works in a real database (`Data\Platforms\Windows.xml`,
if you have one): those games just don't have an emulator configured at
all, since LaunchBox itself launches their `ApplicationPath` directly. The
`.exe` check is a sanity guard, not a platform-name check -- any platform
with an unresolvable emulator and an exe-pointing `ApplicationPath` gets
this treatment, not just one literally named "Windows". Anything else
still just logs a warning and fails, same as before.

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
per unique title (with a trailing `>` when it has multiple versions --
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

Platforms can be shown/hidden entirely (see the checkbox rows in
"Controls" above), but there's still no *grouping* by platform within the
list itself -- visible games from every checked platform interleave in one
alphabetical run, not separate sections/tabs. There's also still no
paging, no text search, and no thumbnails/box art -- with a real database
that's several thousand unique titles, so even with letter-jump and
platform filtering, getting to one specific title can take some hunting.
That's expected for this step; a more polished game-list UI (fast text
search, art) is future work.

## Known placeholder: the font

`font_data.h` is a hand-authored 5x5-pixel block font covering just
`A-Z 0-9 space - : . ( ) > '`, drawn as filled rectangles rather than
sampled from a texture. `>` is reused as a disclosure-arrow icon (see the
game list above), not literal punctuation. Any other character (accents,
CJK text, punctuation like `/` or `?`) renders as a small diamond rather
than blank space -- deliberate, so unsupported text is visibly *wrong*
instead of silently looking like empty/missing data. That's intentional
for this POC — it's crisp by construction and needed no font asset or
download. It is **not** the final font pipeline: replace it with a real
BMFont-style bitmap font (image + glyph descriptor), e.g. a licensed pixel
font like "Press Start 2P" (SIL Open Font License) exported as a sprite
sheet, once the actual game-list UI work starts.
