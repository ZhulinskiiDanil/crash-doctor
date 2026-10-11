# Changelog

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
