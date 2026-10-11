#include "../src/Report.hpp"
#include "Check.hpp"

#include <filesystem>
#include <fstream>

using namespace report;

namespace
{
  Data nullCrash()
  {
    Data data;
    analysis::Module testbed;
    testbed.path = R"(C:\Users\Player\GD\geode\unzipped\zhulis.crash-doctor-testbed\zhulis.crash-doctor-testbed.dll)";
    testbed.name = "zhulis.crash-doctor-testbed.dll";
    testbed.kind = analysis::classify(testbed.path, testbed.modId);
    testbed.base = 0x7FFC00000000;
    testbed.size = 0x100000;
    data.crash.modules.push_back(testbed);
    data.crash.code = analysis::ACCESS_VIOLATION;
    data.crash.parameters = {0, 0x1A8};
    analysis::Frame frame;
    frame.module = 0;
    frame.address = testbed.base + 0x1234;
    frame.functionRva = 0x1200;
    frame.function = "TestbedMenuLayer::crashNull";
    frame.file = R"(C:\Users\Player\src\testbed\main.cpp)";
    frame.line = 42;
    data.crash.frames.push_back(frame);
    data.crash.registers[16] = frame.address;
    data.diagnosis = analysis::diagnose(data.crash);
    data.mods = {{"zhulis.crash-doctor-testbed", "Crash Doctor Testbed", "v1.0.0"}, {"geode.node-ids", "Node IDs", "v1.21.0"}};
    data.gameVersion = "2.2081";
    data.geodeVersion = "v5.10.1";
    data.crumbs = {{1, 1500, "scene MenuLayer"}, {2, 3200, "button settings-button (CCMenuItemSpriteExtra) in MenuLayer"}};
    data.crashlog = R"(C:\GD\geode\crashlogs\2026-10-11_15-00-00.log)";
    return data;
  }
} // namespace

