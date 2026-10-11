#pragma once

// The report's words: the diagnosis in plain language (Russian and English), the stack as lines, the
// text "Copy report" puts on the clipboard and the line for the log. No Windows, no Geode
#include "Analysis.hpp"
#include "Crumbs.hpp"

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
  };

  struct Words
  {
    std::string title;
    std::string explanation;
    std::string advice;
  };

  // The diagnosis as a title, what it means and what to do
  Words describe(Data const& data, Language language);

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

  // The crumbs as "12.3 s  button ..." lines, newest last, at most `count`
  std::vector<std::string> crumbLines(Data const& data, size_t count);
} // namespace report
