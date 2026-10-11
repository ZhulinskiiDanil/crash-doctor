#include "LoggerLink.hpp"

#include "Crumbs.hpp"
#include "ImprovedLogger.hpp"

#include <Geode/Geode.hpp>

#include <deque>

using namespace geode::prelude;

namespace
{
  constexpr char const* LOGGER_ID = "zhulis.improved-logger";
  bool s_linked = false;
  // The last few crumbs, newest last
  std::deque<std::string> s_recent;

  void onCrumb(char const* text)
  {
    // A crumb counting a repeat replaces the one it counts
    std::string crumb = text;
    if (!s_recent.empty() && crumb.starts_with(s_recent.back()) && crumb.find(" (x") != std::string::npos)
      s_recent.back() = crumb;
    else
      s_recent.push_back(crumb);
    while (s_recent.size() > 3)
      s_recent.pop_front();
    std::string joined;
    for (auto const& recent : s_recent)
      joined += (joined.empty() ? "" : " > ") + recent;
    improved_logger::watch("last actions", joined);
  }
} // namespace

void logger_link::setup()
{
  if (!Loader::get()->getLoadedMod(LOGGER_ID))
    return;
  s_linked = true;
  crumbs::listen(onCrumb);
  log::info("Improved Logger is here: the last actions are pinned in its console");
}

void logger_link::markReport()
{
  if (s_linked)
    improved_logger::mark("the last session crashed");
}
