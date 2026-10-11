# How Crash Doctor works

A map of the code and the reasons behind the parts that aren't obvious.

## The path of a crash

1. **The start** (`main.cpp`, early load): the previous session's `session.bin` is read; if it holds a complete crash, a copy is kept. Then the file is made again and mapped into memory for this session (`Session.cpp`), the breadcrumbs start writing into it (`Crumbs.cpp`) and the crash handler is installed (`Capture.cpp`).
2. **While playing**: the hooks in `Hooks.cpp` leave a crumb for each scene, popup, button and key. A crumb is one `snprintf` into a slot of the mapped file.
3. **The crash**: Windows calls the unhandled exception filter. Crash Doctor's (installed after Geode's, so it's first) writes the exception, the registers, the module list, the bytes around RIP, the raw stack and an unwound stack into the mapped file, marks it complete, and calls Geode's filter, which writes its crashlog and shows its window as always.
4. **The next start**: the record is turned into `analysis::Crash` (`Symbols.cpp`: the modules classified, the frames named from PDBs with DbgHelp), `analysis::diagnose` says what most likely happened (`Analysis.cpp`), and `ReportPopup` shows it over the main menu. The result also goes to the log as `crash-doctor-result {json}`.

## Files

| File | What it does |
| --- | --- |
| `SessionFile.hpp` | The layout of `session.bin`: header, crumbs ring, crash record. Plain data |
| `Session.cpp` | Reading the previous file, making and mapping this session's |
| `Capture.cpp` | The filter, the `_purecall` handler, the stack guarantee, everything that runs at the moment of the crash |
| `Crumbs.cpp` | Writing crumbs into the ring, reading them back in order |
| `Hooks.cpp` | The hooks that leave crumbs |
| `Decode.cpp` | Just enough x64 decoding: does a call end here, what does `call [reg+disp]` read |
| `Analysis.cpp` | The diagnoses, from the crash's data alone |
| `Symbols.cpp` | The record to `analysis::Crash`, with DbgHelp |
| `Report.cpp` | The words (Russian and English), the copied text, the log's line, scrubbing the user name |
| `ReportPopup.cpp` | The window |
| `main.cpp` | Start, the report at the next start |

`SessionFile`, `Crumbs`, `Decode`, `Analysis` and `Report` don't include Geode or Windows, so `tests/` builds them alone (CI runs them with GCC too).

## Decisions worth knowing

- **A mapped file, not writes at the crash.** The pages of a mapped file belong to Windows' file cache: whatever the process wrote is written to disk even if the process dies a moment later, killed or crashed. So the crumbs cost a memory write, and the crash handler needs no system call to save its record (it calls `FlushViewOfFile` at the end, for a crash of the whole system).
- **Nothing at the crash allocates, locks or logs.** The heap's lock may be held by the crashing thread, the loader's lock too. The module list comes from the PEB's loader list read directly, not from `CreateToolhelp32Snapshot` or `GetModuleFileName`. The function tables are looked up in each module's own exception directory, not with `RtlLookupFunctionEntry`: that one calls the callbacks Geode's hook library registers for its generated code, which can allocate. `RtlVirtualUnwind` itself only reads. Every read that might fault is in a `__try` block.
- **Code without unwind data is crossed by scanning.** A hook's handler is generated code with no module: the walk looks above RSP for the next value that points into a module's code right after a call, and marks the frame as scanned.
- **Stack overflow.** `SetThreadStackGuarantee(128 KB)` on the main thread at the start: after an overflow, the handlers (Crash Doctor's and then Geode's, which uses DbgHelp) get that much stack. The `CONTEXT` copy for the walk is static, not on the stack.
- **Geode's handler is kept.** Crash Doctor's filter is chained in front of it and always passes the crash on. Only one thread writes a record; a second thread crashing meanwhile goes straight to Geode's.
- **Pure virtual calls get an exception.** `_purecall` ends the process through `abort()`, past every filter: neither Crash Doctor nor Geode would see it. Crash Doctor's `_purecall` handler raises an exception of its own (`0xE0505643`), so both do.
- **The hooks are matched by name.** Geode doesn't expose a hook's detour address, so "this function is the mod's hook of `MenuLayer::onMoreGames`" is: the function's name (from the PDB) has the same method name as one of the mod's hooks. For a crash in a hook (not a recursion) the caller must also be generated code, a hook handler.
- **A use after free in a release game.** There's no debug heap filling freed memory. What's left is the vtable: a destructor leaves the base class' table in the object, and a virtual call past its end lands on 0 or on data. A virtual call (`call [reg+disp]` right before the return address) that landed outside code is the sign; the table's register still holds the table, so the report can tell "not a table at all" from "past the end of one".
- **Symbols at the next start, at the crash's addresses.** A DbgHelp session of its own (not the process handle Geode's handler uses) loads each module's file at the base it had in the crashed session. A module whose PE timestamp changed (a mod updated since) gets no names: they'd point at the wrong lines. Everything is unloaded right after, so a mod can be rebuilt while the game runs.
- **A crash while making or showing the report doesn't loop.** A `report-open` file is there from making the report until 3 s after it's shown. If the game starts and finds it, the crash is not reported again.
- **Keys that type text are not named.** A letter or a digit is "key (text)" unless Ctrl or Alt is held: a password typed in the game never ends up in a copied report.
- **TTF text in the window.** GD's bitmap fonts have no Cyrillic. The labels are rendered once, when the window opens.
