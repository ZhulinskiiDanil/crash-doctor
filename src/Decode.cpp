#include "Decode.hpp"

namespace
{
  // The ModRM reg field of FF: 2 is call, 4 is jmp
  constexpr uint8_t CALL = 2;
  constexpr uint8_t JUMP = 4;

  uint8_t regField(uint8_t modrm) { return (modrm >> 3) & 7; }

  bool isRex(uint8_t byte) { return (byte & 0xF0) == 0x40; }
} // namespace

bool decode::callEndsAt(uint8_t const* end, size_t before) noexcept
{
  // call rel32: E8 xx xx xx xx
  if (before >= 5 && end[-5] == 0xE8)
    return true;

  // FF /2 with a ModRM that makes the instruction exactly this long. The ModRM byte is at end - length + 1
  struct Form
  {
    size_t length;
    uint8_t mod;
    // The r/m field must (or must not) be 4 (a SIB byte follows) or 5 (RIP-relative with mod 0)
    int rm;
  };
  static constexpr Form forms[] = {
      {2, 3, -1}, // call reg
      {2, 0, -1}, // call [reg]
      {3, 1, -1}, // call [reg+disp8]
      {6, 2, -1}, // call [reg+disp32]
      {6, 0, 5},  // call [rip+disp32]
      {3, 0, 4},  // call [base+index*scale]
      {4, 1, 4},  // call [base+index*scale+disp8]
      {7, 2, 4},  // call [base+index*scale+disp32]
  };
  for (auto const& form : forms)
  {
    if (before < form.length)
      continue;
    auto start = end - form.length;
    if (start[0] != 0xFF)
      continue;
    auto modrm = start[1];
    auto mod = modrm >> 6;
    auto rm = modrm & 7;
    if (mod != form.mod || regField(modrm) != CALL)
      continue;
    if (form.rm >= 0 && rm != form.rm)
      continue;
    // Without a SIB or RIP form, r/m 4 means a SIB byte and r/m 5 with mod 0 means RIP: other lengths
    if (form.rm < 0 && mod != 3 && (rm == 4 || (mod == 0 && rm == 5)))
      continue;
    return true;
  }
  return false;
}

decode::MemoryCall decode::memoryCall(uint8_t const* code, size_t size) noexcept
{
  MemoryCall call;
  size_t at = 0;
  uint8_t rex = 0;
  if (at < size && isRex(code[at]))
    rex = code[at++];
  if (at + 2 > size || code[at] != 0xFF)
    return call;
  auto modrm = code[at + 1];
  auto reg = regField(modrm);
  auto mod = modrm >> 6;
  auto rm = modrm & 7;
  if ((reg != CALL && reg != JUMP) || mod == 3)
    return call;
  at += 2;

  if (rm == 4)
  {
    if (at >= size)
      return call;
    auto sib = code[at++];
    auto index = ((sib >> 3) & 7) | ((rex & 0x02) << 2);
    auto base = (sib & 7) | ((rex & 0x01) << 3);
    call.index = index == 4 ? -1 : index;
    // No base with mod 0 and base 5: a disp32 alone
    call.base = (mod == 0 && (sib & 7) == 5) ? -2 : base;
    if (call.base == -2)
    {
      call.base = -1;
      mod = 2;
    }
  }
  else if (mod == 0 && rm == 5)
  {
    // RIP-relative: an import or a function pointer in data, not a vtable
    call.base = -1;
    mod = 2;
  }
  else
  {
    call.base = rm | ((rex & 0x01) << 3);
  }

  if (mod == 1)
  {
    if (at + 1 > size)
      return call;
    call.displacement = static_cast<int8_t>(code[at]);
    at += 1;
  }
  else if (mod == 2)
  {
    if (at + 4 > size)
      return call;
    call.displacement = static_cast<int32_t>(static_cast<uint32_t>(code[at]) | (static_cast<uint32_t>(code[at + 1]) << 8) |
                                             (static_cast<uint32_t>(code[at + 2]) << 16) |
                                             (static_cast<uint32_t>(code[at + 3]) << 24));
    at += 4;
  }

  call.ok = true;
  call.jump = reg == JUMP;
  call.length = at;
  return call;
}
