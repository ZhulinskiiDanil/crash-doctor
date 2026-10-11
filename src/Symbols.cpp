#include "Symbols.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#include <cstring>
#include <set>

namespace
{
  // Not the real process handle: Geode's crash handler calls SymInitialize on that one, and Mod Profiler
  // has its own. A session of this mod's own, loading modules from their files at the crash's addresses
  HANDLE const SESSION = reinterpret_cast<HANDLE>(0x4352'444F);

  std::string narrowWide(wchar_t const* text)
  {
    auto size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
      return {};
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
  }

  std::wstring folderOf(std::wstring const& path) { return path.substr(0, path.find_last_of(L"\\/")); }

  // Loads a module into the session at the base it had, when its file is still the one that crashed
  bool loadModule(session::Module const& module)
  {
    auto path = reinterpret_cast<wchar_t const*>(module.path);
    if (!SymLoadModuleExW(SESSION, nullptr, path, nullptr, module.base, module.size, nullptr, 0))
      return false;
    IMAGEHLP_MODULEW64 info{};
    info.SizeOfStruct = sizeof(info);
    if (!SymGetModuleInfoW64(SESSION, module.base, &info))
      return true;
    // A mod updated since the crash: its names would point at the wrong lines
    if (module.timestamp && info.TimeDateStamp && info.TimeDateStamp != module.timestamp)
    {
      SymUnloadModule64(SESSION, module.base);
      return false;
    }
    return true;
  }

  void name(analysis::Frame& frame, bool top)
  {
    // A return address is right after its call, which may be on the next line or the next function
    auto address = top ? frame.address : frame.address - 1;

    alignas(SYMBOL_INFOW) char buffer[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(wchar_t)];
    auto symbol = reinterpret_cast<SYMBOL_INFOW*>(buffer);
    std::memset(symbol, 0, sizeof(SYMBOL_INFOW));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (SymFromAddrW(SESSION, address, &displacement, symbol))
      frame.function = narrowWide(symbol->Name);

    IMAGEHLP_LINEW64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddrW64(SESSION, address, &lineDisplacement, &line) && line.FileName)
    {
      frame.file = narrowWide(line.FileName);
      frame.line = static_cast<int>(line.LineNumber);
    }
  }
} // namespace

std::string symbols::narrow(uint16_t const* text, size_t capacity)
{
  std::wstring wide(reinterpret_cast<wchar_t const*>(text), wcsnlen(reinterpret_cast<wchar_t const*>(text), capacity));
  return narrowWide(wide.c_str());
}

analysis::Crash symbols::load(session::CrashRecord const& record, size_t named)
{
  analysis::Crash crash;
  crash.code = record.code;
  crash.address = record.address;
  crash.parameters.assign(record.parameters, record.parameters + record.parameterCount);
  crash.mainThread = record.threadId == record.mainThread;
  crash.pureCall = record.pureCall != 0;
  std::memcpy(crash.registers, record.registers, sizeof(crash.registers));
  crash.codeStart = record.codeStart;
  crash.codeBytes.assign(record.codeBytes, record.codeBytes + record.codeSize);
  crash.callerCode.assign(record.callerBytes, record.callerBytes + record.callerSize);
  crash.cppType = std::string(record.cppType, strnlen(record.cppType, sizeof(record.cppType)));
  crash.cppWhat = std::string(record.cppWhat, strnlen(record.cppWhat, sizeof(record.cppWhat)));

  for (uint32_t i = 0; i < record.moduleCount; i++)
  {
    auto const& source = record.modules[i];
    analysis::Module module;
    module.path = narrow(source.path, session::PATH_CHARS);
    module.name = module.path.substr(module.path.find_last_of("\\/") + 1);
    module.kind = analysis::classify(module.path, module.modId);
    module.base = source.base;
    module.size = source.size;
    crash.modules.push_back(std::move(module));
  }
  for (uint32_t i = 0; i < record.frameCount; i++)
  {
    auto const& source = record.frames[i];
    analysis::Frame frame;
    frame.address = source.address;
    frame.module = source.module < static_cast<int>(record.moduleCount) ? source.module : -1;
    frame.functionRva = source.functionRva;
    frame.scanned = source.scanned != 0;
    crash.frames.push_back(std::move(frame));
  }

  // DbgHelp: the modules of the frames that get names, nothing else
  std::set<int> wanted;
  for (size_t i = 0; i < crash.frames.size() && i < named; i++)
    if (crash.frames[i].module >= 0)
      wanted.insert(crash.frames[i].module);
  if (wanted.empty())
    return crash;

  std::wstring searchPath;
  for (auto index : wanted)
  {
    if (!searchPath.empty())
      searchPath += L';';
    searchPath += folderOf(reinterpret_cast<wchar_t const*>(record.modules[index].path));
  }
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
  if (!SymInitializeW(SESSION, searchPath.c_str(), FALSE))
    return crash;
  std::set<int> loaded;
  for (auto index : wanted)
    if (loadModule(record.modules[index]))
      loaded.insert(index);
  for (size_t i = 0; i < crash.frames.size() && i < named; i++)
    if (loaded.count(crash.frames[i].module))
      name(crash.frames[i], i == 0);
  // Every PDB let go: a mod rebuilt while the game runs can write its own
  SymCleanup(SESSION);
  return crash;
}
