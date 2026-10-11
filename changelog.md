# Changelog

## v0.3.0

Fixes from a code review, most of them against confident diagnoses that could be wrong:

- A mod is blamed only when no hook's handler is between its code and the crash: a mod that merely hooks
  something on the way (`PlayLayer::init`, `CCScheduler::update`) is no longer named for a crash in the game's code
- "A hook calls itself" needs the function's class to be the hooked class' `$modify`, not just the same method name
- "UI from another thread" needs a named cocos UI function on the stack; cocos' file, image and zip helpers are fine
  on worker threads
- "A deleted object was used" needs the table to be in a module's read-only data (a vtable), or not in any module;
  a call landing on null through a heap struct is a null pointer
- Nothing is called at the moment of a crash any more: the text of standard exceptions is read where it is
- DbgHelp's options are restored after the report, so Geode's crash handler finds them as it left them
- A mod updated since the crash gets no names from the wrong PDB (its PDB signature is checked)
- The report's guard is only up while the report is made and first drawn; a crash of another mod while the report
  is on screen is reported at the next start
- A Cyrillic user name is replaced in copied reports too; keys that may type text are never named, and typing is
  one crumb (not even a password's length is kept)
- Crash Doctor's crash filter stays in front if another mod installs its own later
- The main thread's extra stack for the handlers is 64 KB instead of 128 KB
- Improved Logger gets the last actions only once the game has loaded, twice a second at most

## v0.2.0

- **The source** around the crash's line in the report, when the file is on this computer
- **Recent crashes**: the same crash counted, not listed again
- **A short report for players**: which mod it most likely was and what to do, with the details a button away.
  The whole report shows by itself when the crashed mod's PDB is on this computer (its developer's), or always
  with the *Report detail* setting
- **The log by mod**: who logged what right before the crash, warnings and errors counted
- With Improved Logger: the last actions pinned in its console, a marker before the crash's report
- Checked with the testbed: an uncaught C++ exception (its type), a division by zero, a crash in a hook (which
  hook). The stack goes through Geode's hook handlers now, as deep as Geode's own crashlog
- Crash Doctor's own breadcrumb hooks are never blamed for a crash under them

## v0.1.0

- After a crash, a report over the main menu at the next start: where it crashed (mod, function, file and line
  from the mod's PDB), what most likely happened, the stack, the last actions and the log's last lines
- Diagnoses: null pointer, endless recursion (and a hook calling itself), a deleted object used, UI from another
  thread, pure virtual call, uncaught C++ exception, division by zero. No diagnosis when the signs aren't clear
- Breadcrumbs: scenes, popups, buttons and keys, kept on disk so they survive the crash
- **Open in VS Code** at the crash's line, **Copy report** for an issue or Discord (no user name in paths)
- Geode's own crashlog and window stay as they were
