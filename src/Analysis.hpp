#pragma once

// What most likely happened, from the crash's data alone. No Windows, no Geode: the tests feed it
// made-up crashes. A diagnosis is given only on signs that hold up; otherwise the kind is Unknown and
// the report shows the facts without a guess
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace analysis
{
  enum class ModuleKind
  {
    Unknown,
    // GeometryDash.exe
    Game,
    // libcocos2d.dll, libExtensions.dll
    Cocos,
    // Geode.dll
    Geode,
    // A mod's DLL, unpacked into geode\unzipped\<id>\ (or loaded from geode\mods)
    Mod,
    // The C and C++ runtimes
    Runtime,
    // Windows' own DLLs and drivers
    System,
    Other,
  };

  struct Module
  {
    std::string path;
    std::string name;
    ModuleKind kind = ModuleKind::Unknown;
    std::string modId;
    uint64_t base = 0;
    uint32_t size = 0;
  };

  struct Frame
  {
    uint64_t address = 0;
    // Index into Crash::modules, -1 outside every module (generated code, a broken pointer)
    int module = -1;
    uint32_t functionRva = 0;
    bool scanned = false;
    // From the PDB, when there is one
    std::string function;
    std::string file;
    int line = 0;
  };

  struct Crash
  {
    uint32_t code = 0;
    uint64_t address = 0;
    std::vector<uint64_t> parameters;
    bool mainThread = true;
    bool pureCall = false;
    uint64_t registers[18] = {};
    // The bytes around RIP, from codeStart
    uint64_t codeStart = 0;
    std::vector<uint8_t> codeBytes;
    std::vector<uint8_t> callerCode;
    std::vector<Module> modules;
    std::vector<Frame> frames;
    std::string cppType;
    std::string cppWhat;
    // The hooks of each mod, by mod ID, as Geode names them ("MenuLayer::init")
    std::map<std::string, std::vector<std::string>> hooks;
    // Crash Doctor's own ID: its frames are the handler's, not a culprit (unless it crashed itself)
    std::string selfId = "zhulis.crash-doctor";
  };

  enum class Kind
  {
    Unknown,
    NullPointer,
    Recursion,
    // A recursion through a mod's own hook: it called itself instead of the original
    HookRecursion,
    UseAfterFree,
    WrongThread,
    PureVirtual,
    CppException,
    DivideByZero,
  };

  struct Diagnosis
  {
    Kind kind = Kind::Unknown;
    // The frame that points at the mod's code (the line to look at), -1 when no mod is on the stack
    int frame = -1;
    std::string culprit;
    // The hook the culprit's frame is in, when it's one ("MenuLayer::init")
    std::string hook;
    // How many frames of the stack are the repeated function, for a recursion
    int repeats = 0;
    // What the diagnosis rests on, a short line for the report and the log
    std::string evidence;
  };

  Diagnosis diagnose(Crash const& crash);

  // What a module is, from its path. `modId` is set for a mod
  ModuleKind classify(std::string_view path, std::string& modId);

  // ".?AVruntime_error@std@@" -> "std::runtime_error"
  std::string typeName(std::string_view decorated);

  // "EXCEPTION_ACCESS_VIOLATION" and the like, or the code in hex
  std::string exceptionName(uint32_t code);

  // "Class::method" -> "method"; the name without arguments
  std::string_view methodName(std::string_view function);

  constexpr uint32_t ACCESS_VIOLATION = 0xC0000005;
  constexpr uint32_t STACK_OVERFLOW = 0xC00000FD;
  constexpr uint32_t INT_DIVIDE_BY_ZERO = 0xC0000094;
  constexpr uint32_t CPP_EXCEPTION = 0xE06D7363;
  // Mirrors session::PURECALL_CODE (the analysis doesn't include the session file)
  constexpr uint32_t PURECALL = 0xE0505643;
  // Windows never maps the first 64 KB: an address below it is a null pointer plus an offset
  constexpr uint64_t NULL_AREA = 0x10000;
} // namespace analysis
