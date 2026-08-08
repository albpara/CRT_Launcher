# CLAUDE.md

Guidance for working on this codebase in future sessions. For build/run/config
instructions aimed at a *user*, see [README.md](README.md) — this file is
about how to develop it, not how to use it.

## What this is

A custom SDL2/C frontend/launcher for a Sega Astro City arcade cabinet
(Windows 11, CRT Emudriver), intended to eventually replace LaunchBox/BigBox.
It has grown feature-by-feature, each explicitly requested — there is no
overall spec beyond what's in the code and README right now.

## Build & test workflow

- Two build directories exist, both intentional — don't merge or delete
  either without asking:
  - `build/` — default config, console-subsystem exe. This is the normal
    day-to-day dev build; `SDL_Log` output is visible in the terminal window
    it opens alongside the app.
  - `build-noconsole/` — configured with `-DCRT_LAUNCHER_NO_CONSOLE=ON`, a
    WINDOWS-subsystem exe with no console ever (not even hidden — Windows
    never allocates one). No log output at all. Only touch this one when the
    no-console behavior itself is what's being verified.
- After any source change: rebuild `build/`, then re-set `build/config.ini`'s
  `launchbox_dir=E:\LaunchBox` (and, until it's been calibrated once, a
  `[bindings]` section — see "Controls" below) and launch
  `build\crt_launcher.exe`. CMake's post-build copy step resets
  `build/config.ini` back to the tracked root `config.ini` (blank
  `launchbox_dir`, no `[bindings]`) — but only on a build that actually
  recompiled something; a no-op `cmake --build` with no source changes
  skips the POST_BUILD command entirely under MinGW Makefiles and leaves
  the previous `build/config.ini` in place. Don't assume either way —
  check it after every rebuild. The user now handles all interactive/
  visual verification themselves — just launch it for them after changes,
  don't try to simulate keyboard input yourself. SDL2 input simulation
  (`keybd_event`/`PostMessage`/`SendInput`) has repeatedly proven
  unreliable in this sandboxed environment, especially for held-modifier
  combos.
- Stop any already-running `crt_launcher.exe` before rebuilding — it locks
  the `.exe` and the link step fails silently otherwise (permission denied).
- The root `config.ini`'s `launchbox_dir` must stay blank when committed —
  it's a personal path, only ever set transiently in the build output
  copies (which are gitignored).

## Architecture

- `config.c/h` — hand-rolled INI parser, `AppConfig`. No hot-reload; changes
  take effect on next launch. Also owns the `InputBinding`/`InputAction`
  types (keyboard, joystick button, hat, or axis — deliberately
  source-agnostic, resolved against real-time input state in `main.c`) and
  `config_save_bindings()`/`config_save_selected_platforms()`, which
  rewrite just one section/line of `config.ini` in place (binary-mode file
  I/O throughout — Windows' text-mode CRT would otherwise double-insert
  `\r` on a read-modify-write round trip of an already-CRLF file).
