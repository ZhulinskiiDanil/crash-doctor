#include "Capture.hpp"
#include "Crumbs.hpp"
#include "LoggerLink.hpp"
#include "Report.hpp"
#include "ReportPopup.hpp"
#include "Session.hpp"
#include "Symbols.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/modify/MenuLayer.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstring>
#include <fstream>

using namespace geode::prelude;

namespace
{
  session::File* s_session = nullptr;
  // What the previous session left, when it crashed
  std::unique_ptr<session::File> s_previous;
  std::optional<report::Data> s_report;
  bool s_reportShown = false;
  // The last report was being made or shown when the game crashed again: not made again
  bool s_reportCrashed = false;

  std::filesystem::path guardPath() { return Mod::get()->getSaveDir() / "report-open"; }

  void copyText(char* to, size_t capacity, std::string_view from)
  {
    auto size = std::min(from.size(), capacity - 1);
    std::memcpy(to, from.data(), size);
    to[size] = 0;
  }

  report::Language language()
  {
    auto setting = Mod::get()->getSettingValue<std::string>("language");
    if (setting == "English")
      return report::Language::English;
    if (setting == "Russian")
      return report::Language::Russian;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? report::Language::Russian
                                                                     : report::Language::English;
  }

  // The last lines of the crashed session's log, at most `count`
  std::vector<std::string> logTail(std::filesystem::path const& path, size_t count)
  {
    std::vector<std::string> lines;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
      return lines;
    stream.seekg(0, std::ios::end);
    auto size = static_cast<int64_t>(stream.tellg());
    auto start = std::max<int64_t>(0, size - 64 * 1024);
    stream.seekg(start);
    std::string text(static_cast<size_t>(size - start), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    for (auto& line : string::split(text, "\n"))
    {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.empty())
        continue;
      if (line.size() > 200)
        line = line.substr(0, 200) + "...";
      lines.push_back(line);
    }
    // The first line may be cut in half by the 64 KB
    if (start > 0 && !lines.empty())
      lines.erase(lines.begin());
    if (lines.size() > count)
      lines.erase(lines.begin(), lines.end() - count);
    return lines;
  }

  // Geode's crashlog of the crash: the newest one written after the session started
  std::string findCrashlog(uint64_t sessionStart)
  {
    std::error_code error;
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    // The C++ library on Windows counts file times like FILETIME
    auto start = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(sessionStart));
    for (auto const& entry : std::filesystem::directory_iterator(dirs::getCrashlogsDir(), error))
    {
      if (entry.path().extension() != ".log")
        continue;
      auto time = entry.last_write_time(error);
      if (error || time < start || time < newestTime)
        continue;
      newest = entry.path();
      newestTime = time;
    }
    return newest.empty() ? std::string() : utils::string::pathToString(newest);
  }

  report::Data buildReport(session::File const& previous)
  {
    report::Data data;
    data.crash = symbols::load(previous.crash);
    data.crash.selfId = Mod::get()->getID();
    for (auto mod : Loader::get()->getAllMods())
      for (auto hook : mod->getHooks())
        data.crash.hooks[std::string(mod->getID())].push_back(std::string(hook->getDisplayName()));
    data.diagnosis = analysis::diagnose(data.crash);
    data.crumbs = crumbs::read(previous);
    data.gameVersion = previous.header.gameVersion;
    data.geodeVersion = previous.header.geodeVersion;
    for (auto const& line : string::split(previous.header.mods, "\n"))
    {
      auto parts = string::split(line, "\t");
      if (parts.size() != 2)
        continue;
      auto mod = Loader::get()->getInstalledMod(parts[0]);
      data.mods.push_back({parts[0], mod ? std::string(mod->getName()) : parts[0], parts[1]});
    }
    data.logTail = logTail(std::filesystem::path(utils::string::utf8ToWide(previous.header.logPath)), 40);
    data.crashlog = findCrashlog(previous.header.startTime);
    if (!data.crumbs.empty())
      data.sessionSeconds = data.crumbs.back().millis / 1000.0;
    if (data.diagnosis.frame >= 0)
    {
      auto const& frame = data.crash.frames[data.diagnosis.frame];
      data.source = report::readSource(frame.file, frame.line);
    }

    // The history: this crash counted with the same ones before
    auto historyPath = Mod::get()->getSaveDir() / "history.tsv";
    auto saved = file::readString(historyPath);
    data.history = history::parse(saved ? *saved : "");
    auto seconds = (static_cast<int64_t>(previous.header.startTime) - 116444736000000000LL) / 10000000;
    history::add(data.history, report::historyEntry(data, seconds));
    (void)file::writeString(historyPath, history::serialize(data.history));
    return data;
  }

  // The report of the last session's crash, made once: when the game has loaded or the main menu
  // first opens, whichever comes first (the menu can be first)
  void makeReport()
  {
    if (!s_previous)
      return;
    if (s_reportCrashed)
    {
      log::warn("The last session crashed while Crash Doctor made or showed its report: no report this time");
      s_previous.reset();
      return;
    }

    // The guard is up while the report is made and shown: a crash in between won't loop
    (void)file::writeString(guardPath(), "");
    auto data = buildReport(*s_previous);
    s_previous.reset();
    logger_link::markReport();
    log::info("The last session crashed: {}", report::describe(data, report::Language::English).title);
    log::info("crash-doctor-result {}", report::resultLine(data));
    std::vector<std::string> frames;
    for (size_t i = 0; i < data.crash.frames.size() && i < 16; i++)
      frames.push_back(report::frameLine(data, data.crash.frames[i]));
    log::info("crash-doctor-stack {}", fmt::join(frames, " | "));
    for (auto const& line : report::crumbLines(data, 15))
      log::info("crash-doctor-crumb {}", line);
    s_report = std::move(data);
    if (!Mod::get()->getSettingValue<bool>("show-report"))
    {
      std::error_code error;
      std::filesystem::remove(guardPath(), error);
      s_reportShown = true;
    }
  }

  void showReport()
  {
    if (s_reportShown || !s_report)
      return;
    s_reportShown = true;
    // In detail for a developer (the mod's PDB is on this computer) or when asked; short for a player
    auto detail = Mod::get()->getSettingValue<std::string>("report-detail");
    bool detailed = detail == "Always detailed" || (detail != "Always short" && report::hasSymbols(*s_report));
    if (auto popup = ReportPopup::create(*s_report, language(), detailed))
      popup->show();
  }
} // namespace

