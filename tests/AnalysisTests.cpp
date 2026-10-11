#include "../src/Analysis.hpp"
#include "Check.hpp"

using namespace analysis;

namespace
{
  constexpr uint64_t GAME = 0x7FF600000000;
  constexpr uint64_t COCOS = 0x7FFA00000000;
  constexpr uint64_t GEODE = 0x7FFB00000000;
  constexpr uint64_t TESTBED = 0x7FFC00000000;
  constexpr uint64_t OTHER_MOD = 0x7FFC10000000;
  constexpr uint64_t SELF = 0x7FFC20000000;
  constexpr uint64_t NTDLL = 0x7FFE00000000;
  constexpr uint64_t SIZE = 0x1000000;

  enum ModuleIndex
  {
    M_GAME,
    M_COCOS,
    M_GEODE,
    M_TESTBED,
    M_OTHER,
    M_SELF,
    M_NTDLL,
  };

  Module module(char const* path, uint64_t base)
  {
    Module result;
    result.path = path;
    result.name = result.path.substr(result.path.find_last_of('\\') + 1);
    result.kind = classify(result.path, result.modId);
    result.base = base;
    result.size = SIZE;
    return result;
  }

  Crash baseCrash()
  {
    Crash crash;
    crash.modules = {
        module(R"(D:\Games\Geometry Dash\GeometryDash.exe)", GAME),
        module(R"(D:\Games\Geometry Dash\libcocos2d.dll)", COCOS),
        module(R"(D:\Games\Geometry Dash\Geode.dll)", GEODE),
        module(R"(D:\Games\Geometry Dash\geode\unzipped\zhulis.crash-doctor-testbed\zhulis.crash-doctor-testbed.dll)", TESTBED),
        module(R"(D:\Games\Geometry Dash\geode\unzipped\someone.other\someone.other.dll)", OTHER_MOD),
        module(R"(D:\Games\Geometry Dash\geode\unzipped\zhulis.crash-doctor\zhulis.crash-doctor.dll)", SELF),
        module(R"(C:\WINDOWS\SYSTEM32\ntdll.dll)", NTDLL),
    };
    return crash;
  }

  Frame frame(int module, uint64_t base, uint32_t offset, uint32_t function, char const* name = "")
  {
    Frame result;
    result.module = module;
    result.address = base + offset;
    result.functionRva = function;
    result.function = name;
    return result;
  }

  // Main thread: game -> testbed's hook -> crash
  Crash accessViolation(uint64_t address, uint64_t type = 0)
  {
    auto crash = baseCrash();
    crash.code = ACCESS_VIOLATION;
    crash.parameters = {type, address};
    crash.frames = {
        frame(M_TESTBED, TESTBED, 0x1234, 0x1200, "TestbedMenuLayer::crashNow"),
        frame(-1, 0x20000000, 0, 0),
        frame(M_GAME, GAME, 0x5000, 0x4F00),
        frame(M_NTDLL, NTDLL, 0x100, 0x80),
    };
    crash.registers[16] = crash.frames[0].address;
    return crash;
  }
} // namespace

