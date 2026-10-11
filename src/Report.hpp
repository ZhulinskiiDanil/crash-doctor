#pragma once

// The report's words: the diagnosis in plain language (Russian and English), the stack as lines, the
// text "Copy report" puts on the clipboard and the line for the log. No Windows, no Geode
#include "Analysis.hpp"
#include "Crumbs.hpp"
#include "History.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace report
{
  enum class Language
  {
    English,
    Russian,
  };

  struct ModVersion
  {
    std::string id;
    std::string name;
    std::string version;
  };

  struct SourceLine
  {
    int number = 0;
    std::string text;
  };

  // Everything a report shows, gathered at the start after a crash
  struct Data
  {
    analysis::Crash crash;
    analysis::Diagnosis diagnosis;
    std::vector<crumbs::Crumb> crumbs;
    // The last lines of the crashed session's log
    std::vector<std::string> logTail;
    std::string gameVersion;
    std::string geodeVersion;
    // The mods of the crashed session
    std::vector<ModVersion> mods;
    // Geode's crashlog of this crash, empty when there's none
    std::string crashlog;
    // Seconds from the session's start to the last crumb
    double sessionSeconds = 0;
    // The source around the line the diagnosis points at, when the file is on this computer
    std::vector<SourceLine> source;
    // The recent crashes, this one first, with how many times each happened
    std::vector<history::Entry> history;
  };

  struct Words
  {
    std::string title;
    std::string explanation;
    std::string advice;
  };

  // The diagnosis as a title, what it means and what to do
  Words describe(Data const& data, Language language);

  // The same for a player, not a developer: which mod it most likely was and what to do about it
  Words describeForPlayer(Data const& data, Language language);

  // Whether the culprit's frame has a file and line: its mod was built with a PDB that's on this computer
  bool hasSymbols(Data const& data);

  // "zhulis.crash-doctor-testbed.dll!TestbedMenuLayer::crashNull + 0x1c (main.cpp:42)"
  std::string frameLine(Data const& data, analysis::Frame const& frame);

  // The culprit's name and version, "Crash Doctor Testbed v1.0.0 (zhulis.crash-doctor-testbed)"
  std::string culpritLine(Data const& data);

  // The file and line the diagnosis points at, "D:\...\main.cpp:42", or empty
  std::string location(Data const& data);

  // A link VS Code opens at the line: vscode://file/D:/.../main.cpp:42
  std::string vscodeLink(std::string_view file, int line);

  // The short text for an issue or Discord: diagnosis, culprit, stack, crumbs, versions
  std::string copyText(Data const& data, Language language);

  // The user's profile folder in paths, "C:\Users\<name>", becomes %USERPROFILE%, in either slash and any case
  std::string scrub(std::string text, std::string_view profile);

  // One line for the log, a JSON object: kind, culprit, file, line, function, thread, crashlog
  std::string resultLine(Data const& data);

  // The diagnosis' key in the result line: "null-pointer", "recursion", ...
  std::string_view kindKey(analysis::Kind kind);

  // The lines `radius` around `line` of a source file, empty when it can't be read or is too short
  std::vector<SourceLine> readSource(std::string const& path, int line, int radius = 3);

  // The source as "  137 | code" lines, the crash's line marked with ">"
  std::vector<std::string> sourceLines(Data const& data);

  // The history's entry for this crash: what makes it the same as another
  history::Entry historyEntry(Data const& data, int64_t time);

  // The crumbs as "12.3 s  button ..." lines, newest last, at most `count`
  std::vector<std::string> crumbLines(Data const& data, size_t count);
} // namespace report
