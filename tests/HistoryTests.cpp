#include "../src/History.hpp"
#include "Check.hpp"

using namespace history;

namespace
{
  Entry crash(char const* signature, int64_t time)
  {
    Entry entry;
    entry.time = time;
    entry.signature = signature;
    entry.kind = "null-pointer";
    entry.culprit = "zhulis.crash-doctor-testbed";
    entry.where = "main.cpp:42";
    return entry;
  }
} // namespace

int main()
{
  // ! --- Grouping --- !

  std::vector<Entry> entries;
  add(entries, crash("a", 1));
  add(entries, crash("b", 2));
  add(entries, crash("a", 3));
  CHECK(entries.size() == 2);
  CHECK(entries[0].signature == "a" && entries[0].count == 2 && entries[0].time == 3);
  CHECK(entries[1].signature == "b" && entries[1].count == 1);

  // At most MAX_ENTRIES, the oldest go
  for (int i = 0; i < 30; i++)
    add(entries, crash(("x" + std::to_string(i)).c_str(), 10 + i));
  CHECK(entries.size() == MAX_ENTRIES);
  CHECK(entries.front().signature == "x29");

  // ! --- The file --- !

  {
    std::vector<Entry> list;
    add(list, crash("a", 100));
    add(list, crash("a", 200));
    auto odd = crash("b\twith\ttabs", 300);
    odd.where = "line\nbreak";
    add(list, odd);
    auto back = parse(serialize(list));
    CHECK(back.size() == 2);
    CHECK(back[0].signature == "b with tabs" && back[0].where == "line break");
    CHECK(back[1].count == 2 && back[1].time == 200 && back[1].culprit == "zhulis.crash-doctor-testbed");
  }

  // A damaged file loses only the damaged lines
  {
    auto back = parse("garbage\n1\t2\tsig\tkind\tmod\twhere\r\n\t\t\t\t\t\nx\t1\ts\tk\tm\tw\n5\t0\ts\tk\tm\tw\n");
    CHECK(back.size() == 1);
    CHECK(back[0].signature == "sig" && back[0].where == "where" && back[0].count == 2);
  }
  CHECK(parse("").empty());
  CHECK(parse(std::string(1000, '\0')).empty());

  return finish("history");
}
