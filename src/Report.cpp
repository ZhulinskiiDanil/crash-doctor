#include "Report.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace report;
using analysis::Kind;

namespace
{
  std::string hex(uint64_t value)
  {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
  }

  std::string fileName(std::string_view path)
  {
    auto slash = path.find_last_of("\\/");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
  }

  void replaceAll(std::string& text, std::string_view what, std::string_view with)
  {
    for (size_t at = 0; (at = text.find(what, at)) != std::string::npos; at += with.size())
      text.replace(at, what.size(), with);
  }

  analysis::Frame const* culpritFrame(Data const& data)
  {
    auto index = data.diagnosis.frame;
    if (index < 0 || index >= static_cast<int>(data.crash.frames.size()))
      return nullptr;
    return &data.crash.frames[index];
  }

  std::string modName(Data const& data, std::string const& id)
  {
    for (auto const& mod : data.mods)
      if (mod.id == id && !mod.name.empty())
        return mod.name;
    return id;
  }

  std::string jsonString(std::string_view text)
  {
    std::string result = "\"";
    for (auto c : text)
    {
      if (c == '"' || c == '\\')
      {
        result += '\\';
        result += c;
      }
      else if (static_cast<unsigned char>(c) < 0x20)
      {
        char escaped[8];
        std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
        result += escaped;
      }
      else
        result += c;
    }
    return result + "\"";
  }

  // The hook's original as the code should call it: "MenuLayer::onMoreGames(...)"
  std::string originalCall(std::string const& hook)
  {
    if (hook.empty())
      return "Base::method(...)";
    auto open = hook.find('(');
    return (open == std::string::npos ? hook : hook.substr(0, open)) + "(...)";
  }

  std::string functionOf(Data const& data)
  {
    auto frame = culpritFrame(data);
    if (!frame || frame->function.empty())
      return {};
    return frame->function;
  }
} // namespace

std::string_view report::kindKey(Kind kind)
{
  switch (kind)
  {
  case Kind::NullPointer:
    return "null-pointer";
  case Kind::Recursion:
    return "recursion";
  case Kind::HookRecursion:
    return "hook-recursion";
  case Kind::UseAfterFree:
    return "use-after-free";
  case Kind::WrongThread:
    return "wrong-thread";
  case Kind::PureVirtual:
    return "pure-virtual-call";
  case Kind::CppException:
    return "cpp-exception";
  case Kind::DivideByZero:
    return "divide-by-zero";
  case Kind::Unknown:
    break;
  }
  return "unknown";
}

