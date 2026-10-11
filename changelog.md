# Changelog

## v0.1.0

- After a crash, a report over the main menu at the next start: where it crashed (mod, function, file and line
  from the mod's PDB), what most likely happened, the stack, the last actions and the log's last lines
- Diagnoses: null pointer, endless recursion (and a hook calling itself), a deleted object used, UI from another
  thread, pure virtual call, uncaught C++ exception, division by zero. No diagnosis when the signs aren't clear
- Breadcrumbs: scenes, popups, buttons and keys, kept on disk so they survive the crash
- **Open in VS Code** at the crash's line, **Copy report** for an issue or Discord (no user name in paths)
- Geode's own crashlog and window stay as they were