// ! --- Start --- !

// Early: crashes while the other mods load are caught too
$on_mod(Loaded)
{
  auto dir = Mod::get()->getSaveDir();
  auto path = dir / "session.bin";
  s_previous = session::takePreviousCrash(path);

  std::error_code error;
  if (std::filesystem::exists(guardPath(), error))
  {
    s_reportCrashed = s_previous != nullptr;
    std::filesystem::remove(guardPath(), error);
  }

  s_session = session::open(path);
  if (!s_session)
  {
    log::error("Can't open {}: crashes this session won't be reported", path);
    return;
  }
  auto& header = s_session->header;
  copyText(header.gameVersion, sizeof(header.gameVersion), Loader::get()->getGameVersion());
  copyText(header.geodeVersion, sizeof(header.geodeVersion), Loader::get()->getVersion().toVString());
  copyText(header.logPath, sizeof(header.logPath), utils::string::pathToString(log::getCurrentLogPath()));

  crumbs::attach(s_session);
  crumbs::add("game started");
  capture::install(s_session);
  logger_link::setup();
  log::debug("Watching for crashes, session file {}", path);
}

$on_game(Loaded)
{
  if (s_session)
  {
    // The mods of this session, for the report if it crashes
    std::string mods;
    for (auto mod : Loader::get()->getAllMods())
      if (mod->isLoaded())
        mods += fmt::format("{}\t{}\n", mod->getID(), mod->getVersion().toVString());
    copyText(s_session->header.mods, sizeof(s_session->header.mods), mods);
    crumbs::add("game loaded");
  }

  makeReport();
}

class $modify(ReportMenuLayer, MenuLayer)
{
  bool init()
  {
    if (!MenuLayer::init())
      return false;
    makeReport();
    if (s_report && !s_reportShown)
    {
      // After Geode's own "it crashed" popup, which is queued for the next frame: this one on top
      this->scheduleOnce(schedule_selector(ReportMenuLayer::onShowReport), 0.2f);
    }
    return true;
  }

  void onShowReport(float)
  {
    showReport();
    this->scheduleOnce(schedule_selector(ReportMenuLayer::onReportShown), 3.f);
  }

  void onReportShown(float)
  {
    std::error_code error;
    std::filesystem::remove(guardPath(), error);
  }
};
