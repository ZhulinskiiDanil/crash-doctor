# NOTES: Crash Doctor, the autonomous run

Start: **Sun Oct 11 14:44:25 2026** (the first `date` of the run). Budget: 8 hours, until 22:44.

## Log

- 14:44 start. Read TASK.md, the previous run's NOTES (mod-profiler), the style of improved-geode-logger and
  mod-profiler, the registry of zhulis-mods-manager.
- 14:55 read Geode's crash handler (`loader/src/platform/windows/crashlog.cpp`): one `SetUnhandledExceptionFilter`,
  nothing for `_purecall` or `std::terminate`, a modal window on the crashing thread, `last-crashed` file and a
  "Crashed" popup in MenuLayer. Design: chain in front of it, write into a mapped file, analyze next start.
- 15:02 the analysis, decoder, crumbs and report text with tests (4 suites, Geode-free).
- 15:20 first `geode build` of the mod and the testbed, `testbed/check.sh` (flag file, crash, wait for Geode's
  crashlog, restart, wait for the result line, screenshot).
- 15:24 null pointer: right on the first run. The window didn't show: MenuLayer::init runs before
  `$on_game(Loaded)`, where the report was made. Fixed: made at whichever comes first.
- 15:27 recursion in a hook: right on the first run (`hook-recursion`, `MenuLayer::onMoreGames`, line 138).
- 15:28 use after free: "unknown" with 0 frames. The call landed on 0x0 and the walk stopped at RIP 0. Fixed the
  walk; and the testbed's attempt to refill the freed memory never worked (cocos' heap isn't the testbed's): the
  object kept the base class' vtable (`CCObject`'s, left by the destructor) and the call read past its end. New
  rule: a virtual call (`call [reg+disp]` before the return address) that lands outside code. 15:30 right.
- 15:31-15:43 the thread crash took four tries to make the testbed crash at all: `CCLabelBMFont::create` off the
  main thread didn't crash (NVIDIA's driver ignores GL calls without a context), Geode gives `CCSprite::create` a
  placeholder for a missing file, GD's `CCArray::objectAtIndex` checks the index, and the menu's scheduler was
  paused by the settings layer the testbed opens (the crash is timed from a thread now). Final: the thread adds
  a node it didn't find (`getChildByID` returned null) to the scene, the crash is inside `CCNode::addChild` in
  libcocos2d. Right on the first run of it (`wrong-thread`, line 76). Geode's crashlog says "Faulty Mod: <Unknown>"
  for it; Crash Doctor names the testbed.
