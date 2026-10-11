#include "../src/Decode.hpp"
#include "Check.hpp"

#include <vector>

namespace
{
  // Whether the bytes, as the end of some code, end with a call
  bool endsWithCall(std::vector<uint8_t> bytes)
  {
    return decode::callEndsAt(bytes.data() + bytes.size(), bytes.size());
  }

  decode::MemoryCall memoryCall(std::vector<uint8_t> bytes)
  {
    return decode::memoryCall(bytes.data(), bytes.size());
  }
} // namespace

int main()
{
  // ! --- Return addresses --- !

  CHECK(endsWithCall({0x90, 0xE8, 0x10, 0x20, 0x30, 0x40}));     // call rel32
  CHECK(endsWithCall({0x90, 0x90, 0xFF, 0xD0}));                 // call rax
  CHECK(endsWithCall({0x90, 0x41, 0xFF, 0xD3}));                 // call r11
  CHECK(endsWithCall({0x90, 0xFF, 0x50, 0x28}));                 // call [rax+0x28]
  CHECK(endsWithCall({0x90, 0xFF, 0x90, 0x08, 0x01, 0, 0}));     // call [rax+0x108]
  CHECK(endsWithCall({0x90, 0xFF, 0x15, 0x10, 0x20, 0x30, 0}));  // call [rip+disp32]
  CHECK(endsWithCall({0x90, 0xFF, 0x54, 0x24, 0x08}));           // call [rsp+8]
  CHECK(!endsWithCall({0x90, 0x90, 0x90, 0x48, 0x89, 0xC3}));    // mov rbx, rax
  CHECK(!endsWithCall({0x90, 0x90, 0xFF, 0xE0}));                // jmp rax
  CHECK(!endsWithCall({0x90, 0x90, 0xC3}));                      // ret
  CHECK(!endsWithCall({}));

  // ! --- Indirect calls --- !

  {
    // call qword ptr [rax+0x1a8]
    auto call = memoryCall({0xFF, 0x90, 0xA8, 0x01, 0x00, 0x00});
    CHECK(call.ok && !call.jump && call.base == 0 && call.index == -1 && call.displacement == 0x1A8 && call.length == 6);
  }
  {
    // call qword ptr [rax+0x28]
    auto call = memoryCall({0xFF, 0x50, 0x28});
    CHECK(call.ok && call.base == 0 && call.displacement == 0x28 && call.length == 3);
  }
  {
    // call qword ptr [r10+0x10]
    auto call = memoryCall({0x41, 0xFF, 0x52, 0x10});
    CHECK(call.ok && call.base == 10 && call.displacement == 0x10 && call.length == 4);
  }
  {
    // jmp qword ptr [rcx]
    auto call = memoryCall({0xFF, 0x21});
    CHECK(call.ok && call.jump && call.base == 1 && call.displacement == 0 && call.length == 2);
  }
  {
    // call qword ptr [rip+0x1234]: not through a register
    auto call = memoryCall({0xFF, 0x15, 0x34, 0x12, 0x00, 0x00});
    CHECK(call.ok && call.base == -1 && call.length == 6);
  }
  {
    // call qword ptr [rax+rcx*8+0x10]
    auto call = memoryCall({0xFF, 0x54, 0xC8, 0x10});
    CHECK(call.ok && call.base == 0 && call.index == 1 && call.displacement == 0x10 && call.length == 4);
  }
  CHECK(!memoryCall({0xFF, 0xD0}).ok);       // call rax: no memory
  CHECK(!memoryCall({0x48, 0x8B, 0x01}).ok); // mov rax, [rcx]
  CHECK(!memoryCall({0xFF, 0x90, 0x01}).ok); // cut short
  CHECK(!memoryCall({}).ok);

  return finish("decode");
}
