#include "ReportPopup.hpp"

#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace
{
  constexpr float WIDTH = 460.f;
  constexpr float HEIGHT = 290.f;
  constexpr float LIST_WIDTH = 420.f;
  constexpr float LIST_HEIGHT = 170.f;
  constexpr char const* FONT = "Segoe UI";
  constexpr char const* MONO = "Consolas";

  ccColor3B const TITLE = {255, 210, 90};
  ccColor3B const TEXT = {255, 255, 255};
  ccColor3B const DIM = {190, 200, 220};
  ccColor3B const SECTION = {120, 200, 255};

  std::string profileFolder()
  {
    auto profile = std::getenv("USERPROFILE");
    return profile ? profile : "";
  }
} // namespace

ReportPopup* ReportPopup::create(report::Data const& data, report::Language language)
{
  auto popup = new ReportPopup();
  if (popup->init(data, language))
  {
    popup->autorelease();
    return popup;
  }
  delete popup;
  return nullptr;
}

char const* ReportPopup::ui(char const* english, char const* russian) const
{
  return m_language == report::Language::Russian ? russian : english;
}

bool ReportPopup::init(report::Data const& data, report::Language language)
{
  if (!Popup::init(WIDTH, HEIGHT))
    return false;
  m_data = data;
  m_language = language;
  setTitle("Crash Doctor");
  m_noElasticity = true;

  auto words = report::describe(m_data, m_language);

  // The diagnosis, above the list
  auto heading = CCLabelTTF::create(words.title.c_str(), FONT, 13.f);
  heading->setColor(TITLE);
  if (heading->getContentSize().width > LIST_WIDTH)
    heading->setScale(LIST_WIDTH / heading->getContentSize().width);
  m_mainLayer->addChildAtPosition(heading, Anchor::Top, {0.f, -44.f});

  auto background = NineSlice::create("square02b_001.png");
  background->setColor({0, 0, 0});
  background->setOpacity(90);
  background->setContentSize({LIST_WIDTH + 10.f, LIST_HEIGHT + 6.f});
  m_mainLayer->addChildAtPosition(background, Anchor::Center, {0.f, -8.f});

  auto list = ScrollLayer::create({LIST_WIDTH, LIST_HEIGHT});
  m_column = list->m_contentLayer;
  m_width = LIST_WIDTH - 12.f;
  m_column->setLayout(ColumnLayout::create()
                          ->setAxisReverse(true)
                          ->setAxisAlignment(AxisAlignment::End)
                          ->setCrossAxisAlignment(AxisAlignment::Start)
                          ->setCrossAxisLineAlignment(AxisAlignment::Start)
                          ->setAutoGrowAxis(LIST_HEIGHT)
                          ->setGap(2.f));
  m_mainLayer->addChildAtPosition(list, Anchor::Center, {-LIST_WIDTH / 2.f, -8.f - LIST_HEIGHT / 2.f});

  // Where first: the mod and the line are what a developer looks for
  if (auto culprit = report::culpritLine(m_data); !culprit.empty())
    addText(std::string(ui("Mod: ", "Мод: ")) + culprit, FONT, 9.f, TEXT);
  else
    addText(ui("No mod's code on the stack", "Кода модов в стеке нет"), FONT, 9.f, TEXT);
  if (auto where = report::location(m_data); !where.empty())
    addText(std::string(ui("Line: ", "Строка: ")) + where, MONO, 8.5f, TEXT);
  if (m_data.diagnosis.frame >= 0 && !m_data.crash.frames[m_data.diagnosis.frame].function.empty())
    addText(std::string(ui("Function: ", "Функция: ")) + m_data.crash.frames[m_data.diagnosis.frame].function, MONO, 8.5f, TEXT);
  auto exception = analysis::exceptionName(m_data.crash.code);
  if (m_data.crash.code == analysis::ACCESS_VIOLATION && m_data.crash.parameters.size() >= 2)
    exception += fmt::format(" ({} 0x{:x})",
                             m_data.crash.parameters[0] == 1 ? "write" : m_data.crash.parameters[0] == 8 ? "execute" : "read",
                             m_data.crash.parameters[1]);
  addText(std::string(ui("Exception: ", "Исключение: ")) + exception + ", " +
              (m_data.crash.mainThread ? ui("main thread", "главный поток") : ui("not the main thread", "не главный поток")),
          FONT, 9.f, DIM);
  if (!m_data.diagnosis.evidence.empty())
    addText(std::string(ui("Sign: ", "Признак: ")) + m_data.diagnosis.evidence, FONT, 9.f, DIM);

  addSection(ui("What it means", "Что это значит"));
  addText(words.explanation, FONT, 9.f, TEXT);
  addText(words.advice, FONT, 9.f, DIM, 3.f);

  // The stack, a recursion folded
  addSection(ui("Stack", "Стек"));
  std::string stack;
  size_t shown = 0;
  for (size_t i = 0; i < m_data.crash.frames.size() && shown < 16; i++)
  {
    auto const& frame = m_data.crash.frames[i];
    size_t same = 0;
    while (frame.functionRva && i + same + 1 < m_data.crash.frames.size() &&
           m_data.crash.frames[i + same + 1].functionRva == frame.functionRva &&
           m_data.crash.frames[i + same + 1].module == frame.module)
      same++;
    stack += fmt::format("#{:<3} {}\n", i, report::frameLine(m_data, frame));
    shown++;
    if (same >= 2)
    {
      stack += fmt::format("     ... {} {}\n", same, ui("more times", "раз ещё"));
      i += same;
    }
  }
  if (!stack.empty())
    stack.pop_back();
  addText(stack, MONO, 7.5f, TEXT);

  auto crumbs = report::crumbLines(m_data, 15);
  addSection(ui("Last actions", "Последние действия"));
  if (crumbs.empty())
    addText(ui("None recorded", "Ничего не записано"), FONT, 9.f, DIM);
  else
    addText(fmt::format("{}", fmt::join(crumbs, "\n")), MONO, 7.5f, TEXT);

  if (!m_data.logTail.empty())
  {
    addSection(ui("The log's last lines", "Последние строки лога"));
    addText(fmt::format("{}", fmt::join(m_data.logTail, "\n")), MONO, 7.f, DIM);
  }

  addSection(ui("Versions", "Версии"));
  addText(fmt::format("GD {}, Geode {}, {} {}", m_data.gameVersion, m_data.geodeVersion, m_data.mods.size(),
                      ui("mods", "модов")),
          FONT, 9.f, DIM);
  if (!m_data.crashlog.empty())
    addText(std::string(ui("Geode's crashlog: ", "Crashlog Geode: ")) + report::scrub(m_data.crashlog, profileFolder()), MONO,
            10.f, DIM);

  m_column->updateLayout();
  list->scrollToTop();

  auto scrollbar = Scrollbar::create(list);
  m_mainLayer->addChildAtPosition(scrollbar, Anchor::Center, {LIST_WIDTH / 2.f + 10.f, -8.f});

  // The buttons
  auto menu = CCMenu::create();
  menu->setContentSize({WIDTH - 20.f, 30.f});
  menu->setLayout(RowLayout::create()->setGap(8.f));
  if (!report::location(m_data).empty())
    menu->addChild(makeButton(ui("Open in VS Code", "Открыть в VS Code"), [this] { onOpenCode(); }));
  menu->addChild(makeButton(ui("Copy report", "Скопировать отчёт"), [this] { onCopy(); }));
  menu->addChild(makeButton(ui("Crashlogs", "Папка crashlog"), [this] { onCrashlogs(); }));
  menu->updateLayout();
  m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, {0.f, 22.f});
  return true;
}

