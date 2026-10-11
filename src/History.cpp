#include "History.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace
{
  // Tabs and line breaks would break the format: they become spaces
  std::string clean(std::string text)
  {
    for (auto& c : text)
      if (c == '\t' || c == '\n' || c == '\r')
        c = ' ';
    return text;
  }

  std::vector<std::string> split(std::string const& line)
  {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true)
    {
      auto tab = line.find('\t', start);
      parts.push_back(line.substr(start, tab - start));
      if (tab == std::string::npos)
        return parts;
      start = tab + 1;
    }
  }

  template <class T>
  bool number(std::string const& text, T& out)
  {
    auto end = text.data() + text.size();
    auto [at, error] = std::from_chars(text.data(), end, out);
    return error == std::errc() && at == end;
  }
} // namespace

void history::add(std::vector<Entry>& entries, Entry entry)
{
  auto same = std::find_if(entries.begin(), entries.end(), [&](Entry const& old) { return old.signature == entry.signature; });
  if (same != entries.end())
  {
    entry.count = same->count + 1;
    entries.erase(same);
  }
  entries.insert(entries.begin(), std::move(entry));
  if (entries.size() > MAX_ENTRIES)
    entries.resize(MAX_ENTRIES);
}

std::string history::serialize(std::vector<Entry> const& entries)
{
  std::string text;
  for (auto const& entry : entries)
    text += std::to_string(entry.time) + "\t" + std::to_string(entry.count) + "\t" + clean(entry.signature) + "\t" +
            clean(entry.kind) + "\t" + clean(entry.culprit) + "\t" + clean(entry.where) + "\n";
  return text;
}

std::vector<history::Entry> history::parse(std::string const& text)
{
  std::vector<Entry> entries;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line) && entries.size() < MAX_ENTRIES)
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    auto parts = split(line);
    if (parts.size() != 6)
      continue;
    Entry entry;
    if (!number(parts[0], entry.time) || !number(parts[1], entry.count) || entry.count < 1 || parts[2].empty())
      continue;
    entry.signature = parts[2];
    entry.kind = parts[3];
    entry.culprit = parts[4];
    entry.where = parts[5];
    entries.push_back(std::move(entry));
  }
  return entries;
}
