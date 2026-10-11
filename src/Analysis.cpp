#include "Analysis.hpp"
#include "Decode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

using namespace analysis;

namespace
{
  std::string lower(std::string_view text)
  {
    std::string result(text);
    for (auto& c : result)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : result)
      if (c == '/')
        c = '\\';
    return result;
  }

  std::string hex(uint64_t value)
  {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
  }

  ModuleKind kindOf(Crash const& crash, Frame const& frame)
  {
    if (frame.module < 0 || frame.module >= static_cast<int>(crash.modules.size()))
      return ModuleKind::Unknown;
    return crash.modules[frame.module].kind;
  }

  std::string const& modOf(Crash const& crash, Frame const& frame)
  {
    static std::string const none;
    if (frame.module < 0 || frame.module >= static_cast<int>(crash.modules.size()))
      return none;
    return crash.modules[frame.module].modId;
  }

  bool isMod(Crash const& crash, Frame const& frame, bool skipSelf)
  {
    if (kindOf(crash, frame) != ModuleKind::Mod)
      return false;
    return !skipSelf || modOf(crash, frame) != crash.selfId;
  }

  // The topmost frame in a mod's code. Crash Doctor's own frames are skipped when it raised the
  // exception itself (a pure virtual call goes through its handler)
  // The topmost frame in a mod's code, when no hook's handler is between it and the crash. A mod frame below
  // a handler only called a hooked function, or passed a call on to the original: what crashed above it may be
  // the game's own code or another mod's hook, so nobody is blamed. Crash Doctor's own frames are skipped when
  // it raised the exception itself (a pure virtual call goes through its handler)
  int firstModFrame(Crash const& crash, size_t from = 0)
  {
    auto self = crash.code == PURECALL;
    for (auto i = from; i < crash.frames.size(); i++)
    {
      auto const& frame = crash.frames[i];
      if (isMod(crash, frame, self))
        return static_cast<int>(i);
      // Generated code below the top frame: a hook's handler (at the top it's where a broken call landed)
      if (i > 0 && frame.module < 0)
        return -1;
    }
    return -1;
  }

  Module const* moduleOf(Crash const& crash, uint64_t address)
  {
    for (auto const& module : crash.modules)
      if (address >= module.base && address - module.base < module.size)
        return &module;
    return nullptr;
  }

  bool inReadOnlyData(Module const& module, uint64_t address)
  {
    auto rva = address - module.base;
    for (auto const& [begin, end] : module.readOnlyData)
      if (rva >= begin && rva < end)
        return true;
    return false;
  }

  bool inModule(Crash const& crash, uint64_t address)
  {
    for (auto const& module : crash.modules)
      if (address >= module.base && address - module.base < module.size)
        return true;
    return false;
  }

  // Fill patterns of freed or never written memory (MSVC's debug heap, HeapFree with heap checks)
  bool isFreedPattern(uint64_t value)
  {
    auto low = static_cast<uint32_t>(value);
    auto high = static_cast<uint32_t>(value >> 32);
    for (uint32_t pattern : {0xDDDDDDDDu, 0xFEEEFEEEu})
      if (low == pattern && (high == pattern || high == 0))
        return true;
    return false;
  }

  struct Access
  {
    bool known = false;
    // 0 read, 1 write, 8 execute (DEP)
    uint64_t type = 0;
    uint64_t address = 0;
  };

  Access accessOf(Crash const& crash)
  {
    if (crash.code != ACCESS_VIOLATION || crash.parameters.size() < 2)
      return {};
    return {true, crash.parameters[0], crash.parameters[1]};
  }

  void setCulprit(Crash const& crash, Diagnosis& diagnosis, int frame)
  {
    diagnosis.frame = frame;
    if (frame >= 0)
      diagnosis.culprit = modOf(crash, crash.frames[frame]);
  }

  // The hook the frame is in: its function has the name of a hooked method of the same mod, and it was
  // called by generated code (a hook's handler isn't in any module)
  // The class of "ns::Class::method(args)", without its namespaces
  std::string_view className(std::string_view function)
  {
    auto end = function.find('(');
    if (end != std::string_view::npos)
      function = function.substr(0, end);
    auto colons = function.rfind("::");
    if (colons == std::string_view::npos)
      return {};
    function = function.substr(0, colons);
    colons = function.rfind("::");
    return colons == std::string_view::npos ? function : function.substr(colons + 2);
  }

  // Whether a function can be a $modify of the hooked class: its class is named after it ("MyMenuLayer" for
  // "MenuLayer::init"), or it's one Geode named itself ("hook123" for a $modify without a name)
  bool sameFamily(std::string_view function, std::string_view hook)
  {
    auto own = className(function);
    auto hooked = className(hook);
    if (own.empty() || hooked.empty())
      return false;
    if (own.find(hooked) != std::string_view::npos)
      return true;
    return own.size() > 4 && own.starts_with("hook") && own[4] >= '0' && own[4] <= '9';
  }

  std::string hookOf(Crash const& crash, int index, bool requireHandler)
  {
    if (index < 0)
      return {};
    auto const& frame = crash.frames[index];
    auto found = crash.hooks.find(modOf(crash, frame));
    if (found == crash.hooks.end() || frame.function.empty())
      return {};
    if (requireHandler)
    {
      if (index + 1 >= static_cast<int>(crash.frames.size()))
        return {};
      auto const& caller = crash.frames[index + 1];
      if (caller.module >= 0 && kindOf(crash, caller) != ModuleKind::Geode)
        return {};
    }
    auto method = methodName(frame.function);
    for (auto const& hook : found->second)
      if (!method.empty() && methodName(hook) == method && sameFamily(frame.function, hook))
        return hook;
    return {};
  }

  bool diagnoseRecursion(Crash const& crash, Diagnosis& diagnosis)
  {
    // The function (module, start) repeated most among the unwound frames
    std::map<std::pair<int, uint32_t>, int> counts;
    for (auto const& frame : crash.frames)
      if (frame.module >= 0 && frame.functionRva && !frame.scanned)
        counts[{frame.module, frame.functionRva}]++;
    std::pair<int, uint32_t> best{};
    int bestCount = 0;
    for (auto const& [key, count] : counts)
      if (count > bestCount)
      {
        best = key;
        bestCount = count;
      }
    // A stack overflow over a dozen frames of one function, filling at least half of what was unwound
    if (bestCount < 12 || bestCount * 2 < static_cast<int>(crash.frames.size()))
      return false;
    if (crash.modules[best.first].kind != ModuleKind::Mod)
      return false;

    // The line to look at is the call that recurses: the first frame of the function after the top one
    int first = -1;
    int call = -1;
    for (size_t i = 0; i < crash.frames.size(); i++)
    {
      auto const& frame = crash.frames[i];
      if (frame.module != best.first || frame.functionRva != best.second)
        continue;
      if (first < 0)
        first = static_cast<int>(i);
      else
      {
        call = static_cast<int>(i);
        break;
      }
    }
    setCulprit(crash, diagnosis, call >= 0 ? call : first);
    diagnosis.repeats = bestCount;
    diagnosis.hook = hookOf(crash, diagnosis.frame, false);
    diagnosis.kind = diagnosis.hook.empty() ? Kind::Recursion : Kind::HookRecursion;
    diagnosis.evidence = "stack overflow, " + std::to_string(bestCount) + " of " + std::to_string(crash.frames.size()) +
                         " frames are one function";
    return true;
  }

  bool isUiFunction(std::string_view function)
  {
    if (!function.starts_with("cocos2d::CC"))
      return false;
    for (std::string_view helper : {"cocos2d::CCFileUtils", "cocos2d::CCImage", "cocos2d::ZipUtils", "cocos2d::CCSAXParser",
                                    "cocos2d::CCString", "cocos2d::CCData", "cocos2d::CCConfiguration", "cocos2d::CCThread"})
      if (function.starts_with(helper))
        return false;
    return true;
  }

  // A crash below cocos (or the game's UI code) on a thread a mod runs: the mod used the UI off the main thread
  bool diagnoseThread(Crash const& crash, Diagnosis& diagnosis)
  {
    if (crash.mainThread)
      return false;
    // The mod running the thread: the deepest mod frame
    int owner = -1;
    for (int i = static_cast<int>(crash.frames.size()) - 1; i >= 0; i--)
      if (isMod(crash, crash.frames[i], true))
      {
        owner = i;
        break;
      }
    if (owner < 0)
      return false;
    // The topmost cocos frame, which must be named and be UI: cocos' file, image and zip helpers are fine on
    // any thread (image loaders decode on worker threads), and an unnamed frame proves nothing
    int ui = -1;
    for (int i = 0; i < owner; i++)
      if (kindOf(crash, crash.frames[i]) == ModuleKind::Cocos)
      {
        ui = i;
        break;
      }
    if (ui < 0 || !isUiFunction(crash.frames[ui].function))
      return false;
    // The owner's frame that called into it, past other mods' hooks in between
    auto const& ownerId = modOf(crash, crash.frames[owner]);
    int call = owner;
    for (int i = ui + 1; i <= owner; i++)
      if (modOf(crash, crash.frames[i]) == ownerId)
      {
        call = i;
        break;
      }
    setCulprit(crash, diagnosis, call);
    diagnosis.kind = Kind::WrongThread;
    auto const& module = crash.modules[crash.frames[ui].module];
    diagnosis.evidence = "not the main thread, crashed inside " + module.name + " called by " + ownerId;
    return true;
  }

  // The register of `call [reg+disp]` right before the second frame's return address, or -1
  int brokenTableRegister(Crash const& crash)
  {
    auto rip = crash.registers[16];
    if (inModule(crash, rip) || crash.callerCode.size() < 7)
      return -1;
    auto bytes = crash.callerCode.data();
    auto size = crash.callerCode.size();
    for (size_t length : {2, 3, 4, 6, 7})
    {
      auto call = decode::memoryCall(bytes + size - length, length);
      if (!call.ok || call.jump || call.length != length || call.base < 0 || call.base == 4)
        continue;
      // A live object's vtable slot always points at code. Landing outside code means the table isn't one
      // (the object's memory holds other data), or the slot is past the end of a shorter table: a base
      // class' table, which is what a destructor leaves in the object. Not claimed for a table in a module
      // outside its read-only data (a global struct of callbacks), nor for a landing on null through a
      // table in no module (a heap struct's callback left null): those are the null pointer's
      auto table = crash.registers[call.base];
      auto module = moduleOf(crash, table);
      if (module && !inReadOnlyData(*module, table))
        return -1;
      if (!module && rip < NULL_AREA)
        return -1;
      return call.base;
    }
    return -1;
  }

  bool calledThroughBrokenTable(Crash const& crash) { return brokenTableRegister(crash) >= 0; }

  bool diagnoseUseAfterFree(Crash const& crash, Diagnosis& diagnosis, Access const& access)
  {
    std::string evidence;
    auto rip = crash.registers[16];

    // Executing something that isn't code, reached by a virtual call whose table isn't in any module
    if (auto reg = brokenTableRegister(crash); reg >= 0)
    {
      auto table = crash.registers[reg];
      evidence = "a virtual call landed on " + hex(rip) + ", not code, through the vtable at " + hex(table) +
                 (inModule(crash, table) ? " (past its end: a base class' table, left by a destructor)"
                                         : " (not in any module: the object's memory holds other data)");
    }

    // A virtual call faulting on reading its vtable slot: the vtable pointer isn't in any module
    if (evidence.empty() && crash.codeStart && rip >= crash.codeStart && rip < crash.codeStart + crash.codeBytes.size())
    {
      auto offset = rip - crash.codeStart;
      auto call = decode::memoryCall(crash.codeBytes.data() + offset, crash.codeBytes.size() - offset);
      if (call.ok && call.base >= 0 && call.base != 4)
      {
        auto table = crash.registers[call.base];
        if (table >= NULL_AREA && !inModule(crash, table))
          evidence = "a virtual call through the table at " + hex(table) + ", which isn't in any module";
      }
    }

    // Freed memory's fill patterns in the faulting address
    if (evidence.empty() && access.known && isFreedPattern(access.address))
      evidence = "the address " + hex(access.address) + " is the fill pattern of freed memory";

    if (evidence.empty())
      return false;
    diagnosis.kind = Kind::UseAfterFree;
    diagnosis.evidence = evidence;
    setCulprit(crash, diagnosis, firstModFrame(crash));
    return true;
  }
} // namespace

