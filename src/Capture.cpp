#include "Capture.hpp"
#include "Decode.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstring>
#include <stdlib.h>

// Everything here may run on a broken stack, with the heap's lock held by the crashing thread, or with
// the loader's lock held. So: no allocations, no locks, no logging, no cocos, no C++ objects with
// destructors (the reads that might fault are in __try blocks, which can't have them).
// Reads of the process' own structures (the module list in the PEB, the modules' headers and unwind
// tables, the stack) are plain memory reads guarded by __try.

namespace
{
  session::File* s_file = nullptr;
  LPTOP_LEVEL_EXCEPTION_FILTER s_previous = nullptr;
  _purecall_handler s_previousPurecall = nullptr;
  // The first crashing thread writes the record; another one crashing meanwhile goes straight on
  std::atomic<uint32_t> s_capturing{0};
  std::atomic<uint32_t> s_pureCall{0};
  // The context being unwound: 1232 bytes kept off the stack, which may be nearly gone
  CONTEXT s_walk;

  // How much stack a handler of a stack overflow gets on the main thread. Geode's crash handler
  // runs after this one on the same stack, with DbgHelp: it needs room too
  constexpr ULONG STACK_GUARANTEE = 128 * 1024;
  // How far the walk looks for the next return address when unwinding can't go on
  constexpr size_t SCAN_SLOTS = 512;
  constexpr uint32_t MAX_SCANS = 16;

  // The loader's own records, only the part read here (x64 layouts, stable since Windows XP)
  struct LoaderString
  {
    USHORT length;
    USHORT maximumLength;
    PWSTR buffer;
  };

  struct LoaderEntry
  {
    LIST_ENTRY inLoadOrder;
    LIST_ENTRY inMemoryOrder;
    LIST_ENTRY inInitializationOrder;
    void* base;
    void* entryPoint;
    ULONG size;
    LoaderString fullName;
    LoaderString baseName;
  };

  struct LoaderData
  {
    ULONG length;
    BOOLEAN initialized;
    void* ssHandle;
    LIST_ENTRY inLoadOrder;
  };

  LoaderData* loaderData()
  {
    // PEB at gs:[0x60], its Ldr at +0x18
    auto peb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    return *reinterpret_cast<LoaderData**>(peb + 0x18);
  }