Words report::describe(Data const& data, Language language)
{
  auto const& diagnosis = data.diagnosis;
  bool ru = language == Language::Russian;
  auto function = functionOf(data);
  auto inFunction = function.empty() ? std::string() : " (" + function + ")";
  Words words;
  switch (diagnosis.kind)
  {
  case Kind::NullPointer:
    words.title = ru ? "Скорее всего: обращение через nullptr" : "Most likely: a null pointer";
    words.explanation =
        ru ? "Код мода обратился к объекту по нулевому указателю" + inFunction +
                 ". Обычно это результат getChildByID, typeinfo_cast, create() или поиска, который вернул nullptr, а код "
                 "использовал его без проверки."
           : "The mod's code used a pointer that was null" + inFunction +
                 ". Usually it came from getChildByID, typeinfo_cast, create() or a lookup that returned nullptr and "
                 "was used without a check.";
    words.advice = ru ? "Проверяйте результат перед использованием: if (auto node = this->getChildByID(\"...\")) { ... }. "
                        "Узел может пропасть из-за другого мода или обновления игры."
                      : "Check the result before using it: if (auto node = this->getChildByID(\"...\")) { ... }. "
                        "Another mod or a game update can remove or rename the node.";
    break;
  case Kind::Recursion:
    words.title = ru ? "Скорее всего: бесконечная рекурсия" : "Most likely: endless recursion";
    words.explanation =
        ru ? "Стек переполнился: функция" + inFunction + " вызывает себя снова и снова (" +
                 std::to_string(diagnosis.repeats) + " кадров стека)."
           : "The stack overflowed: a function" + inFunction + " calls itself again and again (" +
                 std::to_string(diagnosis.repeats) + " stack frames).";
    words.advice = ru ? "Проверьте условие выхода из рекурсии и не вызывает ли функция себя через другой вызов."
                      : "Check the recursion's exit condition, and whether the function calls itself through another call.";
    break;
  case Kind::HookRecursion:
    words.title = ru ? "Скорее всего: хук вызывает сам себя" : "Most likely: a hook calls itself";
    words.explanation = ru ? "Хук " + diagnosis.hook + " вызывает свою же функцию вместо оригинала, и стек переполняется (" +
                                 std::to_string(diagnosis.repeats) + " кадров)."
                           : "The hook of " + diagnosis.hook + " calls itself instead of the original, until the stack overflows (" +
                                 std::to_string(diagnosis.repeats) + " frames).";
    words.advice = ru ? "Внутри $modify вызывайте оригинал через базовый класс: " + originalCall(diagnosis.hook) +
                            ", а не просто имя метода или this->метод."
                      : "Inside $modify, call the original through the base class: " + originalCall(diagnosis.hook) +
                            ", not the bare method name or this->method.";
    break;
  case Kind::UseAfterFree:
    words.title = ru ? "Скорее всего: обращение к удалённому объекту" : "Most likely: a deleted object was used";
    words.explanation =
        ru ? "Объект, с которым работал мод" + inFunction +
                 ", похоже, уже удалён: его память занята другими данными. Так бывает, когда хранят обычный указатель на "
                 "ноду, а её удалили (removeFromParent, смена сцены)."
           : "The object the mod used" + inFunction +
                 " seems to be deleted already: its memory holds other data now. That happens when a plain pointer to a "
                 "node is kept and the node gets removed (removeFromParent, a scene change).";
    words.advice = ru ? "Храните ноду через geode::Ref<T> или retain()/release(), обнуляйте указатель при удалении "
                        "и проверяйте, что нода ещё на сцене."
                      : "Keep the node in a geode::Ref<T> or retain()/release() it, clear the pointer when it's removed, "
                        "and check the node is still in the scene.";
    break;
  case Kind::WrongThread:
    words.title = ru ? "Скорее всего: UI из другого потока" : "Most likely: UI from another thread";
    words.explanation =
        ru ? "Игра упала внутри cocos2d в потоке, который не является главным" + inFunction +
                 ". cocos2d и интерфейс игры не потокобезопасны: создавать, менять и удалять ноды можно только из главного "
                 "потока."
           : "The game crashed inside cocos2d on a thread that isn't the main one" + inFunction +
                 ". cocos2d and the game's UI aren't thread-safe: nodes can only be made, changed or removed on the main "
                 "thread.";
    words.advice = ru ? "Перенесите работу с UI в главный поток: geode::queueInMainThread([=] { ... });"
                      : "Move the UI work to the main thread: geode::queueInMainThread([=] { ... });";
    break;
  case Kind::PureVirtual:
    words.title = ru ? "Скорее всего: обращение к объекту во время удаления" : "Most likely: an object used while being deleted";
    words.explanation =
        ru ? "Вызвана чисто виртуальная функция (pure virtual call)" + inFunction +
                 ": метод вызвали у объекта, который уже разрушается (из деструктора базового класса) или ещё не "
                 "создан до конца."
           : "A pure virtual function was called" + inFunction +
                 ": a method was called on an object that is being destroyed (from a base class' destructor) or isn't "
                 "fully constructed yet.";
    words.advice = ru ? "Не вызывайте виртуальные методы из конструкторов и деструкторов; отписывайте колбэки и "
                        "слушатели до удаления объекта."
                      : "Don't call virtual methods from constructors and destructors; remove callbacks and listeners "
                        "before the object is deleted.";
    break;
  case Kind::CppException:
  {
    auto type = data.crash.cppType.empty() ? std::string(ru ? "неизвестного типа" : "of an unknown type")
                                           : analysis::typeName(data.crash.cppType);
    auto what = data.crash.cppWhat.empty() ? std::string() : ": \"" + data.crash.cppWhat + "\"";
    words.title = ru ? "Скорее всего: исключение C++ без обработки" : "Most likely: an uncaught C++ exception";
    words.explanation = ru ? "Брошено исключение " + type + what + inFunction + ", и его никто не поймал."
                           : "An exception " + type + what + " was thrown" + inFunction + " and nothing caught it.";
    words.advice = ru ? "Ловите исключения там, где код может их бросить (std::stoi, at(), разбор JSON), или "
                        "используйте варианты без исключений (geode::utils::numFromString, Result)."
                      : "Catch exceptions where the code can throw them (std::stoi, at(), JSON parsing), or use the "
                        "variants without exceptions (geode::utils::numFromString, Result).";
    break;
  }
  case Kind::DivideByZero:
    words.title = ru ? "Скорее всего: деление на ноль" : "Most likely: a division by zero";
    words.explanation = ru ? "Целочисленное деление на ноль" + inFunction + "." : "An integer division by zero" + inFunction + ".";
    words.advice = ru ? "Проверяйте делитель перед делением." : "Check the divisor before dividing.";
    break;
  case Kind::Unknown:
    words.title = ru ? "Причина не определена" : "No diagnosis";
    words.explanation = ru ? "Надёжных признаков известной ошибки нет, поэтому без догадок: ниже где именно упало, стек и "
                             "последние действия."
                           : "There are no reliable signs of a known mistake, so no guess: below is where it crashed, the "
                             "stack and the last actions.";
    words.advice = ru ? "Отправьте отчёт разработчику мода: кнопка «Скопировать отчёт»."
                      : "Send the report to the mod's developer: the Copy report button.";
    break;
  }
  if (!diagnosis.hook.empty() && diagnosis.kind != Kind::HookRecursion)
    words.explanation += ru ? " Это хук мода на " + diagnosis.hook + "." : " It's the mod's hook of " + diagnosis.hook + ".";
  return words;
}

