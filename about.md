# Crash Doctor

When the game crashes, the next start shows **what happened** in plain words, in English or Russian:

- **Which mod**, and for its developer the function, the file and line and the code around it (from the mod's PDB)
- **Most likely**: a null pointer, an endless recursion or a hook calling itself, a deleted object used, UI from
  another thread, a pure virtual call, an uncaught C++ exception, a division by zero. When the signs aren't clear,
  no guess
- **How you got there**: the last scenes, popups, buttons and keys before the crash (keys that type text aren't named)
- **The log** right before it, by mod, and the crashes before it, counted

Players get a short report: which mod it was and what to do. The whole one shows for the mod's developer (its PDB
is on their computer), or with the *Report detail* setting.

**Copy report** puts a short text for an issue or Discord on the clipboard, without your user name.
**Open in VS Code** opens the line.

Geode's own crash window and crashlog work exactly as before: Crash Doctor only adds its data and passes the crash on.
