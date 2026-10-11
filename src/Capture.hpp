#pragma once

// The code that runs at the moment of a crash. Windows only, no Geode: nothing in it allocates, locks,
// logs or touches cocos. It writes into the mapped session file and hands the crash on to the filter
// that was there before (Geode's), which writes its crashlog as always
#include "SessionFile.hpp"

namespace capture
{
  // Starts catching crashes into `file`: an unhandled exception filter chained in front of the current
  // one, a `_purecall` handler, and stack room for the handler on the calling thread (the main one)
  void install(session::File* file);

  // Another filter installed after Crash Doctor's never calls it: when one did, Crash Doctor's goes back in
  // front and calls it next. Returns that filter's address, or null when Crash Doctor's was still in front
  void const* reclaim();

  // The stack room for a stack overflow's handler, on the calling thread
  void reserveStack();
} // namespace capture