bool report::hasSymbols(Data const& data)
{
  auto frame = culpritFrame(data);
  return frame && !frame->file.empty() && frame->line > 0;
}

Words report::describeForPlayer(Data const& data, Language language)
{
  bool ru = language == Language::Russian;
  auto diagnosis = describe(data, language).title;
  Words words;
  auto const& id = data.diagnosis.culprit;
  int count = data.history.empty() ? 1 : data.history.front().count;
  if (id.empty())
  {
    words.title = ru ? "Игра упала" : "The game crashed";
    words.explanation = ru ? "Не удалось понять, какой мод виноват: в момент краша кода модов в стеке не было."
                           : "It isn't clear which mod it was: no mod's code was on the stack when it crashed.";
    words.advice = ru ? "Если краш повторяется, нажмите «Скопировать отчёт» и отправьте его в Discord Geode или "
                        "разработчикам модов, которые вы ставили недавно."
                      : "If it happens again, press Copy report and send it to Geode's Discord or to the developers of "
                        "the mods you installed lately.";
    return words;
  }
  auto name = modName(data, id);
  words.title = ru ? "Игра упала из-за мода " + name : "The game crashed in " + name;
  words.explanation = ru ? "Это ошибка в моде " + name + " (" + id + "), не в игре и не у вас. " + diagnosis + "."
                         : "It's a bug in " + name + " (" + id + "), not in the game or on your side. " + diagnosis + ".";
  if (count > 1)
    words.explanation += ru ? " Этот краш был уже " + std::to_string(count) + " раз."
                            : " This crash happened " + std::to_string(count) + " times.";
  words.advice = ru ? "Нажмите «Скопировать отчёт» и отправьте его разработчику мода. Если краш повторяется, выключите "
                      "мод в списке модов Geode, пока не выйдет исправление."
                    : "Press Copy report and send it to the mod's developer. If it keeps happening, turn the mod off "
                      "in Geode's mods list until a fix is out.";
  return words;
}

std::string report::frameLine(Data const& data, analysis::Frame const& frame)
{
  std::string line;
  analysis::Module const* module = nullptr;
  if (frame.module >= 0 && frame.module < static_cast<int>(data.crash.modules.size()))
    module = &data.crash.modules[frame.module];
  if (module)
    line = module->name;
  else
    line = "<no module>";
  if (!frame.function.empty())
    line += "!" + frame.function;
  else if (module)
    line += "+" + hex(frame.address - module->base);
  else
    line += " " + hex(frame.address);
  if (!frame.file.empty())
    line += " (" + fileName(frame.file) + ":" + std::to_string(frame.line) + ")";
  if (frame.scanned)
    line += " ?";
  return line;
}

std::string report::culpritLine(Data const& data)
{
  auto const& id = data.diagnosis.culprit;
  if (id.empty())
    return {};
  for (auto const& mod : data.mods)
    if (mod.id == id)
      return (mod.name.empty() ? id : mod.name) + " " + mod.version + " (" + id + ")";
  return id;
}

std::string report::location(Data const& data)
{
  auto frame = culpritFrame(data);
  if (!frame || frame->file.empty() || frame->line <= 0)
    return {};
  return frame->file + ":" + std::to_string(frame->line);
}

