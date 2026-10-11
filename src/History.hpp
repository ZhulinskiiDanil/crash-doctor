#pragma once

// The crashes of the last sessions, the same ones grouped with a count. Kept as lines of text in the
// mod's save folder; no Windows or Geode in here
#include <cstdint>
#include <string>
#include <vector>

namespace history
{
  struct Entry
  {
    // Seconds since 1970 of the last time it happened
    int64_t time = 0;
    // What makes two crashes the same one: the diagnosis, the mod and where
    std::string signature;
    std::string kind;
    std::string culprit;
    // "main.cpp:42" or "module+0x1234"
    std::string where;
    int count = 1;
  };

  // How many different crashes are kept
  constexpr size_t MAX_ENTRIES = 20;

  // Adds a crash: the same signature counts once more and moves to the top, a new one goes on top.
  // Newest first, at most MAX_ENTRIES
  void add(std::vector<Entry>& entries, Entry entry);

  // The file's text and back. Lines that don't parse are skipped: a damaged file loses only them
  std::string serialize(std::vector<Entry> const& entries);
  std::vector<Entry> parse(std::string const& text);
} // namespace history