std::string_view analysis::methodName(std::string_view function)
{
  auto end = function.find('(');
  if (end != std::string_view::npos)
    function = function.substr(0, end);
  auto colons = function.rfind("::");
  if (colons != std::string_view::npos)
    function = function.substr(colons + 2);
  return function;
}

ModuleKind analysis::classify(std::string_view path, std::string& modId)
{
  modId.clear();
  auto lowered = lower(path);
  auto slash = lowered.find_last_of('\\');
  auto name = slash == std::string::npos ? lowered : lowered.substr(slash + 1);

  for (std::string_view marker : {"\\geode\\unzipped\\", "\\geode\\mods\\"})
  {
    auto at = lowered.find(marker);
    if (at == std::string::npos)
      continue;
    auto start = at + marker.size();
    auto end = lowered.find('\\', start);
    // Unpacked: the folder is the mod's ID. Loaded from the mods folder: the file name is
    if (end != std::string::npos)
      modId = std::string(path.substr(start, end - start));
    else if (name.size() > 4)
      modId = std::string(path.substr(start, path.size() - start - 4));
    return ModuleKind::Mod;
  }
  if (name == "geometrydash.exe")
    return ModuleKind::Game;
  if (name == "libcocos2d.dll" || name == "libextensions.dll")
    return ModuleKind::Cocos;
  if (name == "geode.dll")
    return ModuleKind::Geode;
  if (name.starts_with("vcruntime") || name.starts_with("msvcp") || name == "ucrtbase.dll" ||
      name.starts_with("api-ms-win-crt"))
    return ModuleKind::Runtime;
  if (lowered.find("\\windows\\") != std::string::npos)
    return ModuleKind::System;
  return ModuleKind::Other;
}