std::string report::vscodeLink(std::string_view file, int line)
{
  std::string path(file);
  std::replace(path.begin(), path.end(), '\\', '/');
  std::string encoded;
  for (unsigned char c : path)
  {
    if (std::isalnum(c) || c == '/' || c == ':' || c == '.' || c == '-' || c == '_' || c >= 0x80)
      encoded += static_cast<char>(c);
    else
    {
      char escaped[4];
      std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
      encoded += escaped;
    }
  }
  return "vscode://file/" + encoded + ":" + std::to_string(line);
}

std::vector<SourceLine> report::readSource(std::string const& path, int line, int radius)
{
  std::vector<SourceLine> lines;
  if (path.empty() || line <= 0)
    return lines;
  // The path is UTF-8 from the PDB
  std::ifstream stream(std::filesystem::path(reinterpret_cast<char8_t const*>(path.c_str())));
  if (!stream)
    return lines;
  std::string text;
  for (int number = 1; std::getline(stream, text) && number <= line + radius; number++)
  {
    if (number < line - radius)
      continue;
    if (!text.empty() && text.back() == '\r')
      text.pop_back();
    // Tabs as spaces: the window's font has no tab stops
    std::string expanded;
    for (auto c : text)
      expanded += c == '\t' ? std::string("    ") : std::string(1, c);
    if (expanded.size() > 140)
      expanded = expanded.substr(0, 140) + "...";
    lines.push_back({number, expanded});
  }
  // The file is shorter than the line: not the file that was built
  if (lines.empty() || lines.back().number < line)
    lines.clear();
  return lines;
}

std::vector<std::string> report::sourceLines(Data const& data)
{
  std::vector<std::string> lines;
  auto frame = culpritFrame(data);
  for (auto const& source : data.source)
  {
    char number[16];
    std::snprintf(number, sizeof(number), "%5d", source.number);
    auto marker = frame && source.number == frame->line ? "> " : "  ";
    lines.push_back(marker + std::string(number) + " | " + source.text);
  }
  return lines;
}

history::Entry report::historyEntry(Data const& data, int64_t time)
{
  history::Entry entry;
  entry.time = time;
  entry.kind = std::string(kindKey(data.diagnosis.kind));
  entry.culprit = data.diagnosis.culprit;
  auto frame = culpritFrame(data);
  if (frame && !frame->file.empty())
    entry.where = fileName(frame->file) + ":" + std::to_string(frame->line);
  // Without a culprit or a line: the first frame, as module+offset (the same code crashing again)
  if (entry.where.empty())
  {
    auto const* first = frame ? frame : (data.crash.frames.empty() ? nullptr : &data.crash.frames[0]);
    if (first)
      entry.where = frameLine(data, *first);
  }
  entry.signature = entry.kind + "|" + entry.culprit + "|" + entry.where + "|" + analysis::exceptionName(data.crash.code);
  return entry;
}

std::vector<std::string> report::crumbLines(Data const& data, size_t count)
{
  std::vector<std::string> lines;
  auto start = data.crumbs.size() > count ? data.crumbs.size() - count : 0;
  for (auto i = start; i < data.crumbs.size(); i++)
  {
    auto const& crumb = data.crumbs[i];
    char time[24];
    std::snprintf(time, sizeof(time), "%.1f s", crumb.millis / 1000.0);
    lines.push_back(std::string(time) + "  " + crumb.text);
  }
  return lines;
}

