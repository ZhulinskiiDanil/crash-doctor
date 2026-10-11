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
