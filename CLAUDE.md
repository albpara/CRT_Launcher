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
  - `build-noconsole/` — configured with `-DCRT_LAUNCHER_NO_CONSOLE=ON`, the
    distribution/deployment build: a WINDOWS-subsystem exe with no console
    ever (not even hidden — Windows never allocates one) AND SDL2 statically
    linked into the exe itself (`SDL2::SDL2-static`, MSYS2's package ships
    `libSDL2.a`) instead of dynamically against `SDL2.dll` — the only
    runtime dependencies left are standard Windows system DLLs, verified via
    `objdump -p`. No log output at all. Only touch this one when the
    no-console/static-linking behavior itself is what's being verified.
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
  take effect on next launch. `config_load()` no longer early-returns when
  `path` doesn't exist at all -- it used to, which skipped sibling-folder
  `launchbox_dir` auto-detection entirely (a bare install with only the
  exe copied somewhere showed no games even with LaunchBox sitting right
  there as a sibling, since detection never got a chance to run). Now it
  falls through to the same validation/auto-detect/logging a real file
  goes through, then `config_write_default_file()` writes a fresh minimal
  config.ini to `path` afterward -- deliberately NOT a copy of the tracked
  root `config.ini` (that one's the fully-documented reference; this is a
  short bootstrap kept intentionally out of lockstep with it), and
  deliberately leaves both `[bindings]` (triggers first-launch calibration,
  same as any uncalibrated install) and `launchbox_dir` (keeps
  auto-detection running on every future launch) absent/blank rather than
  baking in whatever was true at that one moment. Also owns the
  `InputBinding`/`InputAction`
  types (keyboard, joystick button, hat, or axis — deliberately
  source-agnostic, resolved against real-time input state in `main.c`) and
  `config_save_bindings()`/`config_save_selected_platforms()`, which
  rewrite just one section/line of `config.ini` in place (binary-mode file
  I/O throughout — Windows' text-mode CRT would otherwise double-insert
  `\r` on a read-modify-write round trip of an already-CRLF file).
  `config_resolve_default_path()` locates config.ini next to the exe's own
  file (`GetModuleFileNameA`), not via the process's current working
  directory — CWD is NOT reliably the exe's folder (confirmed: a
  Windows-startup Run key launch, see `startup.c`, put it somewhere like
  `C:\Windows\System32`, which broke config.ini discovery entirely on an
  auto-launched install before this existed). `main.c` calls it once and
  reuses the result for every `config_load`/`config_save_*` call.
- `main.c` — entry point and the only place with a real event loop. Wires
  every other module together, resolves each frame's held/pressed input
  against `AppConfig.bindings` (`binding_is_held`) with its own
  `SDL_GetTicks`-based repeat timing (not the OS's key-repeat rate) for
  navigation, and a hardcoded keyboard fallback (`ACTION_FALLBACK_SCANCODE`
  — Up/Down/Left/Right/Enter/Escape/Shift, deliberately not configurable)
  layered under whatever's actually calibrated, so a keyboard plugged into
  the cabinet always works as an escape hatch. Also drives the in-app
  calibration flow itself (press-to-bind capture for all four binding
  types, auto-starting on an uncalibrated install), the SELECT/BACK
  dispatch for launching, opening the system/platform rows, and the exit
  confirmation, and the screensaver idle timer (`last_activity_time`; see
  `AppConfig.screensaver_timeout_ms`). Runs unconditionally, including
  during calibration -- an uncalibrated cabinet sitting untouched on the
  auto-started calibration prompt still needs to blank eventually. Refreshed
  two ways: every frame from `action_is_held()` on the seven mapped actions
  (works once real bindings exist), and directly off raw `SDL_KEYDOWN`/
  `SDL_JOYBUTTONDOWN`/`SDL_JOYHATMOTION`/`SDL_JOYAXISMOTION` events in the
  poll loop (needed because during calibration there ARE no real bindings
  yet to check `action_is_held()` against, and a joystick-only cabinet --
  exactly the case calibration exists for -- has no keyboard fallback to
  fall back on either). Waking reuses `prime_edges_held()` so the waking
  press doesn't also act on the list, same reasoning as its calibration
  use -- and while the screensaver's up, the event loop swallows the
  waking event itself too, so it can't also complete a calibration step or
  fire the resolution-toggle hotkey.
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
  currently CALIBRATE CONTROLS and an ADD/REMOVE FROM STARTUP row --
  render.c overrides the latter's text every frame from `startup.h`'s live
  registry state rather than using a static label) and, right below it in
  the same unified row space, one checkbox-style toggle row per platform
  LaunchBox
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
  folder. Always passes `CREATE_NEW_PROCESS_GROUP` -- without it, a
  console-subsystem child (some emulator builds are) shares this
  process's console/process group, and a console control event generated
  when the child's console session tears down on exit can propagate back
  and silently kill the launcher too (no crash dialog, nothing for
  `SDL_Log` to catch) -- this was observed in practice as the launcher
  vanishing sometime after returning from a game, with the console window
  itself still open. Don't drop this flag without re-confirming the
  isolation is still needed.
