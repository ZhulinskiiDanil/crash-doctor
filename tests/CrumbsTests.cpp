#include "../src/Crumbs.hpp"
#include "Check.hpp"

#include <memory>
#include <string>

int main()
{
  auto file = std::make_unique<session::File>();
  crumbs::attach(file.get());

  CHECK(crumbs::read(*file).empty());

  crumbs::add("scene %s", "MenuLayer");
  crumbs::add("button %s", "play-button");
  {
    auto list = crumbs::read(*file);
    CHECK(list.size() == 2);
    CHECK(list[0].text == "scene MenuLayer");
    CHECK(list[1].text == "button play-button");
  }

  // Past the ring's size: the oldest go, the order stays
  for (int i = 0; i < 40; i++)
    crumbs::add("key %d", i);
  {
    auto list = crumbs::read(*file);
    CHECK(list.size() == session::MAX_CRUMBS);
    CHECK(list.front().text == "key " + std::to_string(40 - session::MAX_CRUMBS));
    CHECK(list.back().text == "key 39");
    for (size_t i = 1; i < list.size(); i++)
      CHECK(list[i - 1].sequence < list[i].sequence);
  }

  // The same crumb in a row is counted, not repeated
  crumbs::add("key (text)");
  crumbs::add("key (text)");
  crumbs::add("key (text)");
  {
    auto list = crumbs::read(*file);
    CHECK(list.back().text == "key (text) (x3)");
    CHECK(list[list.size() - 2].text == "key 39");
  }
  crumbs::add("key Escape");
  CHECK(crumbs::read(*file).back().text == "key Escape");

  // A long text is cut, not overflowing its slot
  crumbs::add("%s", std::string(500, 'x').c_str());
  CHECK(crumbs::read(*file).back().text.size() == session::CRUMB_TEXT - 1);

  return finish("crumbs");
}
