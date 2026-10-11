#include "Crumbs.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/CCMenuItem.hpp>
#include <Geode/modify/FLAlertLayer.hpp>

using namespace geode::prelude;

// The breadcrumbs: scenes, popups, buttons and keys. Each hook does one snprintf into the ring and
// passes the call on; nothing here runs every frame

namespace
{
  // "MenuLayer" for a scene: the class of its first child, which is the layer that makes the scene
  std::string sceneName(CCScene* scene)
  {
    if (!scene)
      return "nothing";
    if (auto transition = typeinfo_cast<CCTransitionScene*>(scene); transition && transition->m_pInScene)
      scene = transition->m_pInScene;
    auto layer = scene->getChildrenCount() ? static_cast<CCNode*>(scene->getChildren()->objectAtIndex(0)) : nullptr;
    return layer ? std::string(getObjectName(layer)) : std::string(getObjectName(scene));
  }

  // The class without its namespace, "cocos2d::CCMenuItemSprite" -> "CCMenuItemSprite"
  std::string shortName(CCObject* object)
  {
    if (!object)
      return "?";
    auto name = std::string(getObjectName(object));
    auto colons = name.rfind("::");
    return colons == std::string::npos ? name : name.substr(colons + 2);
  }

  // Keys that never type text: these are named
  bool isControlKey(enumKeyCodes key)
  {
    switch (key)
    {
    case KEY_Escape:
    case KEY_Enter:
    case KEY_Tab:
    case KEY_Backspace:
    case KEY_Delete:
    case KEY_Insert:
    case KEY_Home:
    case KEY_End:
    case KEY_PageUp:
    case KEY_PageDown:
    case KEY_Left:
    case KEY_Right:
    case KEY_Up:
    case KEY_Down:
      return true;
    default:
      return key >= KEY_F1 && key <= KEY_F12;
    }
  }

  // A key's name, or empty when it may type text: only keys that never do are named, and the rest only with
  // Ctrl held (a shortcut). Ctrl+Alt is AltGr, which types characters. So a password typed in the game never
  // ends up in a copied report, not its letters, its punctuation or its length
  std::string keyName(enumKeyCodes key, CCKeyboardDispatcher* dispatcher)
  {
    bool control = dispatcher->getControlKeyPressed();
    bool alt = dispatcher->getAltKeyPressed();
    if (!isControlKey(key) && (!control || alt))
      return "";
    std::string name = control ? "Ctrl+" : "";
    if (alt)
      name += "Alt+";
    if (dispatcher->getShiftKeyPressed())
      name += "Shift+";
    auto label = dispatcher->keyToString(key);
    return name + (label ? label : "?");
  }
} // namespace

class $modify(CrumbDirector, CCDirector)
{
  bool replaceScene(CCScene* scene)
  {
    crumbs::add("scene %s", sceneName(scene).c_str());
    return CCDirector::replaceScene(scene);
  }

  bool pushScene(CCScene* scene)
  {
    crumbs::add("scene %s (pushed)", sceneName(scene).c_str());
    return CCDirector::pushScene(scene);
  }

  void popScene()
  {
    crumbs::add("scene popped");
    CCDirector::popScene();
  }
};

class $modify(CrumbAlert, FLAlertLayer)
{
  void show()
  {
    crumbs::add("popup %s", shortName(this).c_str());
    FLAlertLayer::show();
  }
};

class $modify(CrumbMenuItem, CCMenuItem)
{
  void activate()
  {
    // The button's ID (Node IDs gives most of the game's), its class and whose callback it calls
    auto id = std::string(this->getID());
    crumbs::add("button %s (%s) in %s", id.empty() ? "<no ID>" : id.c_str(), shortName(this).c_str(),
                shortName(m_pListener).c_str());
    CCMenuItem::activate();
  }
};

class $modify(CrumbKeyboard, CCKeyboardDispatcher)
{
  bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double time)
  {
    // A modifier alone is part of the next key's name
    bool modifier = key == KEY_Control || key == KEY_Shift || key == KEY_Alt || key == KEY_LeftControl ||
                    key == KEY_RightContol || key == KEY_LeftShift || key == KEY_RightShift || key == KEY_LeftWindowsKey ||
                    key == KEY_RightWindowsKey;
    if (down && !repeat && !modifier)
    {
      auto name = keyName(key, this);
      if (name.empty())
        crumbs::addUncounted("typing");
      else
        crumbs::add("key %s", name.c_str());
    }
    return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, time);
  }
};