std::string report::copyText(Data const& data, Language language)
{
  bool ru = language == Language::Russian;
  auto words = describe(data, language);
  std::string text = "**Crash Doctor** - " + words.title + "\n";
  text += words.explanation + "\n";
  if (auto culprit = culpritLine(data); !culprit.empty())
    text += (ru ? "Мод: " : "Mod: ") + culprit + "\n";
  if (auto where = location(data); !where.empty())
    text += (ru ? "Где: " : "Where: ") + where + "\n";
  text += (ru ? "Исключение: " : "Exception: ") + analysis::exceptionName(data.crash.code);
  if (data.crash.code == analysis::ACCESS_VIOLATION && data.crash.parameters.size() >= 2)
    text += " " + std::string(data.crash.parameters[0] == 1 ? "write" : data.crash.parameters[0] == 8 ? "execute" : "read") +
            " " + hex(data.crash.parameters[1]);
  text += ru ? (data.crash.mainThread ? ", главный поток\n" : ", не главный поток\n")
             : (data.crash.mainThread ? ", main thread\n" : ", not the main thread\n");
  if (!data.diagnosis.evidence.empty())
    text += (ru ? "Признак: " : "Sign: ") + data.diagnosis.evidence + "\n";

  text += ru ? "\nСтек:\n```\n" : "\nStack:\n```\n";
  size_t shown = 0;
  for (size_t i = 0; i < data.crash.frames.size() && shown < 12; i++)
  {
    auto const& frame = data.crash.frames[i];
    // A recursion is a few lines and a count, not 128 of the same
    size_t same = 0;
    while (i + same + 1 < data.crash.frames.size() && data.crash.frames[i + same + 1].functionRva == frame.functionRva &&
           data.crash.frames[i + same + 1].module == frame.module && frame.functionRva)
      same++;
    text += "#" + std::to_string(i) + " " + frameLine(data, frame) + "\n";
    shown++;
    if (same >= 2)
    {
      text += "   ... " + std::to_string(same) + (ru ? " раз ещё\n" : " more times\n");
      i += same;
    }
  }
  text += "```\n";

  if (auto source = sourceLines(data); !source.empty())
  {
    text += "```cpp\n";
    for (auto const& line : source)
      text += line + "\n";
    text += "```\n";
  }

  auto crumbs = crumbLines(data, 10);
  if (!crumbs.empty())
  {
    text += ru ? "Последние действия:\n" : "Last actions:\n";
    for (auto const& line : crumbs)
      text += "- " + line + "\n";
  }

  text += "GD " + data.gameVersion + ", Geode " + data.geodeVersion;
  // Only the mods on the stack, with versions: the whole list doesn't fit a Discord message
  std::vector<std::string> seen;
  for (auto const& frame : data.crash.frames)
  {
    if (frame.module < 0 || frame.module >= static_cast<int>(data.crash.modules.size()))
      continue;
    auto const& id = data.crash.modules[frame.module].modId;
    if (id.empty() || std::find(seen.begin(), seen.end(), id) != seen.end())
      continue;
    seen.push_back(id);
  }
  for (auto const& id : seen)
    for (auto const& mod : data.mods)
      if (mod.id == id)
        text += ", " + id + " " + mod.version;
  text += ru ? ", всего модов: " : ", mods in total: ";
  text += std::to_string(data.mods.size()) + "\n";
  return text;
}

std::string report::scrub(std::string text, std::string_view profile)
{
  if (profile.size() < 4)
    return text;
  // Every spelling of the profile's path: either slash, any case
  std::string lowered = text;
  std::string needle(profile);
  for (auto& c : needle)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)) == '/' ? '\\' : std::tolower(static_cast<unsigned char>(c)));
  for (auto& c : lowered)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)) == '/' ? '\\' : std::tolower(static_cast<unsigned char>(c)));
  constexpr std::string_view replacement = "%USERPROFILE%";
  std::string result;
  size_t from = 0;
  for (size_t at = 0; (at = lowered.find(needle, at)) != std::string::npos; at += needle.size())
  {
    result.append(text, from, at - from);
    result += replacement;
    from = at + needle.size();
  }
  result.append(text, from, std::string::npos);

  // The user name alone, after a Users folder written some other way (8.3 names, other drives)
  auto slash = needle.find_last_of('\\');
  if (slash != std::string::npos && slash + 1 < needle.size())
  {
    auto name = std::string(profile.substr(slash + 1));
    replaceAll(result, "\\Users\\" + name, "\\Users\\%USERNAME%");
    replaceAll(result, "/Users/" + name, "/Users/%USERNAME%");
  }
  return result;
}

std::string report::resultLine(Data const& data)
{
  auto frame = culpritFrame(data);
  std::string line = "{\"diagnosis\":" + jsonString(kindKey(data.diagnosis.kind));
  line += ",\"culprit\":" + jsonString(data.diagnosis.culprit);
  line += ",\"file\":" + jsonString(frame ? frame->file : "");
  line += ",\"line\":" + std::to_string(frame ? frame->line : 0);
  line += ",\"function\":" + jsonString(frame ? frame->function : "");
  line += ",\"hook\":" + jsonString(data.diagnosis.hook);
  line += ",\"exception\":" + jsonString(analysis::exceptionName(data.crash.code));
  line += ",\"thread\":" + jsonString(data.crash.mainThread ? "main" : "other");
  line += ",\"evidence\":" + jsonString(data.diagnosis.evidence);
  line += ",\"frames\":" + std::to_string(data.crash.frames.size());
  line += ",\"crumbs\":" + std::to_string(data.crumbs.size());
  line += ",\"crashlog\":" + jsonString(fileName(data.crashlog));
  return line + "}";
}
