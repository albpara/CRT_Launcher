# CRT Launcher

A minimal SDL2/C game-launcher frontend for an arcade cabinet running
Windows and [CRT Emudriver](https://geedorah.com/eiusdemmodi/), built to
eventually replace LaunchBox/BigBox on a Sega Astro City. It reads an
existing LaunchBox database and drives everything at a native low
resolution (e.g. 320x240) with pixel-perfect, unfiltered rendering.

**Features**

- Reads games straight from `LaunchBox\Data\Platforms\*.xml` (nothing else
  under the install is touched) and launches them through the emulators
  defined in `Data\Emulators.xml` -- or directly, for Windows apps.
- Clones/regional variants collapse into one row with a version-picker
  modal; favorites are highlighted and sorted first.
- Per-platform show/hide filter, persisted to `config.ini`.
- In-app controller calibration (keyboard, buttons, hats, analog axes),
  auto-started on first run.
- A Galaga starfield screensaver (CRT burn-in protection), which can also
  replace the default checkerboard as the launcher background.
- Run-at-Windows-startup toggle, exclusive-fullscreen low-res with
  windowed fallback.

## How to build

Requires CMake and SDL2. The path this project actually uses is MinGW via
[MSYS2](https://www.msys2.org/):

```bash
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2
```

Add `C:\msys64\mingw64\bin` to your PATH, then:

```bash
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

The exe, `SDL2.dll`, and a copy of `config.ini` land in `build/`.

Alternatively, MSVC + [vcpkg](https://github.com/microsoft/vcpkg)
(`vcpkg install sdl2:x64-windows`, then configure with vcpkg's CMake
toolchain file) works too; binaries land in `build/Debug` or
`build/Release`.

### Distribution build

For deployment to the cabinet, build a second directory with
`CRT_LAUNCHER_NO_CONSOLE=ON`:

```bash
cmake -B build-noconsole -G "MinGW Makefiles" -DCRT_LAUNCHER_NO_CONSOLE=ON
cmake --build build-noconsole
```

This produces a WINDOWS-subsystem exe (no console window ever) with SDL2
statically linked in -- deploying is copying `crt_launcher.exe` and
`config.ini`, nothing else. Trade-off: no `SDL_Log` output at all, so keep
the default build for development.

## Running

Put `config.ini` next to the exe (the build already does; if it's missing
the app generates a default one) and run `crt_launcher.exe`. If a
`LaunchBox` folder sits next to the exe's own folder it's auto-detected;
otherwise set `launchbox_dir` in `config.ini`.

### Controls

Seven actions -- 4 directions, SELECT, BACK, MODIFIER -- bound via the
in-app calibration (any key, controller button, hat, or axis). Arrow
keys/Enter/Escape/Shift always work as a hardcoded keyboard fallback.

- **Up/Down** move, **Left/Right** jump by first letter, both repeat.
- **SELECT** launches the selected game (its default version).
- **MODIFIER+SELECT** opens the version picker on `>`-suffixed rows.
- **BACK** closes modals; at the top level it opens an exit confirmation.
- Scrolling up past the top reveals the system menu: CALIBRATE CONTROLS,
  ADD/REMOVE FROM STARTUP, and one checkbox row per platform (SELECT
  toggles it; the choice persists).
- The configured hotkey (default **F5**) toggles low-res <-> desktop
  resolution.

After a minute without input a Galaga starfield takes over the screen as
the screensaver; any input wakes it. Set `background=starfield` to have it
drift behind the list as well, in place of the default checkerboard, and
`font=galaga88` to swap the default 5x5 font for the larger 7x7 Galaga '88
one. See the comments in [config.ini](config.ini) for every setting.

## Known limitations

- The XML "parser" is a bounded substring scanner -- fine for LaunchBox's
  flat export format, not general XML.
- Game launching covers the MAME-style `%romlocation%` command-line
  pattern and direct .exe launches; no AutoHotkey scripts, pause menus, or
  achievements.
- Both bitmap fonts cover `A-Z 0-9 space - : . ( ) > '` and nothing else;
  anything outside that renders as a placeholder diamond. `galaga88`'s
  `A-Z 0-9` come from the Galaga '88 (PC Engine) ROM at offset `0x1C000`
  -- 36 consecutive 8x8 4bpp planar tiles in that order, the only font
  block in the image, each drawing 7x7 in a single palette index (so the
  data is really 1-bit). Its punctuation isn't in the ROM and is
  hand-drawn.

## License

[MIT](LICENSE), with one exception: the `galaga88` font's `A-Z 0-9`
glyphs are extracted from the PC Engine release of Galaga '88 (not the
arcade original, which runs on different hardware). Those letterforms are
Namco's and aren't mine to relicense -- the MIT grant covers this
project's own code. `compact`, the default font, is unaffected.
- Flat list only: no box art, no text search; letter-jump is a linear
  scan (fine at a few thousand titles).