int main()
{
  // ! --- Modules --- !

  {
    std::string id;
    CHECK(classify(R"(C:\GD\geode\unzipped\zhulis.crash-doctor-testbed\zhulis.crash-doctor-testbed.dll)", id) == ModuleKind::Mod);
    CHECK(id == "zhulis.crash-doctor-testbed");
    CHECK(classify(R"(C:\GD\geode\mods\someone.mod.dll)", id) == ModuleKind::Mod && id == "someone.mod");
    CHECK(classify(R"(C:\GD\GeometryDash.exe)", id) == ModuleKind::Game && id.empty());
    CHECK(classify(R"(C:\GD\libcocos2d.dll)", id) == ModuleKind::Cocos);
    CHECK(classify(R"(C:\GD\Geode.dll)", id) == ModuleKind::Geode);
    CHECK(classify(R"(C:\WINDOWS\System32\VCRUNTIME140.dll)", id) == ModuleKind::Runtime);
    CHECK(classify(R"(C:\WINDOWS\System32\KERNELBASE.dll)", id) == ModuleKind::System);
    CHECK(classify(R"(C:\GD\hackproldr.dll)", id) == ModuleKind::Other);
  }

  // ! --- Names --- !

  CHECK(typeName(".?AVruntime_error@std@@") == "std::runtime_error");
  CHECK(typeName(".?AUMyError@@") == "MyError");
  CHECK(typeName(".?AVparse_error@detail@json@@") == "json::detail::parse_error");
  CHECK(methodName("TestbedMenuLayer::onMoreGames") == "onMoreGames");
  CHECK(methodName("MenuLayer::init(void)") == "init");
  CHECK(methodName("free") == "free");
  CHECK(exceptionName(ACCESS_VIOLATION) == "EXCEPTION_ACCESS_VIOLATION");
  CHECK(exceptionName(0x12345) == "0x12345");

  // ! --- Null pointer --- !

  {
    auto diagnosis = diagnose(accessViolation(0x1A8));
    CHECK(diagnosis.kind == Kind::NullPointer);
    CHECK(diagnosis.culprit == "zhulis.crash-doctor-testbed");
    CHECK(diagnosis.frame == 0);
  }
  {
    // Null in the game's code called by the mod: the mod is still the one that passed it
    auto crash = accessViolation(0x10);
    crash.frames.insert(crash.frames.begin(), frame(M_COCOS, COCOS, 0x100, 0x80));
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::NullPointer);
    CHECK(diagnosis.frame == 1);
  }
  {
    // No mod on the stack: no guess
    auto crash = accessViolation(0x10);
    crash.frames.erase(crash.frames.begin());
    CHECK(diagnose(crash).kind == Kind::Unknown);
  }
  {
    // An address far from zero is not a null pointer
    CHECK(diagnose(accessViolation(0x12345678)).kind == Kind::Unknown);
  }
  {
    // In a hook: the frame's method is hooked by its mod, and the caller is a hook handler
    auto crash = accessViolation(0x8);
    crash.frames[0].function = "TestbedMenuLayer::init";
    crash.hooks["zhulis.crash-doctor-testbed"] = {"MenuLayer::init", "MenuLayer::onMoreGames"};
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::NullPointer);
    CHECK(diagnosis.hook == "MenuLayer::init");
    // Called straight from the game, not through a handler: not said to be a hook
    crash.frames[1] = frame(M_GAME, GAME, 0x10, 0x0);
    CHECK(diagnose(crash).hook.empty());
  }

  // ! --- Recursion --- !

  {
    auto crash = baseCrash();
    crash.code = STACK_OVERFLOW;
    crash.frames.push_back(frame(M_TESTBED, TESTBED, 0x2000, 0x2000, "TestbedMenuLayer::onMoreGames"));
    for (int i = 0; i < 127; i++)
      crash.frames.push_back(frame(M_TESTBED, TESTBED, 0x2040, 0x2000, "TestbedMenuLayer::onMoreGames"));
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::Recursion);
    CHECK(diagnosis.culprit == "zhulis.crash-doctor-testbed");
    // The recursive call's line, not the prolog's
    CHECK(diagnosis.frame == 1);
    CHECK(diagnosis.repeats == 128);

    crash.hooks["zhulis.crash-doctor-testbed"] = {"MenuLayer::onMoreGames"};
    diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::HookRecursion);
    CHECK(diagnosis.hook == "MenuLayer::onMoreGames");
  }
  {
    // A deep stack of different functions: a stack overflow, but not a recursion anyone can point at
    auto crash = baseCrash();
    crash.code = STACK_OVERFLOW;
    for (uint32_t i = 0; i < 40; i++)
      crash.frames.push_back(frame(M_TESTBED, TESTBED, 0x1000 + i * 0x100, 0x1000 + i * 0x100));
    CHECK(diagnose(crash).kind == Kind::Unknown);
  }
  {
    // Repeating in the game's own code: not a mod's recursion
    auto crash = baseCrash();
    crash.code = STACK_OVERFLOW;
    for (int i = 0; i < 64; i++)
      crash.frames.push_back(frame(M_GAME, GAME, 0x3040, 0x3000));
    CHECK(diagnose(crash).kind == Kind::Unknown);
  }

  // ! --- Use after free --- !

  {
    // call [rax+0x1a8] with RAX a vtable pointer nowhere near a module (text that reused the memory)
    auto crash = accessViolation(0xFFFFFFFFFFFFFFFF);
    crash.codeStart = crash.frames[0].address - 16;
    crash.codeBytes.assign(48, 0x90);
    uint8_t call[] = {0xFF, 0x90, 0xA8, 0x01, 0x00, 0x00};
    std::copy(std::begin(call), std::end(call), crash.codeBytes.begin() + 16);
    crash.registers[0] = 0x616E206C6576656C;
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::UseAfterFree);
    CHECK(diagnosis.culprit == "zhulis.crash-doctor-testbed");

    // The same call with RAX in a module (a real vtable) faulting elsewhere: no guess
    crash.registers[0] = GAME + 0x500000;
    CHECK(diagnose(crash).kind == Kind::Unknown);
  }
  {
    // Jumped to garbage through a call [rax+0x30]: RIP outside every module
    auto crash = accessViolation(0x4141414141410000, 8);
    crash.frames[0] = frame(-1, 0, 0, 0);
    crash.frames[0].address = 0x4141414141410000;
    crash.registers[16] = 0x4141414141410000;
    crash.frames.insert(crash.frames.begin() + 1, frame(M_TESTBED, TESTBED, 0x1300, 0x1200));
    crash.callerCode = {0x90, 0x90, 0x90, 0x90, 0x90, 0xFF, 0x50, 0x30};
    crash.registers[0] = 0x2000000;
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::UseAfterFree);
    CHECK(diagnosis.frame == 1);


    // Landing on 0 through a broken table is still a deleted object, not a null pointer
    crash.frames[0].address = 0;
    crash.registers[16] = 0;
    crash.parameters = {8, 0};
    CHECK(diagnose(crash).kind == Kind::UseAfterFree);
    // Through a table in a module whose slot is 0: past the end of a base class' vtable, a destroyed object
    crash.registers[0] = TESTBED + 0x8000;
    CHECK(diagnose(crash).kind == Kind::UseAfterFree);
    CHECK(diagnose(crash).evidence.find("left by a destructor") != std::string::npos);
    crash.registers[0] = 0x2000000;
    crash.registers[16] = 0x4141414141410000;
    crash.parameters = {8, 0x4141414141410000};

    // Reached by a direct call instead: not a vtable
    crash.callerCode = {0x90, 0x90, 0x90, 0xE8, 0x00, 0x10, 0x00, 0x00};
    CHECK(diagnose(crash).kind == Kind::Unknown);
  }
  {
    // The debug heap's fill of freed memory
    CHECK(diagnose(accessViolation(0xDDDDDDDDDDDDDDDD)).kind == Kind::UseAfterFree);
    CHECK(diagnose(accessViolation(0xFEEEFEEEFEEEFEEE)).kind == Kind::UseAfterFree);
  }

  // ! --- Threads --- !

  {
    // A worker thread started by the testbed, into cocos, crashing in the driver
    auto crash = baseCrash();
    crash.code = ACCESS_VIOLATION;
    crash.parameters = {0, 0x30};
    crash.mainThread = false;
    crash.frames = {
        frame(M_NTDLL, NTDLL, 0x500, 0x400),
        frame(M_COCOS, COCOS, 0x100, 0x80),
        frame(M_COCOS, COCOS, 0x200, 0x180),
        frame(M_OTHER, OTHER_MOD, 0x900, 0x800, "OtherLabel::create"),
        frame(-1, 0x30000000, 0, 0),
        frame(M_TESTBED, TESTBED, 0x4100, 0x4000, "workerThread"),
        frame(M_NTDLL, NTDLL, 0x50, 0x10),
    };
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::WrongThread);
    // The testbed's call, not the other mod's hook in between
    CHECK(diagnosis.culprit == "zhulis.crash-doctor-testbed");
    CHECK(diagnosis.frame == 5);

    // The same on the main thread is a null pointer, blamed on the topmost mod
    crash.mainThread = true;
    diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::NullPointer);
    CHECK(diagnosis.culprit == "someone.other");
  }
  {
    // A worker thread crashing in its own code, no cocos: not the UI's fault
    auto crash = accessViolation(0x18);
    crash.mainThread = false;
    CHECK(diagnose(crash).kind == Kind::NullPointer);
  }

  // ! --- Pure virtual call --- !

  {
    auto crash = baseCrash();
    crash.code = PURECALL;
    crash.pureCall = true;
    crash.frames = {
        frame(M_NTDLL, NTDLL, 0x900, 0x800),
        frame(M_SELF, SELF, 0x100, 0x80, "`anonymous namespace'::onPureCall"),
        frame(M_TESTBED, TESTBED, 0x5100, 0x5000, "Listener::notify"),
        frame(M_TESTBED, TESTBED, 0x5200, 0x5180, "Listener::~Listener"),
    };
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::PureVirtual);
    CHECK(diagnosis.frame == 2);
    CHECK(diagnosis.culprit == "zhulis.crash-doctor-testbed");
  }

  // ! --- More --- !

  {
    auto crash = accessViolation(0);
    crash.code = CPP_EXCEPTION;
    crash.parameters = {};
    crash.cppType = ".?AVout_of_range@std@@";
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::CppException);
    CHECK(diagnosis.evidence == "uncaught std::out_of_range");
  }
  {
    auto crash = accessViolation(0);
    crash.code = INT_DIVIDE_BY_ZERO;
    crash.parameters = {};
    auto diagnosis = diagnose(crash);
    CHECK(diagnosis.kind == Kind::DivideByZero);
    CHECK(diagnosis.culprit == "zhulis.crash-doctor-testbed");
  }
  {
    // Crash Doctor crashing in its own code is blamed on itself
    auto crash = accessViolation(0x20);
    crash.frames[0] = frame(M_SELF, SELF, 0x100, 0x80);
    CHECK(diagnose(crash).culprit == "zhulis.crash-doctor");
  }
  {
    // Anything else: no guess
    auto crash = accessViolation(0);
    crash.code = 0xC0000409;
    CHECK(diagnose(crash).kind == Kind::Unknown);
  }

  return finish("analysis");
}
