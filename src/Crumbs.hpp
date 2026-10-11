#pragma once

// Breadcrumbs: the last actions before a crash, written straight into the mapped session file.
// A crumb costs one snprintf into a slot of the ring, no allocation and no system call: the file's
// pages are written out by Windows, also after the process died. No Geode in it
#include "SessionFile.hpp"

#include <string>
#include <vector>

namespace crumbs
{
  // From now on crumbs go into `file`
  void attach(session::File* file);

  // Called with each new crumb's text, after it's written (Improved Logger's console shows them)
  void listen(void (*listener)(char const* text));

  // A crumb, printf-style. Main thread only (all the hooks that leave crumbs run there)
  void add(char const* format, ...);

  struct Crumb
  {
    uint64_t sequence;
    uint64_t millis;
    std::string text;
  };

  // The crumbs of a session file, oldest first
  std::vector<Crumb> read(session::File const& file);
} // namespace crumbs
