#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include <cstring>
#include <thread>

using namespace geode::prelude;

// Kept as separate functions, so each crash has its own frame and line
#define NOINLINE __declspec(noinline)

// Crashes the game on purpose, one kind at a time, for Crash Doctor's checks.
// The kind is read from geode/config/zhulis.crash-doctor-testbed/crash.txt, which is deleted before the
// crash, so the next start is a normal one. Before crashing it presses two buttons of the main menu:
// the breadcrumbs should have them.
// Each crashing line ends with "// expect: <diagnosis>": the check script takes the expected line from it

namespace
{
  std::string s_kind;
  // Only the recursion's check makes the hook below recurse
  bool s_recursionArmed = false;
  volatile int s_depth = 0;
  // The node the use after free keeps a plain pointer to
  CCNode* s_stale = nullptr;
  std::vector<char*> s_junk;

  std::string readKind()
  {
    auto path = Mod::get()->getConfigDir() / "crash.txt";
    auto text = file::readString(path);
    if (!text)
      return "";
    std::error_code error;
    std::filesystem::remove(path, error);
    return std::string(string::trim(*text));
  }

  void press(CCNode* root, char const* id)
  {
    auto item = typeinfo_cast<CCMenuItem*>(root->getChildByIDRecursive(id));
    if (!item)
    {
      log::warn("No button {} to press", id);
      return;
    }
    log::info("Pressing {}", id);
    item->activate();
  }

  // An object whose base class calls a pure virtual method while it's destroyed: the derived part is
  // gone by then, so the call lands on _purecall
  struct Listener
  {
    virtual ~Listener() { notify(); }
    virtual void onEvent() = 0;
    NOINLINE void notify();
  };

  NOINLINE void Listener::notify()
  {
    onEvent(); // expect: pure-virtual-call
  }

  struct ScoreListener : Listener
  {
    int score = 0;
    void onEvent() override { score++; }
  };

  // The heap reuses the freed node's memory for other data, as the game's allocations would
  void reuseFreedMemory(CCNode* freed)
  {
    static char const text[] = "level name: Stereo Madness; ";
    for (int round = 0; round < 64; round++)
    {
      for (int i = 0; i < 1024; i++)
      {
        auto block = static_cast<char*>(operator new(sizeof(CCNode)));
        for (size_t at = 0; at < sizeof(CCNode); at++)
          block[at] = text[at % (sizeof(text) - 1)];
        s_junk.push_back(block);
      }
      // Reading the freed node here is the bug's own undefined behavior, only to make the check repeatable
      if (std::memcmp(freed, text, 8) == 0)
        return;
    }
  }

  NOINLINE void workerThread()
  {
    // A label made off the main thread, like from a web request's callback
    auto label = CCLabelBMFont::create("Loaded!", "bigFont.fnt"); // expect: wrong-thread
    log::info("The thread made a label: {}", static_cast<void*>(label));
  }
} // namespace

class $modify(TestbedMenuLayer, MenuLayer)
{
  bool init()
  {
    if (!MenuLayer::init())
      return false;
    static bool once = false;
    if (once)
      return true;
    once = true;
    s_kind = readKind();
    if (s_kind.empty())
      return true;
    log::info("Testbed: crashing with {} in a few seconds", s_kind);

    if (s_kind == "use-after-free")
    {
      s_stale = CCNode::create();
      s_stale->setID("testbed-marker");
      this->addChild(s_stale);
    }
    this->scheduleOnce(schedule_selector(TestbedMenuLayer::onPressButtons), 1.5f);
    this->scheduleOnce(schedule_selector(TestbedMenuLayer::onCrash), 3.f);
    return true;
  }

  void onPressButtons(float)
  {
    // Two buttons that open something over the menu, for the breadcrumbs
    press(this, "achievements-button");
    press(CCDirector::get()->getRunningScene(), "settings-button");
  }

  void onMoreGames(CCObject* sender)
  {
    if (!s_recursionArmed)
      return MenuLayer::onMoreGames(sender);
    // The bug: the hook's own name instead of MenuLayer::onMoreGames(sender)
    s_depth = s_depth + 1;
    if (s_depth > 0)
      this->onMoreGames(sender); // expect: hook-recursion
    s_depth = s_depth - 1;
  }

  void onCrash(float)
  {
    log::info("Testbed: crashing now with {}", s_kind);
    if (s_kind == "null")
      crashNull();
    else if (s_kind == "recursion")
    {
      s_recursionArmed = true;
      this->onMoreGames(nullptr);
    }
    else if (s_kind == "use-after-free")
      crashUseAfterFree();
    else if (s_kind == "thread")
      std::thread(workerThread).detach();
    else if (s_kind == "pure-virtual")
      delete static_cast<Listener*>(new ScoreListener());
    else
      log::warn("Unknown crash kind {}", s_kind);
  }

  NOINLINE void crashNull()
  {
    // The node isn't there: getChildByID returns nullptr, and the code doesn't check
    auto missing = this->getChildByID("testbed-missing-node");
    missing->setPosition({10.f, 10.f}); // expect: null-pointer
  }

  NOINLINE void crashUseAfterFree()
  {
    // Removed from the menu, its last reference: the node is deleted, the plain pointer stays
    s_stale->removeFromParent();
    reuseFreedMemory(s_stale);
    s_stale->setPosition({10.f, 10.f}); // expect: use-after-free
  }
};
