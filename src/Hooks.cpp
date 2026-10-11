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

  // A key's name, unless it types text: letters and digits are only named with Ctrl or Alt held, so a
  // password typed in the game never ends up in a copied report
  std::string keyName(enumKeyCodes key, CCKeyboardDispatcher* dispatcher)
  {
    bool control = dispatcher->getControlKeyPressed();
    bool alt = dispatcher->getAltKeyPressed();
    bool text = (key >= KEY_A && key <= KEY_Z) || (key >= KEY_Zero && key <= KEY_Nine) ||
                (key >= KEY_NumPad0 && key <= KEY_NumPad9) || key == KEY_Space || key == KEY_OEMComma ||
                key == KEY_OEMPeriod || key == KEY_OEMMinus || key == KEY_OEMPlus;
    if (text && !control && !alt)
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
    if (down && !repeat)
    {
      auto name = keyName(key, this);
      if (name.empty())
        crumbs::add("key (text)");
      else
        crumbs::add("key %s", name.c_str());
    }
    return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, time);
  }
};
