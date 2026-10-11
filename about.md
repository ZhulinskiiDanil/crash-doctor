# Crash Doctor

When the game crashes, the next start shows **what happened** in plain words:

- **Where**: the mod, the function, and the file and line when the mod has its PDB
- **Most likely**: a null pointer, an endless recursion, a deleted object used, UI from another thread, a pure
  virtual call, an uncaught C++ exception, a division by zero. When the signs aren't clear, no guess
- **How you got there**: the last scenes, popups, buttons and keys before the crash
- The last lines of the log

**Copy report** puts a short text for an issue or Discord on the clipboard, **Open in VS Code** opens the line.

Geode's own crash window and crashlog work exactly as before: Crash Doctor only adds its data and passes the crash on.
