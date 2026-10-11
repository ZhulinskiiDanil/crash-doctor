#include "../src/Analysis.hpp"
#include "../src/Report.hpp"
#include "../src/Session.hpp"
#include "../src/Symbols.hpp"
#include "Check.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

// A damaged session.bin: whatever is in it, reading it, symbols, the analysis and the report's text must not crash.
// Windows only (the file and DbgHelp)

namespace
{
  std::filesystem::path const PATH = std::filesystem::temp_directory_path() / "crash-doctor-session-test.bin";

  void write(session::File const& file, size_t size = sizeof(session::File))
  {
    std::ofstream stream(PATH, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<char const*>(&file), static_cast<std::streamsize>(size));
  }

  void header(session::File& file)
  {
    std::memcpy(file.header.magic, session::MAGIC, sizeof(session::MAGIC));
    file.header.version = session::VERSION;
    file.header.state = session::State::Crashed;
  }
} // namespace

int main()
{
  auto file = std::make_unique<session::File>();

  // ! --- What isn't a crash --- !

  CHECK(!session::takePreviousCrash(PATH.parent_path() / "no-such-crash-doctor-file.bin"));
  header(*file);
  file->header.state = session::State::Running;
  write(*file);
  CHECK(!session::takePreviousCrash(PATH));
  file->header.state = session::State::Crashed;
  write(*file, sizeof(session::File) / 2);
  CHECK(!session::takePreviousCrash(PATH));
  file->header.version = session::VERSION + 1;
  write(*file);
  CHECK(!session::takePreviousCrash(PATH));
  header(*file);
  file->crash.frameCount = session::MAX_FRAMES + 1;
  write(*file);
  CHECK(!session::takePreviousCrash(PATH));
  file->crash.frameCount = 0;
  file->crash.callerSize = 1000;
  write(*file);
  CHECK(!session::takePreviousCrash(PATH));

  // ! --- Garbage in a valid frame --- !

  std::mt19937_64 random(12345);
  int read = 0;
  for (int round = 0; round < 200; round++)
  {
    auto bytes = reinterpret_cast<uint8_t*>(&file->crash);
    for (size_t i = 0; i < sizeof(file->crash); i++)
      bytes[i] = static_cast<uint8_t>(random());
    for (auto& crumb : file->crumbs)
      for (auto& c : crumb.text)
        c = static_cast<char>(random());
    for (auto& c : file->header.mods)
      c = static_cast<char>(random());
    header(*file);
    // Counts in range half of the time, so the rest of the path runs too
    if (round % 2)
    {
      file->crash.frameCount %= session::MAX_FRAMES + 1;
      file->crash.moduleCount %= 8;
      file->crash.stackSize %= session::STACK_BYTES + 1;
      file->crash.codeSize %= session::CODE_BYTES + 1;
      file->crash.callerSize %= 9;
      file->crash.parameterCount %= 16;
    }
    write(*file);
    auto previous = session::takePreviousCrash(PATH);
    if (!previous)
      continue;
    read++;
    report::Data data;
    data.crash = symbols::load(previous->crash, 4);
    data.diagnosis = analysis::diagnose(data.crash);
    data.crumbs = crumbs::read(*previous);
    auto text = report::copyText(data, round % 4 ? report::Language::English : report::Language::Russian);
    auto line = report::resultLine(data);
    CHECK(!text.empty() && !line.empty());
  }
  CHECK(read > 50);
  std::printf("%d of 200 damaged files read and reported\n", read);

  std::filesystem::remove(PATH);
  return finish("session");
}
