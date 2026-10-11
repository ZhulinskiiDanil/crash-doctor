#include "../src/Report.hpp"
#include "Check.hpp"

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

  return finish("report");
}