- `xml_util.c/h` — the substring-XML helpers shared by `launchbox.c` and
  `launcher.c`.
- `starfield.c/h` — the Galaga (Namco 05XX) starfield: always the
  screensaver, and optionally the launcher background via `[display]
  background=starfield`. The background defaults to the checkerboard —
  `BACKGROUND_CHECKERBOARD` is deliberately enum value 0, and an
  unrecognized value falls back to it rather than keeping whatever was
  parsed.
  LFSR taps/hit-mask/colour bits verified against MAME's
  `starfield_05xx.cpp`; the field is 256 rows tall because the LFSR runs
  256 cycles/line for 256 lines — exactly one full period — which is what
  makes the scroll wrap seamlessly. Field *width* is the live window
  width, so the pattern is generated to fit rather than stretched, and
  stars stay 1x1 at any resolution (explicit user requirement).
  `starfield_draw()` neither clears nor presents — the caller owns the
  frame, which is what lets it serve as a background layer as well as a
  full-screen effect. It also needs no reset/activation call: the dt
  clamp absorbs any stall, so animation just continues wherever it was.
- `startup.c/h` — whether CRT Launcher runs at Windows sign-in, via a
  single `"CRT Launcher"` value under the current user's
  `HKCU\...\Run` key (per-user, no admin rights, no `.lnk`/COM). No
  config.ini involvement at all -- the registry is the only source of
  truth, queried fresh (`startup_is_enabled()`) every frame the
  ADD/REMOVE FROM STARTUP system-menu row is on screen, so it can't drift
  from a change made outside the app (Task Manager's Startup tab,
  msconfig). `startup_enable()` writes the exe's own live path
  (`GetModuleFileNameA`), so toggling off and back on after a move
  self-corrects.
- `assets/icon.ico` + `assets/app.rc.in` — the exe's icon (16-256px, all
  32bpp). `app.rc.in` is a resource-script template; CMake's
  `configure_file()` bakes in an absolute path to `icon.ico` (via
  `enable_language(RC)` + `target_sources`) so windres never has to guess
  a working directory to resolve a relative one against. Applies to both
  `build/` and `build-noconsole/` automatically, since it's wired into the
  shared `add_executable(crt_launcher ...)` target in `CMakeLists.txt`,
  not a separate step.

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
- **Concise comments (explicit user request, 2026-08-09).** Keep code
  comments as short as possible — one or two lines stating the non-obvious
  fact or constraint, no narrative history. The codebase used to carry
  long essay-style comments; they were deliberately trimmed repo-wide.
  Don't reintroduce the old style. Same for config.ini and the README:
  the README stays tidy, centered on purpose + how to build.

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
