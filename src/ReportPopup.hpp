#pragma once

#include "Report.hpp"

#include <Geode/Geode.hpp>

// The report over the main menu. In detail (for a developer): the diagnosis, where, the source, the
// stack, the last actions, the log's last lines, recent crashes, with buttons to open the line in VS Code,
// copy a short report and open Geode's crashlogs. Short (for a player): which mod it most likely was and
// what to do, with the details a button away. Its text is TTF (GD's bitmap fonts have no Cyrillic),
// rendered once
class ReportPopup : public geode::Popup
{
public:
  static ReportPopup* create(report::Data const& data, report::Language language, bool detailed);

protected:
  report::Data m_data;
  report::Language m_language = report::Language::English;
  bool m_detailed = true;
  cocos2d::CCNode* m_column = nullptr;
  float m_width = 0;

  bool init(report::Data const& data, report::Language language, bool detailed);
  void fillDetails();
  void fillShort();

  // A wrapped TTF label at the end of the scrolled column
  void addText(std::string const& text, char const* font, float size, cocos2d::ccColor3B color, float gapAbove = 0);
  void addSection(std::string const& title);
  CCMenuItemSpriteExtra* makeButton(std::string const& label, std::function<void()> action);

  void onCopy();
  void onOpenCode();
  void onCrashlogs();
  void onDetails();

  char const* ui(char const* english, char const* russian) const;
};