std::string analysis::typeName(std::string_view decorated)
{
  // ".?AV" class, ".?AU" struct, then names from the innermost, each ending with '@', the list with "@@"
  if (decorated.size() < 6 || !decorated.starts_with(".?A"))
    return std::string(decorated);
  auto body = decorated.substr(4);
  auto end = body.find("@@");
  if (end != std::string_view::npos)
    body = body.substr(0, end);
  std::vector<std::string_view> parts;
  while (!body.empty())
  {
    auto at = body.find('@');
    parts.push_back(body.substr(0, at));
    if (at == std::string_view::npos)
      break;
    body = body.substr(at + 1);
  }
  std::string result;
  for (auto part = parts.rbegin(); part != parts.rend(); part++)
  {
    if (!result.empty())
      result += "::";
    result += *part;
  }
  return result;
}

std::string analysis::exceptionName(uint32_t code)
{
  switch (code)
  {
  case ACCESS_VIOLATION:
    return "EXCEPTION_ACCESS_VIOLATION";
  case STACK_OVERFLOW:
    return "EXCEPTION_STACK_OVERFLOW";
  case INT_DIVIDE_BY_ZERO:
    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
  case 0xC000001D:
    return "EXCEPTION_ILLEGAL_INSTRUCTION";
  case 0x80000003:
    return "EXCEPTION_BREAKPOINT";
  case 0xC0000409:
    return "STATUS_STACK_BUFFER_OVERRUN";
  case 0xC0000374:
    return "STATUS_HEAP_CORRUPTION";
  case CPP_EXCEPTION:
    return "C++ exception";
  case PURECALL:
    return "pure virtual call";
  default:
    return hex(code);
  }
}