  IMAGE_NT_HEADERS64 const* ntHeaders(uint64_t base, uint32_t size)
  {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        static_cast<uint64_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > size)
      return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
      return nullptr;
    return nt;
  }

  // The PDB signature of a module, from its CodeView record in memory
  void readPdbSignature(session::Module& module)
  {
    __try
    {
      auto nt = ntHeaders(module.base, module.size);
      if (!nt)
        return;
      module.timestamp = nt->FileHeader.TimeDateStamp;
      auto const& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
      if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG || !directory.VirtualAddress ||
          directory.VirtualAddress + static_cast<uint64_t>(directory.Size) > module.size)
        return;
      auto entries = reinterpret_cast<IMAGE_DEBUG_DIRECTORY const*>(module.base + directory.VirtualAddress);
      for (size_t i = 0; i < directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY); i++)
      {
        auto const& entry = entries[i];
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || !entry.AddressOfRawData || entry.SizeOfData < 24 ||
            entry.AddressOfRawData + static_cast<uint64_t>(entry.SizeOfData) > module.size)
          continue;
        auto data = reinterpret_cast<uint8_t const*>(module.base + entry.AddressOfRawData);
        if (std::memcmp(data, "RSDS", 4) != 0)
          continue;
        std::memcpy(module.pdbGuid, data + 4, 16);
        std::memcpy(&module.pdbAge, data + 20, 4);
        return;
      }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
  }

  // Every module in the PEB's list. The loader's lock isn't taken: a module loading on another thread
  // right now may be missed, and a broken list ends the walk through the __except
  void captureModules(session::CrashRecord& record)
  {
    record.moduleCount = 0;
    __try
    {
      auto data = loaderData();
      auto head = &data->inLoadOrder;
      for (auto link = head->Flink; link && link != head && record.moduleCount < session::MAX_MODULES; link = link->Flink)
      {
        auto entry = CONTAINING_RECORD(link, LoaderEntry, inLoadOrder);
        if (!entry->base || !entry->size)
          continue;
        auto& module = record.modules[record.moduleCount++];
        module.base = reinterpret_cast<uint64_t>(entry->base);
        module.size = entry->size;
        size_t chars = entry->fullName.length / sizeof(wchar_t);
        if (chars >= session::PATH_CHARS)
          chars = session::PATH_CHARS - 1;
        if (entry->fullName.buffer)
          std::memcpy(module.path, entry->fullName.buffer, chars * sizeof(wchar_t));
        module.path[chars] = 0;
      }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    for (uint32_t i = 0; i < record.moduleCount; i++)
      readPdbSignature(record.modules[i]);
  }

  int findModule(session::CrashRecord const& record, uint64_t address)
  {
    for (uint32_t i = 0; i < record.moduleCount; i++)
    {
      auto const& module = record.modules[i];
      if (address >= module.base && address - module.base < module.size)
        return static_cast<int>(i);
    }
    return -1;
  }

  // The module's function table entry for `rva`, by binary search in its exception directory
  RUNTIME_FUNCTION* findFunction(session::Module const& module, uint32_t rva)
  {
    auto nt = ntHeaders(module.base, module.size);
    if (!nt || nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION)
      return nullptr;
    auto const& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!directory.VirtualAddress || directory.VirtualAddress + static_cast<uint64_t>(directory.Size) > module.size)
      return nullptr;
    auto table = reinterpret_cast<RUNTIME_FUNCTION*>(module.base + directory.VirtualAddress);
    size_t low = 0;
    size_t high = directory.Size / sizeof(RUNTIME_FUNCTION);
    while (low < high)
    {
      auto middle = (low + high) / 2;
      auto& entry = table[middle];
      if (rva < entry.BeginAddress)
        high = middle;
      else if (rva >= entry.EndAddress)
        low = middle + 1;
      else
        return &entry;
    }
    return nullptr;
  }

  // Where the function starts when the entry is a chained part of it
  uint32_t functionBegin(session::Module const& module, RUNTIME_FUNCTION const* entry)
  {
    for (int depth = 0; depth < 8; depth++)
    {
      auto info = reinterpret_cast<uint8_t const*>(module.base + entry->UnwindData);
      // UNW_FLAG_CHAININFO in the top 5 bits of the first byte
      if (!((info[0] >> 3) & UNW_FLAG_CHAININFO))
        break;
      uint8_t codes = info[2];
      entry = reinterpret_cast<RUNTIME_FUNCTION const*>(info + 4 + ((codes + 1) & ~1) * 2);
    }
    return entry->BeginAddress;
  }

  bool isExecutable(session::Module const& module, uint64_t address)
  {
    auto nt = ntHeaders(module.base, module.size);
    if (!nt)
      return false;
    auto rva = address - module.base;
    auto section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
    {
      if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
        continue;
      if (rva >= section->VirtualAddress && rva < section->VirtualAddress + static_cast<uint64_t>(section->Misc.VirtualSize))
        return true;
    }
    return false;
  }

  // A value on the stack that points into a module's code right after a call: most likely a return address
  bool looksLikeReturn(session::CrashRecord const& record, uint64_t value)
  {
    auto index = findModule(record, value);
    if (index < 0)
      return false;
    auto const& module = record.modules[index];
    if (!isExecutable(module, value) || value - module.base < 8)
      return false;
    return decode::callEndsAt(reinterpret_cast<uint8_t const*>(value), 8);
  }

  // Finds the next return address above RSP and continues the walk from it
  bool scanForReturn(session::CrashRecord const& record, CONTEXT& context)
  {
    auto slot = context.Rsp;
    for (size_t i = 0; i < SCAN_SLOTS && slot + 8 <= record.stackBase; i++, slot += 8)
    {
      auto value = *reinterpret_cast<uint64_t const*>(slot);
      if (!looksLikeReturn(record, value))
        continue;
      context.Rip = value;
      context.Rsp = slot + 8;
      return true;
    }
    return false;
  }

  // Geode's hook handlers (TulipHook's generated code, in no module) start with `push rbp; mov rbp, rsp`:
  // their caller's return address is right above the saved RBP. RBP still holds the handler's frame, as
  // the function it called restored it (or never touched it)
  bool unwindFramePointer(session::CrashRecord const& record, CONTEXT& context)
  {
    auto frame = context.Rbp;
    if (frame < context.Rsp || frame + 16 > record.stackBase || (frame & 7))
      return false;
    auto returnAddress = *reinterpret_cast<uint64_t const*>(frame + 8);
    auto savedFrame = *reinterpret_cast<uint64_t const*>(frame);
    if (!returnAddress)
      return false;
    // Back into a module: right after a call. Into more generated code (another handler): its frame further up
    if (findModule(record, returnAddress) >= 0 ? !looksLikeReturn(record, returnAddress) : savedFrame <= frame)
      return false;
    context.Rip = returnAddress;
    context.Rsp = frame + 16;
    context.Rbp = savedFrame;
    return true;
  }

  void walkStack(session::CrashRecord& record)
  {
    auto& context = s_walk;
    uint32_t scans = 0;
    bool scanned = false;
    record.frameCount = 0;
    __try
    {
      // The top frame may be at 0 (a call through a null or garbage pointer): it's recorded and unwound
      while ((context.Rip || record.frameCount == 0) && record.frameCount < session::MAX_FRAMES)
      {
        auto& frame = record.frames[record.frameCount];
        auto pc = context.Rip;
        frame.address = pc;
        frame.scanned = scanned;
        frame.module = static_cast<int16_t>(findModule(record, pc));
        frame.functionRva = 0;
        bool top = record.frameCount == 0;
        record.frameCount++;
        scanned = false;
        if (context.Rsp < record.stackLimit || context.Rsp >= record.stackBase)
          break;

        RUNTIME_FUNCTION* function = nullptr;
        if (frame.module >= 0)
        {
          auto const& module = record.modules[frame.module];
          // A return address is right after its call, which may be the function's last instruction
          auto lookup = top ? pc : pc - 1;
          function = findFunction(module, static_cast<uint32_t>(lookup - module.base));
          if (function)
            frame.functionRva = functionBegin(module, function);
        }

        if (function)
        {
          auto base = record.modules[frame.module].base;
          PVOID handlerData = nullptr;
          DWORD64 establisher = 0;
          RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, function, &context, &handlerData, &establisher, nullptr);
        }
        else if (top)
        {
          // A leaf function without unwind data, or a jump to an address that isn't code (a call
          // through a broken pointer): the return address is right at RSP
          context.Rip = *reinterpret_cast<uint64_t const*>(context.Rsp);
          context.Rsp += 8;
        }
        else if (!unwindFramePointer(record, context))
        {
          // Code without unwind data in the middle of the stack: a hook's handler, a trampoline
          if (scans++ >= MAX_SCANS || !scanForReturn(record, context))
            break;
          scanned = true;
        }
      }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
  }

  void copyBytes(void* to, uint64_t from, size_t size)
  {
    std::memcpy(to, reinterpret_cast<void const*>(from), size);
  }

  void captureCode(session::CrashRecord& record, uint64_t rip)
  {
    record.codeSize = 0;
    // From a little before RIP, or from RIP when the page before isn't readable
    uint64_t starts[] = {rip - session::CODE_BEFORE, rip};
    for (auto start : starts)
    {
      __try
      {
        copyBytes(record.codeBytes, start, session::CODE_BYTES);
        record.codeStart = start;
        record.codeSize = session::CODE_BYTES;
        return;
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {
      }
    }
  }

  void captureStack(session::CrashRecord& record, uint64_t rsp)
  {
    record.stackStart = rsp;
    record.stackSize = 0;
    if (rsp < record.stackLimit || rsp >= record.stackBase)
      return;
    auto size = record.stackBase - rsp;
    if (size > session::STACK_BYTES)
      size = session::STACK_BYTES;
    __try
    {
      copyBytes(record.stack, rsp, static_cast<size_t>(size));
      record.stackSize = static_cast<uint32_t>(size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
  }

  void captureCaller(session::CrashRecord& record)
  {
    record.callerSize = 0;
    if (record.frameCount < 2 || record.frames[1].module < 0)
      return;
    auto end = record.frames[1].address;
    auto const& module = record.modules[record.frames[1].module];
    if (end - module.base < sizeof(record.callerBytes))
      return;
    __try
    {
      copyBytes(record.callerBytes, end - sizeof(record.callerBytes), sizeof(record.callerBytes));
      record.callerSize = sizeof(record.callerBytes);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
  }

  void copyText(char* to, size_t capacity, char const* from)
  {
    size_t i = 0;
    for (; i + 1 < capacity && from[i]; i++)
      to[i] = from[i];
    to[i] = 0;
  }

  // The thrown type of a C++ exception (MSVC's: the ThrowInfo in its parameters) and, when one of the
  // types it can be caught as is std::exception, its what()
  void captureCppException(session::CrashRecord& record, EXCEPTION_RECORD const& exception)
  {
    constexpr uint32_t CPP_EXCEPTION = 0xE06D7363;
    if (exception.ExceptionCode != CPP_EXCEPTION || exception.NumberParameters < 4)
      return;
    __try
    {
      auto object = exception.ExceptionInformation[1];
      auto throwInfo = exception.ExceptionInformation[2];
      auto imageBase = exception.ExceptionInformation[3];
      if (!throwInfo || !imageBase)
        return;
      auto catchableArray = imageBase + *reinterpret_cast<int32_t const*>(throwInfo + 12);
      auto count = *reinterpret_cast<int32_t const*>(catchableArray);
      for (int32_t i = 0; i < count && i < 32; i++)
      {
        auto catchable = imageBase + *reinterpret_cast<int32_t const*>(catchableArray + 4 + i * 4);
        // CatchableType: properties, pType, then the displacement of the base in the object
        auto type = imageBase + *reinterpret_cast<int32_t const*>(catchable + 4);
        auto name = reinterpret_cast<char const*>(type + 16);
        if (i == 0)
          copyText(record.cppType, sizeof(record.cppType), name);
        if (std::strcmp(name, ".?AVexception@std@@") == 0 && object)
        {
          auto displacement = *reinterpret_cast<int32_t const*>(catchable + 8);
          auto base = object + displacement;
          // std::exception's vtable: the deleting destructor, then what()
          auto vtable = *reinterpret_cast<uint64_t const* const*>(base);
          auto what = reinterpret_cast<char const* (*)(uint64_t)>(vtable[1]);
          auto text = what(base);
          if (text)
            copyText(record.cppWhat, sizeof(record.cppWhat), text);
        }
      }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
  }

  void capture(EXCEPTION_POINTERS* info)
  {
    auto file = s_file;
    if (!file)
      return;
    auto& record = file->crash;
    auto const& exception = *info->ExceptionRecord;
    auto const& context = *info->ContextRecord;

    record.code = exception.ExceptionCode;
    record.flags = exception.ExceptionFlags;
    record.address = reinterpret_cast<uint64_t>(exception.ExceptionAddress);
    record.parameterCount = exception.NumberParameters > 15 ? 15 : exception.NumberParameters;
    for (uint32_t i = 0; i < record.parameterCount; i++)
      record.parameters[i] = exception.ExceptionInformation[i];
    record.threadId = GetCurrentThreadId();
    record.mainThread = file->header.mainThread;
    record.pureCall = s_pureCall.load();

    auto const* registers = &context.Rax;
    for (int i = 0; i < 16; i++)
      record.registers[i] = registers[i];
    record.registers[session::RIP] = context.Rip;
    record.registers[session::EFLAGS] = context.EFlags;

    // The stack's whole reserved range: StackLimit is only how far it's committed, and on a stack
    // overflow the handler runs below where it was (DeallocationStack is at TEB+0x1478 on x64)
    auto teb = reinterpret_cast<uint8_t const*>(NtCurrentTeb());
    auto tib = reinterpret_cast<NT_TIB const*>(teb);
    record.stackBase = reinterpret_cast<uint64_t>(tib->StackBase);
    record.stackLimit = *reinterpret_cast<uint64_t const*>(teb + 0x1478);
    if (!record.stackLimit || record.stackLimit >= record.stackBase)
      record.stackLimit = reinterpret_cast<uint64_t>(tib->StackLimit);

    captureModules(record);
    captureCode(record, context.Rip);
    captureStack(record, context.Rsp);
    captureCppException(record, exception);
    s_walk = context;
    walkStack(record);
    captureCaller(record);

    // Last: only a complete record counts at the next start
    std::atomic_thread_fence(std::memory_order_release);
    file->header.state = session::State::Crashed;
    FlushViewOfFile(file, 0);
  }

  LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info)
  {
    if (s_capturing.exchange(1) == 0)
    {
      __try
      {
        capture(info);
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {
      }
    }
    // Geode's handler: its crashlog and its window, exactly as without this mod
    if (s_previous)
      return s_previous(info);
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // A pure virtual call ends in the C runtime's abort(), which ends the process without an exception:
  // nothing would be written, Geode's handler included. An exception of its own goes the usual way
  void __cdecl onPureCall()
  {
    s_pureCall.store(1);
    RaiseException(session::PURECALL_CODE, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    // Not reached; if a filter ever continued it, the previous handler (or abort) gets it
    if (s_previousPurecall)
      s_previousPurecall();
    abort();
  }
} // namespace

void capture::reserveStack()
{
  ULONG size = 0;
  SetThreadStackGuarantee(&size);
  if (size < STACK_GUARANTEE)
  {
    size = STACK_GUARANTEE;
    SetThreadStackGuarantee(&size);
  }
}

void capture::install(session::File* file)
{
  s_file = file;
  reserveStack();
  s_previous = SetUnhandledExceptionFilter(onUnhandledException);
  s_previousPurecall = _set_purecall_handler(onPureCall);
}