void ReportPopup::addText(std::string const& text, char const* font, float size, ccColor3B color, float gapAbove)
{
  if (gapAbove > 0)
  {
    auto gap = CCNode::create();
    gap->setContentSize({1.f, gapAbove});
    m_column->addChild(gap);
  }
  auto label = CCLabelTTF::create(text.c_str(), font, size, {m_width, 0.f}, kCCTextAlignmentLeft);
  label->setColor(color);
  label->setAnchorPoint({0.f, 1.f});
  m_column->addChild(label);
}

void ReportPopup::addSection(std::string const& title)
{
  addText(title, FONT, 10.f, SECTION, 5.f);
}

CCMenuItemSpriteExtra* ReportPopup::makeButton(std::string const& label, std::function<void()> action)
{
  auto text = CCLabelTTF::create(label.c_str(), FONT, 9.f);
  auto size = CCSize(text->getContentSize().width + 20.f, 24.f);
  auto background = NineSlice::create("GJ_button_04.png");
  background->setContentSize(size);
  background->addChildAtPosition(text, Anchor::Center);
  return CCMenuItemExt::createSpriteExtra(background, [action = std::move(action)](auto) { action(); });
}

void ReportPopup::onCopy()
{
  auto text = report::scrub(report::copyText(m_data, m_language), profileFolder());
  clipboard::write(text);
  Notification::create(ui("Report copied", "Отчёт скопирован"), NotificationIcon::Success)->show();
}

void ReportPopup::onOpenCode()
{
  auto frame = m_data.crash.frames[m_data.diagnosis.frame];
  web::openLinkInBrowser(report::vscodeLink(frame.file, frame.line));
}

void ReportPopup::onCrashlogs()
{
  file::openFolder(dirs::getCrashlogsDir());
}
