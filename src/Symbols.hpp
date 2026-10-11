#pragma once

// Turning the crash's raw record into analysis::Crash: the modules classified, the frames named from
// PDBs with DbgHelp. Done at the start after a crash, never at the moment of one. Windows only
#include "Analysis.hpp"
#include "SessionFile.hpp"

namespace symbols
{
  // The record as the analysis takes it: modules, frames, registers, code. Function names, files and
  // lines are filled for the top `named` frames from the modules' PDBs, for modules whose file on disk
  // is still the one that crashed
  analysis::Crash load(session::CrashRecord const& record, size_t named = 48);

  // UTF-16 path from the record to UTF-8
  std::string narrow(uint16_t const* text, size_t capacity);
} // namespace symbols
