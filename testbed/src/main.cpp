#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

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
    int notified = 0;
  };

  NOINLINE void Listener::notify()
  {
    onEvent(); // expect: pure-virtual-call
    notified++;
  }

  struct ScoreListener : Listener
  {
    int score = 0;
    void onEvent() override { score++; }
  };

  NOINLINE void workerThread()
  {
    // UI changed off the main thread, like in a web request's callback: a node that isn't there (another
    // thread's work, not done yet) added to the scene. cocos isn't thread-safe and doesn't check: the
    // crash is inside it, on this thread
    auto scene = CCDirector::get()->getRunningScene();
    auto badge = scene->getChildByID("testbed-badge-not-made-yet");
    scene->addChild(badge); // expect: wrong-thread
    log::info("The thread added {}", static_cast<void*>(badge));
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
    // Timed from a thread, not the layer's scheduler: the settings the buttons open pause the menu
    Ref<TestbedMenuLayer> layer = this;
    std::thread([layer] {
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      queueInMainThread([layer] { layer->onPressButtons(0.f); });
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      queueInMainThread([layer] { layer->onCrash(0.f); });
    }).detach();
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
    // The destructor leaves the base class' vtable in the memory: a virtual call past its end lands nowhere
    s_stale->removeFromParent();
    s_stale->setPosition({10.f, 10.f}); // expect: use-after-free
  }
};
