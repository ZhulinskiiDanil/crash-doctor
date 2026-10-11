#pragma once

// The session file on disk: what the previous session left in it, and this session's mapping of it.
// Windows only, no Geode
#include "SessionFile.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace session
{
  // What the previous session wrote, when it ended in a crash the handler saw: a copy, so this
  // session can map the file again right away. Empty when there was no crash, or the file is from
  // another version of the mod or broken
  std::unique_ptr<File> takePreviousCrash(std::filesystem::path const& path);

  // Creates (or empties) the file and maps it for the rest of the session. The mapping is never
  // closed: the crash handler writes into it until the process ends
  File* open(std::filesystem::path const& path);

  // Whether a mod's own DLL (geode\unzipped\<id>\) is among the top frames of the file's crash
  bool involves(File const& file, std::string_view modId);

  // Whether a header can be trusted: the magic, the version, a size that fits
  bool isValid(File const& file);
} // namespace session