- 15:44 pure virtual call: the diagnosis right, the line was the `delete` (MSVC made `onEvent()` a tail jump, so
  `notify`'s frame was gone). The testbed's `notify` does something after the call now, like real code. 15:45 right.
- 15:46 a start without a crash: no result line, no window, the game runs.
- 15:52 v0.1.0 released (pre-release, `.geode` from CI), added to Zhulis Mods Manager: `mods.json` (commit
  368a6f9) and its built-in list (e2a48a4), two commits so either can be reverted alone.
- 15:58 backlog 2 (the source around the line) and 1 (history: the same crash grouped and counted).
- 16:00 backlog 4: an uncaught C++ exception (`std::invalid_argument` from `std::stoi`), a division by zero and a
  null pointer in a hook reached through the game's button: all right on the first run. The hook one had only 3
  frames: the walk scanned through TulipHook's handlers badly. Now a frame outside every module is unwound by its
  frame pointer first (the handlers start with `push rbp; mov rbp, rsp`): 25 frames, the same as Geode's crashlog.
  That stack showed Crash Doctor's own breadcrumb hook (`CrumbMenuItem::activate`) under every button: a frame of
  Crash Doctor that called on through a hook handler is never the culprit now.
- 16:05 backlog 5: the short report for players (*Report detail*: Auto, Always detailed, Always short). Checked
  with *Always short*: "Игра упала из-за мода Crash Doctor Testbed", what to do, the last actions, **Details**.
- 16:08 backlog 3: the log by mod in the report (works without Improved Logger), and with Improved Logger the last
  actions pinned in its console (`watch`) and a marker before the report. Checked: "Improved Logger is here".
- 16:12 the five kinds again on the new code: all right (diagnosis, testbed, line, Geode's crashlog, no crash).
- 16:18 v0.2.0 released (pre-release). Zhulis Mods Manager fetched Crash Doctor's releases (its cache has
  `ZhulinskiiDanil_crash-doctor.json`).
- 16:19 the cost in a normal session (backlog 7): Mod Profiler's "game's start" line doesn't list Crash Doctor
  among the mods over 0.07 s; in a start that made a report it was 0.25 s (DbgHelp, on the main thread, once after
  a crash). Building the window is one slow frame of 60-75 ms (TTF labels rendered once).
- 16:20 backlog 6: README in English and Russian with screenshots, about.md. **Copy report** checked with a real click
  and the clipboard read back: the diagnosis, the line, the stack, the code, the crumbs, versions, no user name.
- 16:24 the report's guard checked: a crash, then the testbed crashing while the report is on screen, then a start.
- 16:25 a code review by a separate reviewer agent (backlog 7), read-only. 1 critical, 10 important, 8 minor; all
  fixed except where noted below:
  - critical: any mod frame anywhere on the stack was blamed, e.g. a mod hooking `PlayLayer::init` for a crash in
    the game's own code. Now a mod is blamed only when no hook handler is between it and the crash.
  - hook recursion matched the method name alone: now the class must be the hooked one's family (`MyMenuLayer` for
    `MenuLayer`, or Geode's own `hookNN` name).
  - "UI from another thread" fired for any cocos frame: now the cocos frame must be named and be UI (not
    `CCFileUtils`, `CCImage`, `ZipUtils`...: image loaders decode on worker threads legitimately).
  - use after free: a call landing on 0 through a table in no module is a heap struct's null callback, so it's the
    null pointer's now; a table in a module must be in its read-only data (read from the file's section headers).
  - `what()` was called at the crash: now nothing is called, the standard exceptions' text is read where it is.
  - the pure call flag could stay set if caught: now only the exception code counts.
  - DbgHelp's process-wide options are restored after the report; PDB signatures compared, not only timestamps.
  - the guard stayed up for the whole session if the menu left before 3 s: now it's only up while the report is
    built and while its window is made and first drawn. Also refined after the live check: only a crash with
    Crash Doctor's own DLL on its stack is skipped; another mod's crash during the report is reported.
  - `callerSize` not validated in a damaged file; the user profile read in the ANSI code page (a Cyrillic user name
    wasn't scrubbed): both fixed, with a test.
  - keys: only keys that never type text are named (F-keys, arrows, Esc...), anything else only with Ctrl and not
    with Ctrl+Alt (AltGr); typing is one "typing" crumb, not counted (no password length).
  - minor ones fixed: a second crashing thread waits up to 2 s for the first one's record; a cap on the module
    list walk; `tellg` failure. The stack guarantee went from 128 to 64 KB (GD's main thread has 1 MB): checked
    that Geode's crashlog of the hook recursion is still complete (20 233 frames and the mods list).
- 16:36 and 16:45 two starts (of ~50 in the checks) hung while the game loaded: the log stops after "Entry took",
  Mod Profiler's line with ~450 scanned samples is the last. Not reproduced in 10 plain starts (0 of 10). It's at
  the moment `LoadingLayer` is pushed, which was Crash Doctor's first crumb and its first `watch` into Improved
  Logger. Not proven to be Crash Doctor's, but now nothing goes to Improved Logger before the game has loaded, and
  then only from the scheduler, twice a second at most. Also: Crash Doctor now checks every 2 s that its crash
  filter is still in front (another mod's could replace it); none did in the checks.
- 16:55 all 8 testbed kinds right again after the review fixes.
