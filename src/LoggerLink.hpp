#pragma once

// With Improved Logger installed: the last actions pinned in its console as they happen, and a marker
// where the report of a crash is logged. Without it, nothing
namespace logger_link
{
  void setup();

  // A marker line in its console, before the crash's report in the log
  void markReport();
} // namespace logger_link
