#include "audio/SoundPlayer.hpp"
#include "engine/ProbeController.hpp"
#include "ui/ModMenuPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    na_frame_counter::engine::ProbeController::get().onSettingsChanged();

    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) {
        na_frame_counter::engine::ProbeController::get().onSettingsChanged();
    });

    ButtonSettingPressedEventV3(Mod::get(), "open-mod-menu").listen([](std::string_view) {
        na_frame_counter::ui::ModMenuPopup::openOrFocus();
    }).leak();

    listenForKeybindSettingPresses("open-menu-keybind", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        if (auto* scene = cocos2d::CCDirector::sharedDirector()->getRunningScene()) {
            if (auto* existing = typeinfo_cast<na_frame_counter::ui::ModMenuPopup*>(
                scene->getChildByID("minlol12.na-frame-counter/mod-menu-popup")
            )) {
                existing->closePopup();
                return;
            }
        }
        na_frame_counter::ui::ModMenuPopup::openOrFocus();
    });
}