Diagnosis analysis::diagnose(Crash const& crash)
{
  Diagnosis diagnosis;
  auto access = accessOf(crash);

  if (crash.code == PURECALL)
  {
    diagnosis.kind = Kind::PureVirtual;
    diagnosis.evidence = "the C runtime's _purecall was called";
    setCulprit(crash, diagnosis, firstModFrame(crash));
    return diagnosis;
  }

  if (crash.code == STACK_OVERFLOW)
  {
    if (diagnoseRecursion(crash, diagnosis))
      return diagnosis;
    return {};
  }

  if (crash.code == CPP_EXCEPTION)
  {
    diagnosis.kind = Kind::CppException;
    diagnosis.evidence = crash.cppType.empty() ? "a C++ exception nothing caught" : "uncaught " + typeName(crash.cppType);
    setCulprit(crash, diagnosis, firstModFrame(crash));
    diagnosis.hook = hookOf(crash, diagnosis.frame, true);
    return diagnosis;
  }

  if (crash.code == INT_DIVIDE_BY_ZERO)
  {
    diagnosis.kind = Kind::DivideByZero;
    diagnosis.evidence = "integer division by zero";
    setCulprit(crash, diagnosis, firstModFrame(crash));
    // The division is the top frame's: when that isn't a mod's, the mod below only passed something on
    if (diagnosis.frame != 0)
      setCulprit(crash, diagnosis, -1);
    diagnosis.hook = hookOf(crash, diagnosis.frame, true);
    return diagnosis;
  }

  if (crash.code != ACCESS_VIOLATION || !access.known)
    return {};

  if (diagnoseThread(crash, diagnosis))
    return diagnosis;

  // A virtual call landing on 0 or garbage through a table that isn't one: a deleted object, not a null
  // pointer, even when it lands on 0
  if (calledThroughBrokenTable(crash) && diagnoseUseAfterFree(crash, diagnosis, access))
  {
    diagnosis.hook = hookOf(crash, diagnosis.frame, true);
    return diagnosis;
  }

  if (access.address < NULL_AREA)
  {
    auto frame = firstModFrame(crash);
    // A null pointer in the game's code with no mod on the stack isn't a mod's: no guess
    if (frame < 0)
      return {};
    diagnosis.kind = Kind::NullPointer;
    static char const* const verbs[] = {"read", "write", "execute"};
    auto verb = access.type == 0 ? verbs[0] : access.type == 1 ? verbs[1] : verbs[2];
    diagnosis.evidence = std::string(verb) + " at " + hex(access.address) + ", next to address 0";
    setCulprit(crash, diagnosis, frame);
    diagnosis.hook = hookOf(crash, diagnosis.frame, true);
    return diagnosis;
  }

  if (diagnoseUseAfterFree(crash, diagnosis, access))
  {
    diagnosis.hook = hookOf(crash, diagnosis.frame, true);
    return diagnosis;
  }
  return {};
}
