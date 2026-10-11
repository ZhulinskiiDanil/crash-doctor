#include "Session.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstring>
#include <cwctype>
#include <string>
#include <fstream>

bool session::isValid(File const& file)
{
  return std::memcmp(file.header.magic, MAGIC, sizeof(MAGIC)) == 0 && file.header.version == VERSION &&
         file.crash.frameCount <= MAX_FRAMES && file.crash.moduleCount <= MAX_MODULES &&
         file.crash.stackSize <= STACK_BYTES && file.crash.codeSize <= CODE_BYTES &&
         file.crash.parameterCount <= 15 && file.crash.callerSize <= sizeof(file.crash.callerBytes);
}

bool session::involves(File const& file, std::string_view modId)
{
  // "\<id>\" in the module's path, case aside (the unpacked folder is the ID)
  std::wstring needle = L"\\";
  for (auto c : modId)
    needle += static_cast<wchar_t>(std::towlower(static_cast<unsigned char>(c)));
  needle += L"\\";
  auto const& crash = file.crash;
  for (uint32_t i = 0; i < crash.frameCount && i < 24; i++)
  {
    auto index = crash.frames[i].module;
    if (index < 0 || static_cast<uint32_t>(index) >= crash.moduleCount)
      continue;
    std::wstring path(reinterpret_cast<wchar_t const*>(crash.modules[index].path),
                      wcsnlen(reinterpret_cast<wchar_t const*>(crash.modules[index].path), PATH_CHARS));
    for (auto& c : path)
      c = static_cast<wchar_t>(std::towlower(c));
    if (path.find(needle) != std::wstring::npos)
      return true;
  }
  return false;
}

std::unique_ptr<session::File> session::takePreviousCrash(std::filesystem::path const& path)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return nullptr;
  auto file = std::make_unique<File>();
  stream.read(reinterpret_cast<char*>(file.get()), sizeof(File));
  if (stream.gcount() != static_cast<std::streamsize>(sizeof(File)))
    return nullptr;
  if (!isValid(*file) || file->header.state != State::Crashed)
    return nullptr;
  // The texts are read as C strings later: whatever was written, they end
  file->header.gameVersion[sizeof(file->header.gameVersion) - 1] = 0;
  file->header.geodeVersion[sizeof(file->header.geodeVersion) - 1] = 0;
  file->header.logPath[sizeof(file->header.logPath) - 1] = 0;
  file->header.mods[sizeof(file->header.mods) - 1] = 0;
  file->crash.cppType[sizeof(file->crash.cppType) - 1] = 0;
  file->crash.cppWhat[sizeof(file->crash.cppWhat) - 1] = 0;
  for (auto& crumb : file->crumbs)
    crumb.text[CRUMB_TEXT - 1] = 0;
  for (auto& module : file->crash.modules)
    module.path[PATH_CHARS - 1] = 0;
  return file;
}

session::File* session::open(std::filesystem::path const& path)
{
  auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return nullptr;
  auto mapping = CreateFileMappingW(handle, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(File)), nullptr);
  // The mapping keeps the file open on its own
  CloseHandle(handle);
  if (!mapping)
    return nullptr;
  auto view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(File));
  CloseHandle(mapping);
  if (!view)
    return nullptr;

  // A new file is zeros already: only the header is filled
  auto file = static_cast<File*>(view);
  std::memcpy(file->header.magic, MAGIC, sizeof(MAGIC));
  file->header.version = VERSION;
  file->header.state = State::Running;
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  file->header.startTime = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  file->header.processId = GetCurrentProcessId();
  file->header.mainThread = GetCurrentThreadId();
  return file;
}
