#include "Symbols.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#include <cstddef>
#include <cstring>
#include <fstream>
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
    // A mod updated since the crash (another timestamp, another PDB): its names would point at the wrong lines
    static uint8_t const noGuid[16] = {};
    bool same = SymGetModuleInfoW64(SESSION, module.base, &info) &&
                (!module.timestamp || !info.TimeDateStamp || info.TimeDateStamp == module.timestamp) &&
                (std::memcmp(module.pdbGuid, noGuid, 16) == 0 || info.SymType != SymPdb ||
                 (std::memcmp(&info.PdbSig70, module.pdbGuid, 16) == 0 && info.PdbAge == module.pdbAge));
    if (!same)
    {
      SymUnloadModule64(SESSION, module.base);
      return false;
    }
    return true;
  }

  // The read-only data sections of a module's file (where its vtables are), from its section headers
  std::vector<std::pair<uint32_t, uint32_t>> readOnlyData(wchar_t const* path)
  {
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::ifstream file(path, std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
      return ranges;
    IMAGE_NT_HEADERS64 nt{};
    file.seekg(dos.e_lfanew);
    if (!file.read(reinterpret_cast<char*>(&nt), sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE)
      return ranges;
    file.seekg(dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader);
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections && i < 96; i++)
    {
      IMAGE_SECTION_HEADER section{};
      if (!file.read(reinterpret_cast<char*>(&section), sizeof(section)))
        break;
      auto flags = section.Characteristics;
      if ((flags & IMAGE_SCN_MEM_READ) && !(flags & IMAGE_SCN_MEM_WRITE) && !(flags & IMAGE_SCN_MEM_EXECUTE))
        ranges.push_back({section.VirtualAddress, section.VirtualAddress + section.Misc.VirtualSize});
    }
    return ranges;
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
    module.readOnlyData = readOnlyData(reinterpret_cast<wchar_t const*>(source.path));
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
  // The options are DbgHelp's for the whole process (Geode's crash handler uses them too): back as they were after
  struct Options
  {
    DWORD saved = SymGetOptions();
    ~Options() { SymSetOptions(saved); }
  } options;
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
  if (!SymInitializeW(SESSION, searchPath.c_str(), FALSE))
    return crash;
  struct Session
  {
    ~Session() { SymCleanup(SESSION); }
  } session;
  std::set<int> loaded;
  for (auto index : wanted)
    if (loadModule(record.modules[index]))
      loaded.insert(index);
  for (size_t i = 0; i < crash.frames.size() && i < named; i++)
    if (loaded.count(crash.frames[i].module))
      name(crash.frames[i], i == 0);
  // Every PDB let go when `session` goes: a mod rebuilt while the game runs can write its own
  return crash;
}
