#pragma once

// The layout of `session.bin`: one fixed block, mapped into memory for the whole session.
// The crash handler and the breadcrumbs only write into it, nothing else: the pages of a mapped file
// belong to Windows' file cache, so whatever is written survives the process dying on the spot.
// Plain data, no Windows or Geode in it: the analysis and the tests read it too
#include <cstddef>
#include <cstdint>

namespace session
{
  constexpr char MAGIC[8] = {'C', 'R', 'D', 'O', 'C', 'T', 'O', 'R'};
  // Bumped whenever the layout changes: a file of another version is ignored, not misread
  constexpr uint32_t VERSION = 3;

  constexpr size_t MAX_CRUMBS = 32;
  constexpr size_t CRUMB_TEXT = 120;
  constexpr size_t MAX_FRAMES = 128;
  constexpr size_t MAX_MODULES = 512;
  constexpr size_t PATH_CHARS = 260;
  constexpr size_t CODE_BYTES = 48;
  // The bytes of code before the crashing instruction in `codeBytes`
  constexpr size_t CODE_BEFORE = 16;
  constexpr size_t STACK_BYTES = 32 * 1024;
  constexpr size_t MODS_TEXT = 8 * 1024;

  enum class State : uint32_t
  {
    // The session is running, or ended without a crash the handler saw
    Running = 0,
    // The handler finished writing the crash: everything below is complete
    Crashed = 1,
  };

  // The exception Crash Doctor raises itself from `_purecall`, so the crash goes the usual way
  // (its own record, then Geode's crashlog) instead of the C runtime ending the process silently
  constexpr uint32_t PURECALL_CODE = 0xE0505643; // "PVC"

  struct Crumb
  {
    // 0 is an empty slot; otherwise the crumb's number in the session, from 1
    uint64_t sequence;
    // Milliseconds since the session started
    uint64_t millis;
    char text[CRUMB_TEXT];
  };

  struct Frame
  {
    uint64_t address;
    // Where the function containing `address` starts (from the module's unwind data), 0 if unknown.
    // Tells frames of the same function apart from frames of different ones without symbols
    uint32_t functionRva;
    // Index into `modules`, or -1 when the address isn't in a loaded module
    int16_t module;
    // Found by scanning the stack, not by unwinding: it may be a stale value
    uint8_t scanned;
    uint8_t reserved;
  };

  struct Module
  {
    uint64_t base;
    uint32_t size;
    // The PE timestamp and the PDB signature in its CodeView record: whether the file on disk at the
    // next start is still the one that crashed
    uint32_t timestamp;
    uint8_t pdbGuid[16];
    uint32_t pdbAge;
    uint16_t path[PATH_CHARS];
  };

  struct CrashRecord
  {
    uint32_t code;
    uint32_t flags;
    uint64_t address;
    uint32_t parameterCount;
    uint32_t threadId;
    uint64_t parameters[15];

    // RAX..R15 in the order of the x64 CONTEXT, then RIP and EFLAGS
    uint64_t registers[18];

    uint32_t mainThread;
    // Raised by Crash Doctor's own `_purecall` handler
    uint32_t pureCall;
    uint64_t stackBase;
    uint64_t stackLimit;

    // The bytes around RIP: `codeStart` is where `codeBytes[0]` was, `codeSize` how many could be read
    uint64_t codeStart;
    uint32_t codeSize;
    uint32_t frameCount;
    uint8_t codeBytes[CODE_BYTES];
    // The bytes right before the second frame's return address: the call that led to the top frame
    // (with a call through a broken pointer, the top frame isn't code at all)
    uint8_t callerBytes[8];
    uint32_t callerSize;
    Frame frames[MAX_FRAMES];

    // A C++ exception: the thrown type's decorated name and what() when it's a std::exception
    char cppType[128];
    char cppWhat[256];

    // The raw stack from RSP up, for what the unwinding couldn't follow
    uint64_t stackStart;
    uint32_t stackSize;
    uint32_t moduleCount;
    uint8_t stack[STACK_BYTES];
    Module modules[MAX_MODULES];
  };

  struct Header
  {
    char magic[8];
    uint32_t version;
    State state;
    // FILETIME of the session's start
    uint64_t startTime;
    uint32_t processId;
    uint32_t mainThread;
    char gameVersion[16];
    char geodeVersion[32];
    // Geode's log file of the session, UTF-8
    char logPath[PATH_CHARS * 2];
    // "id version" per line, written once the game loaded
    char mods[MODS_TEXT];
  };

  struct File
  {
    Header header;
    // A ring: the crumb with the highest sequence is the newest
    Crumb crumbs[MAX_CRUMBS];
    CrashRecord crash;
  };

  // The index of each register in `CrashRecord::registers`
  enum Register
  {
    RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15, RIP, EFLAGS
  };
} // namespace session
