#include "Crumbs.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
  session::File* s_file = nullptr;
  uint64_t s_next = 0;
  std::chrono::steady_clock::time_point s_start;
  // The newest crumb's text without its count, and how many times in a row it came
  char s_last[session::CRUMB_TEXT];
  uint32_t s_repeats = 0;
} // namespace

void crumbs::attach(session::File* file)
{
  s_file = file;
  s_start = std::chrono::steady_clock::now();
}

void crumbs::add(char const* format, ...)
{
  if (!s_file)
    return;
  char text[session::CRUMB_TEXT];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(text, sizeof(text), format, arguments);
  va_end(arguments);

  // The same again (typing, a button pressed over and over): the newest crumb counts it instead
  bool again = s_next && std::strcmp(text, s_last) == 0;
  auto sequence = again ? s_next : ++s_next;
  auto& slot = s_file->crumbs[(sequence - 1) % session::MAX_CRUMBS];
  // Marked empty while it's rewritten: a crash in between leaves no half crumb
  slot.sequence = 0;
  slot.millis = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_start).count());
  if (again)
    std::snprintf(slot.text, sizeof(slot.text), "%s (x%u)", text, ++s_repeats + 1);
  else
  {
    std::memcpy(slot.text, text, sizeof(text));
    std::memcpy(s_last, text, sizeof(text));
    s_repeats = 0;
  }
  slot.sequence = sequence;
}

std::vector<crumbs::Crumb> crumbs::read(session::File const& file)
{
  std::vector<Crumb> result;
  for (auto const& slot : file.crumbs)
  {
    if (!slot.sequence)
      continue;
    std::string text(slot.text, strnlen(slot.text, sizeof(slot.text)));
    result.push_back({slot.sequence, slot.millis, std::move(text)});
  }
  std::sort(result.begin(), result.end(), [](Crumb const& a, Crumb const& b) { return a.sequence < b.sequence; });
  return result;
}