int main()
{
  auto data = nullCrash();
  CHECK(data.diagnosis.kind == analysis::Kind::NullPointer);

  CHECK(frameLine(data, data.crash.frames[0]) == "zhulis.crash-doctor-testbed.dll!TestbedMenuLayer::crashNull (main.cpp:42)");
  CHECK(culpritLine(data) == "Crash Doctor Testbed v1.0.0 (zhulis.crash-doctor-testbed)");
  CHECK(location(data) == R"(C:\Users\Player\src\testbed\main.cpp:42)");
  CHECK(vscodeLink(R"(D:\Dev\my mod\src\main.cpp)", 42) == "vscode://file/D:/Dev/my%20mod/src/main.cpp:42");

  // ! --- Words --- !

  {
    auto en = describe(data, Language::English);
    CHECK(en.title == "Most likely: a null pointer");
    CHECK(en.explanation.find("TestbedMenuLayer::crashNull") != std::string::npos);
    auto ru = describe(data, Language::Russian);
    CHECK(ru.title.find("nullptr") != std::string::npos);
    CHECK(ru.title != en.title);
  }
  {
    // Every kind has words in both languages
    for (auto kind : {analysis::Kind::Unknown, analysis::Kind::NullPointer, analysis::Kind::Recursion,
                      analysis::Kind::HookRecursion, analysis::Kind::UseAfterFree, analysis::Kind::WrongThread,
                      analysis::Kind::PureVirtual, analysis::Kind::CppException, analysis::Kind::DivideByZero})
    {
      auto copy = data;
      copy.diagnosis.kind = kind;
      copy.diagnosis.hook = "MenuLayer::onMoreGames";
      for (auto language : {Language::English, Language::Russian})
      {
        auto words = describe(copy, language);
        CHECK(!words.title.empty() && !words.explanation.empty() && !words.advice.empty());
      }
      CHECK(kindKey(kind) != "");
    }
    auto copy = data;
    copy.diagnosis.kind = analysis::Kind::HookRecursion;
    copy.diagnosis.hook = "MenuLayer::onMoreGames";
    CHECK(describe(copy, Language::English).advice.find("MenuLayer::onMoreGames(...)") != std::string::npos);
  }

  // ! --- The log by mod --- !

  {
    auto groups = groupLog({
        "16:06:31 INFO  [Main] [Geode]: Loading mods",
        "16:06:31 WARN  [Main] [Testbed]: No button settings-button to press",
        "16:06:32 INFO  [Main] [Geode]: Loaded",
        "16:06:33 ERROR [Main] [Testbed]: Something broke",
        "  and its second line",
        "16:06:34 INFO  [Mod Profiler] [Mod Profiler]: 996 samples",
        "16:06:34 INFO  [Main] [Testbed]: Testbed: crashing now",
    });
    CHECK(groups.size() == 3);
    CHECK(groups[0].mod == "Testbed" && groups[0].lines == 3 && groups[0].problems == 2);
    CHECK(groups[0].last.size() == 2);
    CHECK(groups[0].last[0] == "Something broke   and its second line");
    CHECK(groups[0].last[1] == "Testbed: crashing now");
    CHECK(groups[1].mod == "Mod Profiler" && groups[1].lines == 1);
    CHECK(groups[2].mod == "Geode" && groups[2].lines == 2 && groups[2].problems == 0);
    CHECK(groupLog({"not a log line"}).empty());
  }

  // ! --- For a player --- !

  {
    CHECK(hasSymbols(data));
    auto player = describeForPlayer(data, Language::English);
    CHECK(player.title == "The game crashed in Crash Doctor Testbed");
    CHECK(player.explanation.find("Most likely: a null pointer") != std::string::npos);
    auto copy = data;
    copy.crash.frames[0].file.clear();
    CHECK(!hasSymbols(copy));
    copy.history = {{0, "s", "null-pointer", "zhulis.crash-doctor-testbed", "x", 3}};
    CHECK(describeForPlayer(copy, Language::Russian).explanation.find("3") != std::string::npos);
    copy.diagnosis = {};
    CHECK(describeForPlayer(copy, Language::English).title == "The game crashed");
    CHECK(!hasSymbols(copy));
  }

  // ! --- Copied text --- !

  {
    auto text = copyText(data, Language::English);
    CHECK(text.find("Most likely: a null pointer") != std::string::npos);
    CHECK(text.find("Crash Doctor Testbed v1.0.0") != std::string::npos);
    CHECK(text.find("read 0x1a8") != std::string::npos);
    CHECK(text.find("button settings-button") != std::string::npos);
    CHECK(text.find("GD 2.2081, Geode v5.10.1, zhulis.crash-doctor-testbed v1.0.0, mods in total: 2") != std::string::npos);
    // Not a mod on the stack: not named
    CHECK(text.find("geode.node-ids") == std::string::npos);

    auto scrubbed = scrub(text, R"(C:\Users\Player)");
    CHECK(scrubbed.find("Player") == std::string::npos);
    CHECK(scrubbed.find(R"(%USERPROFILE%\src\testbed\main.cpp:42)") != std::string::npos);
  }

  // ! --- Scrubbing --- !

  CHECK(scrub(R"(c:\users\player\a.txt and C:/Users/Player/b.txt)", R"(C:\Users\Player)") ==
        R"(%USERPROFILE%\a.txt and %USERPROFILE%/b.txt)");
  CHECK(scrub(R"(D:\Users\Player\x)", R"(C:\Users\Player)") == R"(D:\Users\%USERNAME%\x)");
  // A Cyrillic user name: the profile comes as UTF-8 like the report
  CHECK(scrub(R"(C:\Users\Игрок\GD\x.cpp)", R"(C:\Users\Игрок)") == R"(%USERPROFILE%\GD\x.cpp)");
  CHECK(scrub("nothing here", R"(C:\Users\Player)") == "nothing here");
  CHECK(scrub("short", "") == "short");

  // ! --- The log's line --- !

  {
    auto line = resultLine(data);
    CHECK(line.find("\"diagnosis\":\"null-pointer\"") != std::string::npos);
    CHECK(line.find("\"culprit\":\"zhulis.crash-doctor-testbed\"") != std::string::npos);
    // Backslashes escaped in JSON
    CHECK(line.find(R"("file":"C:\\Users\\Player\\src\\testbed\\main.cpp")") != std::string::npos);
    CHECK(line.find("\"line\":42") != std::string::npos);
    CHECK(line.find("\"crashlog\":\"2026-10-11_15-00-00.log\"") != std::string::npos);
  }

  // ! --- A recursion is folded --- !

  {
    auto copy = data;
    for (int i = 0; i < 100; i++)
      copy.crash.frames.push_back(copy.crash.frames[0]);
    auto text = copyText(copy, Language::English);
    CHECK(text.find("... 100 more times") != std::string::npos);
  }

  // ! --- Source --- !

  {
    auto path = std::filesystem::temp_directory_path() / "crash-doctor-source-test.cpp";
    {
      std::ofstream file(path, std::ios::binary);
      for (int i = 1; i <= 10; i++)
        file << "\tline " << i << "\r\n";
    }
    auto file = path.string();
    auto lines = readSource(file, 5);
    CHECK(lines.size() == 7);
    CHECK(lines.front().number == 2 && lines.back().number == 8);
    CHECK(lines[3].text == "    line 5");
    CHECK(readSource(file, 10).size() == 4);
    // Past the end of the file: not the file that was built
    CHECK(readSource(file, 40).empty());
    CHECK(readSource(file, 0).empty());
    CHECK(readSource(R"(Z:\no\such\file.cpp)", 3).empty());

    auto copy = data;
    copy.crash.frames[0].line = 5;
    copy.source = lines;
    auto shown = sourceLines(copy);
    CHECK(shown.size() == 7);
    CHECK(shown[3] == ">     5 |     line 5");
    CHECK(shown[2] == "      4 |     line 4");
    CHECK(copyText(copy, Language::English).find("```cpp") != std::string::npos);
    std::filesystem::remove(path);
  }

  return finish("report");
}
