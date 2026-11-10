#include "SettingsBridge.hpp"

#include <Geode/utils/string.hpp>
#include <algorithm>
#include <array>
#include <filesystem>

namespace na_frame_counter::core {

namespace {

RGBColor toRGB(cocos2d::ccColor3B c) {
    return {c.r, c.g, c.b};
}

constexpr std::array<std::string_view, 33> kAllKeys = {{
    "enabled",
    "measure-clicks",
    "measure-releases",
    "maximum-window-frames",
    "ignore-harmless-clicks",
    "show-over-limit",
    "show-rings",
    "label-placement",
    "color-preset",
    "color-1f",
    "color-2f",
    "color-3f",
    "color-4f",
    "color-5p",
    "circle-radius",
    "circle-thickness",
    "marker-scale",
    "marker-fade-seconds",
    "scale-with-zoom",
    "distinguish-release",
    "ding-sounds",
    "sound-style",
    "custom-sound-file",
    "sound-volume",
    "sync-gd-sfx",
    "min-pitch",
    "max-pitch",
    "pitch-curve",
    "sound-max-frames",
    "show-counters",
    "hud-corner",
    "bucket-order",
    "show-last-badge",
}};

constexpr std::array<std::string_view, 18> kNandlKeys = {{
    "show-precision-hud",
    "nandl-model",
    "nandl-show-base",
    "nandl-show-nerve",
    "nandl-show-fatigue",
    "nandl-show-cps",
    "nandl-show-nf",
    "nandl-show-nc",
    "nandl-show-fc",
    "nandl-show-nfc",
    "show-pass-prob",
    "show-expected-time",
    "macro-fps",
    "respawn-time",
    "target-hours",
    "nandl-kt",
    "nandl-ku",
    "nandl-kc",
}};

} // namespace

ModSettingsSnapshot SettingsBridge::read() {
    ModSettingsSnapshot s;
    auto* mod = geode::Mod::get();
    if (!mod) return s;

    s.enabled = mod->getSettingValue<bool>("enabled");
    s.measureClicks = mod->getSettingValue<bool>("measure-clicks");
    s.measureReleases = mod->getSettingValue<bool>("measure-releases");
    s.maxWindowFrames = parseMaxWindowPreset(mod->getSettingValue<std::string>("maximum-window-frames"));
    s.ignoreHarmlessClicks = mod->getSettingValue<bool>("ignore-harmless-clicks");
    s.showOverLimit = mod->getSettingValue<bool>("show-over-limit");

    s.showRings = mod->getSettingValue<bool>("show-rings");
    s.labelPlacement = parseLabelPlacement(mod->getSettingValue<std::string>("label-placement"));
    s.colorPreset = parseColorPreset(mod->getSettingValue<std::string>("color-preset"));
    s.custom1F = toRGB(mod->getSettingValue<cocos2d::ccColor3B>("color-1f"));
    s.custom2F = toRGB(mod->getSettingValue<cocos2d::ccColor3B>("color-2f"));
    s.custom3F = toRGB(mod->getSettingValue<cocos2d::ccColor3B>("color-3f"));
    s.custom4F = toRGB(mod->getSettingValue<cocos2d::ccColor3B>("color-4f"));
    s.custom5P = toRGB(mod->getSettingValue<cocos2d::ccColor3B>("color-5p"));
    s.circleRadius = static_cast<float>(mod->getSettingValue<double>("circle-radius"));
    s.circleThickness = static_cast<float>(mod->getSettingValue<double>("circle-thickness"));
    s.markerScale = static_cast<float>(mod->getSettingValue<double>("marker-scale"));
    s.markerFadeSeconds = static_cast<float>(mod->getSettingValue<double>("marker-fade-seconds"));
    s.scaleWithZoom = mod->getSettingValue<bool>("scale-with-zoom");
    s.distinguishRelease = mod->getSettingValue<bool>("distinguish-release");

    s.dingSounds = mod->getSettingValue<bool>("ding-sounds");
    s.soundStyle = parseSoundStyle(mod->getSettingValue<std::string>("sound-style"));
    auto customPath = mod->getSettingValue<std::filesystem::path>("custom-sound-file");
    s.customSoundFile = geode::utils::string::pathToString(customPath);
    s.soundVolume = static_cast<float>(mod->getSettingValue<double>("sound-volume"));
    s.syncGdSfx = mod->getSettingValue<bool>("sync-gd-sfx");
    s.minPitch = static_cast<float>(mod->getSettingValue<double>("min-pitch"));
    s.maxPitch = static_cast<float>(mod->getSettingValue<double>("max-pitch"));
    s.pitchCurve = parsePitchCurve(mod->getSettingValue<std::string>("pitch-curve"));
    s.soundMaxFrames = static_cast<int>(mod->getSettingValue<std::int64_t>("sound-max-frames"));

    s.showCounters = mod->getSettingValue<bool>("show-counters");
    s.hudCorner = parseHudCorner(mod->getSettingValue<std::string>("hud-corner"));
    s.oneAtBottom = mod->getSettingValue<std::string>("bucket-order") != "1 at Top";
    s.showLastBadge = mod->getSettingValue<bool>("show-last-badge");
    s.showPrecisionHud = mod->getSettingValue<bool>("show-precision-hud");
    s.nandlDisplay = parseNandlDisplayMode(mod->getSettingValue<std::string>("nandl-model"));
    s.nandlCustom.base = mod->getSettingValue<bool>("nandl-show-base");
    s.nandlCustom.nerve = mod->getSettingValue<bool>("nandl-show-nerve");
    s.nandlCustom.fatigue = mod->getSettingValue<bool>("nandl-show-fatigue");
    s.nandlCustom.cps = mod->getSettingValue<bool>("nandl-show-cps");
    s.nandlCustom.nerveFatigue = mod->getSettingValue<bool>("nandl-show-nf");
    s.nandlCustom.nerveCps = mod->getSettingValue<bool>("nandl-show-nc");
    s.nandlCustom.fatigueCps = mod->getSettingValue<bool>("nandl-show-fc");
    s.nandlCustom.nfc = mod->getSettingValue<bool>("nandl-show-nfc");
    s.showPassProb = mod->getSettingValue<bool>("show-pass-prob");
    s.showExpectedTime = mod->getSettingValue<bool>("show-expected-time");

    s.nandlParams.macroFps = std::clamp(mod->getSettingValue<double>("macro-fps"), 60.0, 960.0);
    s.nandlParams.respawnTime = std::clamp(mod->getSettingValue<double>("respawn-time"), 0.0, 5.0);
    s.nandlParams.targetTime = std::clamp(mod->getSettingValue<double>("target-hours"), 0.5, 168.0) * 3600.0;
    s.nandlParams.kT = std::clamp(mod->getSettingValue<double>("nandl-kt"), 0.0, 0.05);
    s.nandlParams.kU = std::clamp(mod->getSettingValue<double>("nandl-ku"), 0.0, 0.05);
    s.nandlParams.kC = std::clamp(mod->getSettingValue<double>("nandl-kc"), 0.0, 2.0);

    return s;
}

void SettingsBridge::setBool(std::string_view key, bool val) {
    if (auto* mod = geode::Mod::get()) {
        mod->setSettingValue<bool>(key, val);
    }
}

void SettingsBridge::setInt(std::string_view key, std::int64_t val) {
    if (auto* mod = geode::Mod::get()) {
        mod->setSettingValue<std::int64_t>(key, val);
    }
}

void SettingsBridge::setFloat(std::string_view key, double val) {
    if (auto* mod = geode::Mod::get()) {
        mod->setSettingValue<double>(key, val);
    }
}

void SettingsBridge::setString(std::string_view key, std::string const& val) {
    if (auto* mod = geode::Mod::get()) {
        mod->setSettingValue<std::string>(key, val);
    }
}

void SettingsBridge::setColor(std::string_view key, cocos2d::ccColor3B val) {
    if (auto* mod = geode::Mod::get()) {
        mod->setSettingValue<cocos2d::ccColor3B>(key, val);
    }
}

void SettingsBridge::resetKeys(std::initializer_list<std::string_view> keys) {
    auto* mod = geode::Mod::get();
    if (!mod) return;
    for (auto key : keys) {
        if (auto sett = mod->getSetting(key)) {
            sett->reset();
        }
    }
}

void SettingsBridge::resetAll() {
    auto* mod = geode::Mod::get();
    if (!mod) return;
    for (auto key : kAllKeys) {
        if (auto sett = mod->getSetting(key)) {
            sett->reset();
        }
    }
    for (auto key : kNandlKeys) {
        if (auto sett = mod->getSetting(key)) {
            sett->reset();
        }
    }
}

}