- `main.c` — entry point and the only place with a real event loop. Wires
  every other module together, resolves each frame's held/pressed input
  against `AppConfig.bindings` (`binding_is_held`) with its own
  `SDL_GetTicks`-based repeat timing (not the OS's key-repeat rate) for
  navigation, and a hardcoded keyboard fallback (`ACTION_FALLBACK_SCANCODE`
  — Up/Down/Left/Right/Enter/Escape/Shift, deliberately not configurable)
  layered under whatever's actually calibrated, so a keyboard plugged into
  the cabinet always works as an escape hatch. Also drives the in-app
  calibration flow itself (press-to-bind capture for all four binding
  types, auto-starting on an uncalibrated install) and the SELECT/BACK
  dispatch for launching, opening the system/platform rows, and the exit
  confirmation.
- `display.c/h` — SDL display-mode selection with exact-match-or-windowed-
  fallback logic, plus fullscreen/desktop toggling.
- `render.c/h` — all drawing: checkerboard background, the hand-rolled
  bitmap font (`font_data.h`), the game list, and `render_draw_modal_list` —
  a deliberately generic modal primitive (plain strings in, nothing
  game-specific) meant to be reused for any future overlay, not just the
  version picker it was originally built for.
- `launchbox.c/h` — scans `<launchbox_dir>\Data\Platforms\*.xml` **only**;
  never reads anything else under the LaunchBox install (this scope
  restriction was an explicit early requirement — keep it that way). Not a
  real XML parser, just bounded substring search (see `xml_util.c`). Groups
  clones/regional variants via two separate LaunchBox mechanisms —
  `<DatabaseID>` on `<Game>`, and `<AdditionalApplication>`/`<GameID>` — a
  real schema inconsistency between the two, documented in the file's
  comments. Groups end up ordered favorites-first (from each game's
  `<Favorite>` field), each block alphabetical.
- `gamelist.c/h` — pure navigation state; no rendering, no launching lives
  here. Owns the small "system menu" concept (`GAMELIST_SYSTEM_ENTRY_COUNT`,
  currently just CALIBRATE CONTROLS) and, right below it in the same
  unified row space, one checkbox-style toggle row per platform LaunchBox
  reported (`LaunchboxInfo.platform_names`) — both pinned above
  favorites/games, hidden by default (initial scroll position starts past
  them), revealed by scrolling up past the top of the real list. Unchecking
  a platform doesn't touch `LaunchboxInfo` itself — `gamelist.c` maintains
  a separate filtered `visible_group_indices` view over `lb->groups` and
  every navigation function walks that instead, so the underlying scan
  data stays a straight reflection of what's on disk. Selection persists
  via config.ini's `[launchbox] selected_platforms` key
  (`config_save_selected_platforms`), rewritten on every toggle.
- `launcher.c/h` — parses `Data\Emulators.xml` once at startup and keeps it
  in memory (not re-read per launch), then spawns the emulator via
  `CreateProcessA` with the working directory set to the emulator's own
  folder.
- `xml_util.c/h` — the substring-XML helpers shared by `launchbox.c` and
  `launcher.c`.

## Established conventions

- **Config-driven, no build flags for runtime behavior.** Anything that
  might need tuning (nav repeat speed, hotkeys, bindings, platform filter)
  belongs in `config.ini`, not a CMake option — reserve build-time options
  for things that are genuinely build-time-only, like the console-subsystem
  switch (console visibility used to be a runtime `[debug]` toggle; it's
  build-time-only now — see `CRT_LAUNCHER_NO_CONSOLE` above).
- **Reusable UI primitives over one-off widgets.** `render_draw_modal_list`
  takes plain strings and knows nothing about games, specifically so future
  menus (settings, calibration) can reuse it as-is. Default new UI-drawing
  code to this shape even when there's only one call site today. Expect
  follow-up requests to arrive as small, literal visual tweaks (arrow
  position, spacing, color) — implement those directly rather than
  re-designing the component each time.
- **The font is a known, intentional placeholder.** 5x5 hand-drawn bitmap in
  `font_data.h`, only supports `A-Z 0-9 space - : . ( ) > '`. Any other
  character renders as a small diamond (`FONT_UNKNOWN`) rather than blank —
  do not let unsupported characters silently render as nothing; that's
  indistinguishable from actually-missing data (this bit a real bug: titles
  with accents/CJK text looked like empty or whitespace-padded rows before
  `FONT_UNKNOWN` was added).
- **Keep it simple; don't design ahead of what's asked.** This project has
  explicitly favored small, direct fixes over speculative abstraction
  throughout. When in doubt, prefer the smaller change.

## Known placeholders / deliberately out of scope

- The XML "parser" — bounded substring search, not a real XML parser (no
  nesting beyond one block, no attributes, no CDATA).
- The font (see above) — swap for a real bitmap font pipeline eventually.
- Game launching — MAME-style `%romlocation%` command-line substitution
  only. No AutoHotkey, pause menus, achievements, or anything beyond
  spawning the emulator process.
- The game-list UI — still just a flat scrolling list, no thumbnails/box
  art. Platform *filtering* (checkbox rows) exists now, but there's no
  fast text search, and letter-jump is an O(n) linear scan per keypress
  (fine at a few thousand titles, not the data structure a real jump list
  would use).

## Git

Only commit when explicitly asked — this repo was initialized on request,
not commit-on-every-change. `build/` and `build-noconsole/` are gitignored
(pure CMake output, regenerate with `cmake -B <dir> ...`).
