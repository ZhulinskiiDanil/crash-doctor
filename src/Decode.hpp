#pragma once

// Just enough x64 decoding for crashes: whether a call ends right before an address (a return address),
// and the operand of an indirect call through memory (a virtual call). Pure functions on bytes, no
// Windows or Geode: the crash handler uses the first one, the analysis the second, the tests both
#include <cstddef>
#include <cstdint>

namespace decode
{
  // Whether one of the usual call encodings ends exactly at `end`: `call rel32`, `call reg`,
  // `call [reg]`, `call [reg+disp8]`, `call [reg+disp32]`, `call [rip+disp32]`, with or without REX.
  // `before` is how many bytes before `end` can be read
  bool callEndsAt(uint8_t const* end, size_t before) noexcept;

  struct MemoryCall
  {
    bool ok = false;
    // `jmp` rather than `call` (a tail call)
    bool jump = false;
    // The register holding the table's address (0 = RAX ... 15 = R15), -1 for RIP-relative
    int base = -1;
    // A SIB index register, -1 when there's none
    int index = -1;
    int32_t displacement = 0;
    // How long the instruction is
    size_t length = 0;
  };

  // `call`/`jmp` through memory, `FF /2` or `FF /4`, at `code`. A virtual call is `call [reg+disp]`
  // with the object's vtable in the register
  MemoryCall memoryCall(uint8_t const* code, size_t size) noexcept;
} // namespace decode
