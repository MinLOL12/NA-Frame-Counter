#include "ModMenuPopup.hpp"
#include "../audio/SoundPlayer.hpp"
#include "../core/FrameConfig.hpp"
#include "../core/SettingsBridge.hpp"
#include "../engine/ProbeController.hpp"

#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>

using namespace geode::prelude;
using namespace cocos2d;

namespace na_frame_counter::ui {

namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 296.f;
constexpr float kCardW = 408.f;
constexpr float kCardH = 172.f;
constexpr float kListW = 390.f;
constexpr float kListH = 164.f;
constexpr char const* kPopupID = "minlol12.na-frame-counter/mod-menu-popup";

void drawPreviewMarker(
    CCNode* parent,
    CCPoint center,
    std::uint64_t frames,
    bool isRelease,
    core::ModSettingsSnapshot const& cfg
) {
    auto* holder = CCNode::create();
    holder->setPosition(center);
    const float uiScale = std::clamp(cfg.markerScale, 0.5f, 1.35f);
    holder->setScale(uiScale);

    auto* ring = CCDrawNode::create();
    const auto rgb = core::colorForWindow(frames, cfg);
    const ccColor4F clear{0.f, 0.f, 0.f, 0.f};
    const ccColor4F black{0.f, 0.f, 0.f, 1.f};
    const ccColor4F colored{rgb.r / 255.f, rgb.g / 255.f, rgb.b / 255.f, 1.f};
    const float r = std::clamp(cfg.circleRadius, 5.f, 18.f);
    const float b = std::clamp(cfg.circleThickness, 1.f, 4.f);

    ring->drawCircle({0.f, 0.f}, r, clear, b + 2.0f, black, 72);
    ring->drawCircle({0.f, 0.f}, r, clear, b, colored, 72);
    if (isRelease && cfg.distinguishRelease) {
        ring->drawCircle({0.f, 0.f}, std::max(3.f, r * 0.62f), clear, 1.0f, colored, 48);
    }
    holder->addChild(ring);

    const auto text = core::formatMarkerText(frames, isRelease, false, cfg.maxWindowFrames, cfg.distinguishRelease);
    if (auto* lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt")) {
        const float offset = r + b + 3.0f;
        float scale = 0.40f;
        switch (cfg.labelPlacement) {
            case core::LabelPlacement::Center:
                lbl->setAnchorPoint({0.5f, 0.5f});
                lbl->setPosition({0.f, 0.f});
                scale = std::clamp(r / 26.f, 0.28f, 0.48f);
                break;
            case core::LabelPlacement::Top:
                lbl->setAnchorPoint({0.5f, 0.f});
                lbl->setPosition({0.f, offset});
                break;
            case core::LabelPlacement::Right:
                lbl->setAnchorPoint({0.f, 0.5f});
                lbl->setPosition({offset, 0.f});
                break;
            case core::LabelPlacement::Left:
            default:
                lbl->setAnchorPoint({1.f, 0.5f});
                lbl->setPosition({-offset, 0.f});
                break;
        }
        lbl->setScale(scale);
        lbl->setColor(ccc3(rgb.r, rgb.g, rgb.b));
        holder->addChild(lbl, 1);
    }

    parent->addChild(holder);
}

} // namespace

ModMenuPopup* ModMenuPopup::create() {
    auto* ret = new ModMenuPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_RELEASE(ret);
    return nullptr;
}

ModMenuPopup* ModMenuPopup::openOrFocus() {
    if (auto* scene = CCDirector::sharedDirector()->getRunningScene()) {
        if (auto* existing = typeinfo_cast<ModMenuPopup*>(scene->getChildByID(kPopupID))) {
            if (!existing->m_closed) {
                return existing;
            }
        }
    }
    if (auto* popup = ModMenuPopup::create()) {
        popup->setID(kPopupID);
        popup->show();
        return popup;
    }
    return nullptr;
}

bool ModMenuPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) {
        return false;
    }

    geode::WeakRef<ModMenuPopup> weakSelf(this);

    this->setTitle("NA Frame Counter", "goldFont.fnt", 0.68f, 18.f);

    const bool platformer = engine::ProbeController::get().isPlatformer();
    const char* subtitleText = platformer
        ? "Platformer mode detected (live probing pauses until classic level)"
        : "240Hz Click & Release Windows  |  NaN GD Visuals, Audio & NaNDL L*";
    if (auto* sub = CCLabelBMFont::create(subtitleText, "chatFont.fnt")) {
        sub->setPosition({kPopupW * 0.5f, kPopupH - 36.f});
        sub->setScale(0.62f);
        sub->setColor(platformer ? ccc3(255, 195, 95) : ccc3(215, 225, 245));
        m_mainLayer->addChild(sub);
    }

    buildTabBar();

    auto* card = NineSlice::create("square02b_001.png");
    card->setContentSize({kCardW, kCardH});
    card->setColor({0, 0, 0});
    card->setOpacity(95);
    card->setPosition({kPopupW * 0.5f, 134.f});
    m_mainLayer->addChild(card);
    m_contentContainer = card;

    m_scroll = ScrollLayer::create({kListW, kListH}, true, true);
    m_scroll->setPosition({4.f, 4.f});
    card->addChild(m_scroll, 1);

    m_scrollbar = Scrollbar::create(m_scroll);
    m_scrollbar->setPosition({kCardW - 6.f, kCardH * 0.5f});
    card->addChild(m_scrollbar, 2);

    // Bottom footer stats & buttons
    m_statsLabel = CCLabelBMFont::create("Att: 0  |  1F: 0  |  Inputs: 0  |  Best L*: 0.00", "chatFont.fnt");
    if (m_statsLabel) {
        m_statsLabel->setAnchorPoint({0.f, 0.5f});
        m_statsLabel->setPosition({16.f, 25.f});
        m_statsLabel->setScale(0.64f);
        m_statsLabel->setColor({210, 235, 255});
        m_mainLayer->addChild(m_statsLabel);
        refreshStatsFooter();
    }

    auto* footerMenu = CCMenu::create();
    footerMenu->setPosition({0.f, 0.f});
    footerMenu->setContentSize({kPopupW, kPopupH});
    m_mainLayer->addChild(footerMenu, 5);

    auto* settingsSpr = ButtonSprite::create("Geode Settings", 92, true, "goldFont.fnt", "GJ_button_01.png", 26.f, 0.42f);
    auto* settingsBtn = CCMenuItemExt::createSpriteExtra(settingsSpr, [](CCMenuItemSpriteExtra*) {
        if (auto* mod = Mod::get()) {
            geode::openSettingsPopup(mod);
        }
    });
    settingsBtn->setPosition({248.f, 25.f});
    footerMenu->addChild(settingsBtn);

    auto* resetSpr = ButtonSprite::create("Reset All", 62, true, "goldFont.fnt", "GJ_button_06.png", 26.f, 0.42f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr, [weakSelf](CCMenuItemSpriteExtra*) {
        geode::createQuickPopup(
            "Reset NA Frame Counter",
            "Reset all <cy>NA Frame Counter</c> settings back to their defaults?",
            "Cancel",
            "Reset",
            [weakSelf](FLAlertLayer*, bool confirmed) {
                if (!confirmed) return;
                auto& ctrl = engine::ProbeController::get();
                ctrl.setSuppressSettingsChanged(true);
                core::SettingsBridge::resetAll();
                ctrl.setSuppressSettingsChanged(false);
                ctrl.onSettingsChanged();
                if (auto self = weakSelf.lock(); self && !self->m_closed) {
                    self->rebuildCurrentTab();
                }
            }
        );
    });
    resetBtn->setPosition({336.f, 25.f});
    footerMenu->addChild(resetBtn);

    auto* doneSpr = ButtonSprite::create("Done", 48, true, "goldFont.fnt", "GJ_button_01.png", 26.f, 0.44f);
    auto* doneBtn = CCMenuItemExt::createSpriteExtra(doneSpr, [weakSelf](CCMenuItemSpriteExtra*) {
        if (auto self = weakSelf.lock(); self && !self->m_closed) {
            self->onClose(nullptr);
        }
    });
    doneBtn->setPosition({400.f, 25.f});
    footerMenu->addChild(doneBtn);

    selectTab(Tab::Engine);
    return true;
}

void ModMenuPopup::onClose(CCObject* sender) {
    if (m_closed) return;
    m_closed = true;
    m_previewBox = nullptr;
    m_statsLabel = nullptr;
    m_scroll = nullptr;
    m_scrollbar = nullptr;
    m_tabBar = nullptr;
    m_contentContainer = nullptr;
    m_tabButtons.clear();
    Popup::onClose(sender);
}

void ModMenuPopup::buildTabBar() {
    if (m_tabBar) return;
    m_tabButtons.clear();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kPopupW, kPopupH});
    m_mainLayer->addChild(menu, 4);
    m_tabBar = menu;

    struct TabDef {
        Tab tab;
        char const* label;
        float x;
    };
    const TabDef defs[] = {
        {Tab::Engine, "Engine", 66.f},
        {Tab::Visuals, "Visuals", 164.f},
        {Tab::Audio, "Audio", 262.f},
        {Tab::HudNandl, "HUD & NaNDL", 368.f},
    };

    geode::WeakRef<ModMenuPopup> weakSelf(this);

    for (auto const& d : defs) {
        const bool active = (m_activeTab == d.tab);
        const char* bg = active ? "GJ_button_01.png" : "GJ_button_04.png";
        const float w = (d.tab == Tab::HudNandl) ? 102.f : 86.f;
        auto* spr = ButtonSprite::create(d.label, static_cast<int>(w), true, "bigFont.fnt", bg, 24.f, 0.40f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr, [weakSelf, tab = d.tab](CCMenuItemSpriteExtra*) {
            if (auto self = weakSelf.lock(); self && !self->m_closed) {
                self->selectTab(tab);
            }
        });
        btn->setPosition({d.x, 234.f});
        menu->addChild(btn);
        m_tabButtons.push_back({d.tab, d.label, w, btn});
    }
}

void ModMenuPopup::updateTabBarVisuals() {
    for (auto const& entry : m_tabButtons) {
        if (!entry.button) continue;
        const bool active = (m_activeTab == entry.tab);
        const char* bg = active ? "GJ_button_01.png" : "GJ_button_04.png";
        if (auto* spr = ButtonSprite::create(
            entry.label, static_cast<int>(entry.width), true, "bigFont.fnt", bg, 24.f, 0.40f
        )) {
            entry.button->setSprite(spr);
        }
    }
}

void ModMenuPopup::selectTab(Tab tab) {
    if (m_closed) return;
    m_activeTab = tab;
    updateTabBarVisuals();
    rebuildCurrentTab();
}

void ModMenuPopup::refreshStatsFooter() {
    if (m_closed || !m_statsLabel) return;
    auto const& st = engine::ProbeController::get().stats();
    auto const& cfg = engine::ProbeController::get().config();
    const int dec = std::clamp(cfg.pointsDecimals, 1, 4);
    m_statsLabel->setString(fmt::format(
        "Att: {}  |  1F: {}  |  Pts: {:.{}f}  |  Best L*: {:.2f}",
        st.attempts, st.framePerfects, st.totalPoints, dec, st.bestLStar
    ).c_str());
}

void ModMenuPopup::updateVisualPreview() {
    if (m_closed || !m_previewBox) return;
    m_previewBox->removeAllChildrenWithCleanup(true);

    const auto cfg = core::SettingsBridge::read();
    auto* bg = NineSlice::create("square02b_001.png");
    bg->setContentSize({kListW - 12.f, 44.f});
    bg->setColor({18, 22, 34});
    bg->setOpacity(190);
    bg->setPosition({kListW * 0.5f, 25.f});
    m_previewBox->addChild(bg);

    if (auto* lbl = CCLabelBMFont::create("Live Circle Preview:", "chatFont.fnt")) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({14.f, 25.f});
        lbl->setScale(0.68f);
        lbl->setColor({210, 220, 240});
        m_previewBox->addChild(lbl);
    }

    drawPreviewMarker(m_previewBox, {182.f, 25.f}, 1, false, cfg);
    drawPreviewMarker(m_previewBox, {262.f, 25.f}, 3, false, cfg);
    drawPreviewMarker(m_previewBox, {342.f, 25.f}, 1, true, cfg);
}

CCNode* ModMenuPopup::makeRowShell(float width, float height, bool shaded) {
    auto* row = CCNode::create();
    row->setContentSize({width, height});
    if (shaded) {
        auto* bg = CCLayerColor::create({255, 255, 255, 14}, width - 4.f, height - 2.f);
        bg->setPosition({2.f, 1.f});
        row->addChild(bg, 0);
    }
    return row;
}

void ModMenuPopup::addSectionHeader(std::vector<CCNode*>& rows, float width, char const* title) {
    auto* row = makeRowShell(width, 22.f, false);
    if (auto* lbl = CCLabelBMFont::create(title, "goldFont.fnt")) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({8.f, 11.f});
        lbl->setScale(0.46f);
        row->addChild(lbl, 1);
    }
    rows.push_back(row);
}

void ModMenuPopup::addToggleRow(
    std::vector<CCNode*>& rows,
    float width,
    char const* label,
    char const* settingKey,
    bool value,
    geode::Function<void(bool)> extraCallback
) {
    constexpr float kRowH = 28.f;
    auto* row = makeRowShell(width, kRowH, rows.size() % 2 == 1);

    if (auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt")) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH * 0.5f});
        lbl->setScale(0.36f);
        lbl->limitLabelWidth(275.f, 0.36f, 0.20f);
        row->addChild(lbl, 1);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kRowH});
    row->addChild(menu, 2);

    std::string keyCopy(settingKey);
    auto sharedCb = std::make_shared<geode::Function<void(bool)>>(std::move(extraCallback));
    auto* toggler = CCMenuItemExt::createTogglerWithStandardSprites(
        0.66f,
        [keyCopy, sharedCb](CCMenuItemToggler* sender) {
            // Note: CCMenuItemToggler callback runs BEFORE m_toggled flips in GD!
            const bool nextVal = !sender->isToggled();
            core::SettingsBridge::setBool(keyCopy, nextVal);
            if (sharedCb && *sharedCb) (*sharedCb)(nextVal);
        }
    );
    toggler->toggle(value);
    toggler->setPosition({width - 28.f, kRowH * 0.5f});
    menu->addChild(toggler);

    rows.push_back(row);
}

void ModMenuPopup::addCycleRow(
    std::vector<CCNode*>& rows,
    float width,
    char const* label,
    std::vector<std::string> options,
    std::string const& current,
    std::string const& settingKey,
    geode::Function<void(std::string const&)> extraCallback
) {
    constexpr float kRowH = 30.f;
    auto* row = makeRowShell(width, kRowH, rows.size() % 2 == 1);

    if (auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt")) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH * 0.5f});
        lbl->setScale(0.35f);
        lbl->limitLabelWidth(185.f, 0.35f, 0.20f);
        row->addChild(lbl, 1);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kRowH});
    row->addChild(menu, 2);

    geode::WeakRef<CCLabelBMFont> weakVal;
    if (auto* valLbl = CCLabelBMFont::create(current.c_str(), "bigFont.fnt")) {
        valLbl->setAnchorPoint({0.5f, 0.5f});
        valLbl->setPosition({width - 82.f, kRowH * 0.5f});
        valLbl->setScale(0.34f);
        valLbl->setColor({140, 235, 255});
        valLbl->limitLabelWidth(96.f, 0.34f, 0.18f);
        row->addChild(valLbl, 2);
        weakVal = geode::WeakRef<CCLabelBMFont>(valLbl);
    }

    auto state = std::make_shared<std::size_t>(0);
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i] == current) {
            *state = i;
            break;
        }
    }

    auto sharedCb = std::make_shared<geode::Function<void(std::string const&)>>(std::move(extraCallback));
    auto applyIndex = [options, settingKey, weakVal, state, sharedCb](int delta) {
        if (options.empty()) return;
        const std::size_t n = options.size();
        *state = (*state + n + static_cast<std::size_t>(delta)) % n;
        auto const& next = options[*state];
        if (auto lbl = weakVal.lock()) {
            lbl->setString(next.c_str());
            lbl->limitLabelWidth(96.f, 0.34f, 0.18f);
        }
        core::SettingsBridge::setString(settingKey, next);
        if (sharedCb && *sharedCb) (*sharedCb)(next);
    };

    if (auto* leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        leftSpr->setScale(0.46f);
        auto* leftBtn = CCMenuItemExt::createSpriteExtra(leftSpr, [applyIndex](CCMenuItemSpriteExtra*) {
            applyIndex(-1);
        });
        leftBtn->setPosition({width - 144.f, kRowH * 0.5f});
        menu->addChild(leftBtn);
    }

    if (auto* rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        rightSpr->setScale(0.46f);
        rightSpr->setFlipX(true);
        auto* rightBtn = CCMenuItemExt::createSpriteExtra(rightSpr, [applyIndex](CCMenuItemSpriteExtra*) {
            applyIndex(+1);
        });
        rightBtn->setPosition({width - 20.f, kRowH * 0.5f});
        menu->addChild(rightBtn);
    }

    rows.push_back(row);
}

void ModMenuPopup::addSliderRow(
    std::vector<CCNode*>& rows,
    float width,
    char const* label,
    std::string const& settingKey,
    float value,
    float minVal,
    float maxVal,
    float step,
    unsigned int precision,
    bool isInt,
    geode::Function<void()> extraCallback
) {
    constexpr float kRowH = 32.f;
    auto* row = makeRowShell(width, kRowH, rows.size() % 2 == 1);

    if (auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt")) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH * 0.5f});
        lbl->setScale(0.34f);
        lbl->limitLabelWidth(160.f, 0.34f, 0.20f);
        row->addChild(lbl, 1);
    }

    auto* valLbl = CCLabelBMFont::create("0", "bigFont.fnt");
    if (valLbl) {
        valLbl->setAnchorPoint({1.f, 0.5f});
        valLbl->setPosition({width - 12.f, kRowH * 0.5f});
        valLbl->setScale(0.34f);
        valLbl->setColor({255, 235, 130});
        row->addChild(valLbl, 2);
    }

    auto sharedCb = std::make_shared<geode::Function<void()>>(std::move(extraCallback));
    if (auto* slider = SliderNode::create(nullptr)) {
        slider->setMin(minVal);
        slider->setMax(maxVal);
        slider->setSnapStep(step);
        slider->setValue(std::clamp(value, minVal, maxVal));
        if (valLbl) {
            slider->linkLabel(valLbl, precision);
        }
        slider->setScale(0.70f);
        slider->setPosition({width - 115.f, kRowH * 0.5f});
        slider->setTouchPriority(-503);
        slider->setSlideCallback([settingKey, isInt, sharedCb](SliderNode*, float val) {
            if (isInt) {
                core::SettingsBridge::setInt(settingKey, static_cast<std::int64_t>(std::llround(val)));
            } else {
                core::SettingsBridge::setFloat(settingKey, static_cast<double>(val));
            }
            if (sharedCb && *sharedCb) (*sharedCb)();
        });
        row->addChild(slider, 2);
    }

    rows.push_back(row);
}

void ModMenuPopup::addNumberInputRow(
    std::vector<CCNode*>& rows,
    float width,
    char const* label,
    std::string const& settingKey,
    double value,
    double minVal,
    double maxVal,
    int precision
) {
    constexpr float kRowH = 30.f;
    auto* row = makeRowShell(width, kRowH, rows.size() % 2 == 1);

    if (auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt")) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH * 0.5f});
        lbl->setScale(0.34f);
        lbl->limitLabelWidth(210.f, 0.34f, 0.20f);
        row->addChild(lbl, 1);
    }

    auto* input = TextInput::create(128.f, "0.0", "bigFont.fnt");
    input->setCommonFilter(CommonFilter::Float);
    input->setMaxCharCount(20);
    input->setString(fmt::format("{:.{}f}", value, precision));
    input->setScale(0.85f);
    input->setPosition({width - 72.f, kRowH * 0.5f});
    input->setCallback([settingKey, minVal, maxVal](std::string const& text) {
        if (text.empty() || text == "." || text == "-") return;
        char* end = nullptr;
        double parsed = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || !std::isfinite(parsed)) return;
        parsed = std::clamp(parsed, minVal, maxVal);
        core::SettingsBridge::setFloat(settingKey, parsed);
    });
    row->addChild(input, 2);

    rows.push_back(row);
}

void ModMenuPopup::rebuildCurrentTab() {
    if (m_closed || !m_scroll || !m_scroll->m_contentLayer) return;

    m_previewBox = nullptr;
    m_scroll->m_contentLayer->removeAllChildrenWithCleanup(true);

    geode::WeakRef<ModMenuPopup> weakSelf(this);
    auto const cfg = core::SettingsBridge::read();
    auto* mod = Mod::get();
    std::vector<CCNode*> rows;

    switch (m_activeTab) {
        case Tab::Engine: {
            addSectionHeader(rows, kListW, "Live 240Hz Measurement Engine");
            addToggleRow(rows, kListW, "Enable NA Frame Counter", "enabled", cfg.enabled);
            addToggleRow(rows, kListW, "Measure Click (Press) Windows", "measure-clicks", cfg.measureClicks);
            addToggleRow(rows, kListW, "Measure Release Windows", "measure-releases", cfg.measureReleases);
            addCycleRow(
                rows, kListW, "Maximum Window (Frames)",
                {"10", "12", "17", "30", "60", "120", "240"},
                mod ? mod->getSettingValue<std::string>("maximum-window-frames") : "12",
                "maximum-window-frames"
            );
            addToggleRow(rows, kListW, "Ignore Non-Fatal Clicks", "ignore-harmless-clicks", cfg.ignoreHarmlessClicks);
            addToggleRow(rows, kListW, "Show Over-Limit (>Max) Badge", "show-over-limit", cfg.showOverLimit);
            break;
        }
        case Tab::Visuals: {
            auto* previewRow = makeRowShell(kListW, 50.f, false);
            m_previewBox = previewRow;
            rows.push_back(previewRow);
            updateVisualPreview();

            addSectionHeader(rows, kListW, "Circle Outline & Number Markers");
            addToggleRow(rows, kListW, "Show Click/Release Circles", "show-rings", cfg.showRings, [weakSelf](bool) {
                if (auto self = weakSelf.lock(); self && !self->m_closed) self->updateVisualPreview();
            });
            addCycleRow(
                rows, kListW, "Number Position",
                {"Left", "Center", "Top", "Right"},
                mod ? mod->getSettingValue<std::string>("label-placement") : "Left",
                "label-placement",
                [weakSelf](std::string const&) {
                    if (auto self = weakSelf.lock(); self && !self->m_closed) self->updateVisualPreview();
                }
            );
            addCycleRow(
                rows, kListW, "Color Palette",
                {"NaN GD Classic", "NaN GD Fire", "Pastel", "Monochrome", "Custom 1-5+"},
                mod ? mod->getSettingValue<std::string>("color-preset") : "NaN GD Classic",
                "color-preset",
                [weakSelf](std::string const&) {
                    if (auto self = weakSelf.lock(); self && !self->m_closed) self->updateVisualPreview();
                }
            );
            {
                constexpr float kSwatchH = 30.f;
                auto* swatchRow = makeRowShell(kListW, kSwatchH, rows.size() % 2 == 1);
                if (auto* lbl = CCLabelBMFont::create("Custom 1-5+", "bigFont.fnt")) {
                    lbl->setAnchorPoint({0.f, 0.5f});
                    lbl->setPosition({10.f, kSwatchH * 0.5f});
                    lbl->setScale(0.34f);
                    lbl->limitLabelWidth(135.f, 0.34f, 0.18f);
                    swatchRow->addChild(lbl, 1);
                }
                auto* sMenu = CCMenu::create();
                sMenu->setPosition({0.f, 0.f});
                sMenu->setContentSize({kListW, kSwatchH});
                swatchRow->addChild(sMenu, 2);

                struct SwatchDef { char const* key; char const* tag; core::RGBColor col; float x; };
                const SwatchDef swatches[] = {
                    {"color-1f", "1F", cfg.custom1F, 182.f},
                    {"color-2f", "2F", cfg.custom2F, 224.f},
                    {"color-3f", "3F", cfg.custom3F, 266.f},
                    {"color-4f", "4F", cfg.custom4F, 308.f},
                    {"color-5p", "5+", cfg.custom5P, 350.f},
                };
                for (auto const& sw : swatches) {
                    auto* box = NineSlice::create("square02b_001.png");
                    box->setContentSize({34.f, 20.f});
                    box->setColor(ccc3(sw.col.r, sw.col.g, sw.col.b));
                    if (auto* tag = CCLabelBMFont::create(sw.tag, "bigFont.fnt")) {
                        tag->setPosition({17.f, 10.f});
                        tag->setScale(0.32f);
                        tag->setColor({0, 0, 0});
                        box->addChild(tag, 1);
                    }
                    std::string keyStr(sw.key);
                    auto* btn = CCMenuItemExt::createSpriteExtra(box, [weakSelf, keyStr](CCMenuItemSpriteExtra*) {
                        auto* m = Mod::get();
                        ccColor3B curr = m ? m->getSettingValue<ccColor3B>(keyStr) : ccColor3B{255, 255, 255};
                        auto* picker = ColorPickPopup::create(curr);
                        picker->setCallback([weakSelf, keyStr](ccColor4B const& chosen) {
                            const ccColor3B c3{chosen.r, chosen.g, chosen.b};
                            auto& ctrl = engine::ProbeController::get();
                            ctrl.setSuppressSettingsChanged(true);
                            core::SettingsBridge::setColor(keyStr, c3);
                            core::SettingsBridge::setString("color-preset", "Custom 1-5+");
                            ctrl.setSuppressSettingsChanged(false);
                            ctrl.onSettingsChanged();
                            if (auto self = weakSelf.lock(); self && !self->m_closed) {
                                self->rebuildCurrentTab();
                            }
                        });
                        picker->show();
                    });
                    btn->setPosition({sw.x, kSwatchH * 0.5f});
                    sMenu->addChild(btn);
                }
                rows.push_back(swatchRow);
            }
            addSliderRow(rows, kListW, "Circle Radius", "circle-radius", cfg.circleRadius, 5.f, 26.f, 0.5f, 1, false, [weakSelf]() {
                if (auto self = weakSelf.lock(); self && !self->m_closed) self->updateVisualPreview();
            });
            addSliderRow(rows, kListW, "Outline Thickness", "circle-thickness", cfg.circleThickness, 1.f, 5.f, 0.25f, 2, false, [weakSelf]() {
                if (auto self = weakSelf.lock(); self && !self->m_closed) self->updateVisualPreview();
            });
            addSliderRow(rows, kListW, "Marker Scale", "marker-scale", cfg.markerScale, 0.5f, 2.0f, 0.05f, 2, false, [weakSelf]() {
                if (auto self = weakSelf.lock(); self && !self->m_closed) self->updateVisualPreview();
            });
            addSliderRow(rows, kListW, "Fade Duration (0=Keep)", "marker-fade-seconds", cfg.markerFadeSeconds, 0.f, 6.f, 0.25f, 2);
            addToggleRow(rows, kListW, "Scale Markers With Zoom", "scale-with-zoom", cfg.scaleWithZoom);
            addToggleRow(rows, kListW, "Tag Releases With 'R' (e.g. R1)", "distinguish-release", cfg.distinguishRelease, [weakSelf](bool) {
                if (auto self = weakSelf.lock(); self && !self->m_closed) self->updateVisualPreview();
            });
            break;
        }
        case Tab::Audio: {
            // Live test ding bar
            auto* testRow = makeRowShell(kListW, 42.f, false);
            auto* testMenu = CCMenu::create();
            testMenu->setPosition({0.f, 0.f});
            testMenu->setContentSize({kListW, 42.f});
            testRow->addChild(testMenu, 2);

            if (auto* tLbl = CCLabelBMFont::create("Test Pitch:", "bigFont.fnt")) {
                tLbl->setAnchorPoint({0.f, 0.5f});
                tLbl->setPosition({8.f, 21.f});
                tLbl->setScale(0.32f);
                testRow->addChild(tLbl, 1);
            }

            struct TestBtn { std::uint64_t f; char const* txt; float x; char const* spr; };
            const TestBtn btns[] = {
                {1, "1F Low", 132.f, "GJ_button_06.png"},
                {3, "3F Mid", 202.f, "GJ_button_03.png"},
                {8, "8F High", 274.f, "GJ_button_02.png"},
                {12, "12F Peak", 348.f, "GJ_button_01.png"},
            };
            for (auto const& b : btns) {
                auto* spr = ButtonSprite::create(b.txt, 58, true, "bigFont.fnt", b.spr, 22.f, 0.34f);
                auto* btn = CCMenuItemExt::createSpriteExtra(spr, [frames = b.f](CCMenuItemSpriteExtra*) {
                    audio::SoundPlayer::get().playForWindow(frames, core::SettingsBridge::read(), true);
                });
                btn->setPosition({b.x, 21.f});
                testMenu->addChild(btn);
            }
            rows.push_back(testRow);

            addSectionHeader(rows, kListW, "Pitch-Scaled Ding Audio");
            addToggleRow(rows, kListW, "Enable Ding Sounds", "ding-sounds", cfg.dingSounds);
            addCycleRow(
                rows, kListW, "Ding Sound Style",
                {"Classic Bell", "Soft Chime", "Crystal Ping", "Retro Blip", "Custom File"},
                mod ? mod->getSettingValue<std::string>("sound-style") : "Classic Bell",
                "sound-style"
            );
            addSliderRow(rows, kListW, "Ding Volume", "sound-volume", cfg.soundVolume, 0.f, 1.f, 0.05f, 2);
            addToggleRow(rows, kListW, "Scale With GD SFX Volume", "sync-gd-sfx", cfg.syncGdSfx);
            addSliderRow(rows, kListW, "Lowest Pitch (1 Frame)", "min-pitch", cfg.minPitch, 0.25f, 1.25f, 0.05f, 2);
            addSliderRow(rows, kListW, "Highest Pitch (Wide)", "max-pitch", cfg.maxPitch, 0.80f, 2.50f, 0.05f, 2);
            addCycleRow(
                rows, kListW, "Pitch Mapping Curve",
                {"Musical", "Linear", "Semitones"},
                mod ? mod->getSettingValue<std::string>("pitch-curve") : "Musical",
                "pitch-curve"
            );
            addSliderRow(rows, kListW, "Audio Max Window (F)", "sound-max-frames", static_cast<float>(cfg.soundMaxFrames), 1.f, 240.f, 1.f, 0, true);
            break;
        }
        case Tab::HudNandl: {
            addSectionHeader(rows, kListW, "HUD Layout");
            addToggleRow(rows, kListW, "Show Tally Counter HUD", "show-counters", cfg.showCounters);
            addCycleRow(
                rows, kListW, "Tally HUD Corner",
                {"Top Left", "Top Right", "Bottom Right"},
                mod ? mod->getSettingValue<std::string>("hud-corner") : "Top Left",
                "hud-corner"
            );
            addCycleRow(
                rows, kListW, "Tally Order",
                {"1 at Bottom", "1 at Top"},
                mod ? mod->getSettingValue<std::string>("bucket-order") : "1 at Bottom",
                "bucket-order"
            );
            addToggleRow(rows, kListW, "Show Last Input Badge", "show-last-badge", cfg.showLastBadge);

            addSectionHeader(rows, kListW, "Right-Side Total Points");
            addToggleRow(rows, kListW, "Show Total Points HUD", "show-points-hud", cfg.showPointsHud);
            addCycleRow(
                rows, kListW, "Points Position",
                {"Top Right", "Middle Right", "Bottom Right"},
                mod ? mod->getSettingValue<std::string>("points-corner") : "Top Right",
                "points-corner"
            );
            addCycleRow(
                rows, kListW, "Points Formula",
                {"240Hz FP (1/N)", "60Hz FP (4/N)", "120Hz FP (2/N)", "Squared (1/N^2)", "60Hz Align Odds"},
                mod ? mod->getSettingValue<std::string>("points-formula") : "240Hz FP (1/N)",
                "points-formula"
            );
            addSliderRow(rows, kListW, "Points Decimals", "points-decimals", static_cast<float>(cfg.pointsDecimals), 1.f, 4.f, 1.f, 0, true);

            addSectionHeader(rows, kListW, "Live NaNDL Precision (L*)");
            addToggleRow(rows, kListW, "Show Precision L* HUD", "show-precision-hud", cfg.showPrecisionHud);
            addCycleRow(
                rows, kListW, "NaNDL Model",
                {
                    "NFC (Full)", "Base", "Nerve", "Fatigue", "CPS",
                    "Nerve+Fatigue", "Nerve+CPS", "Fatigue+CPS",
                    "All 8 Models", "Custom Toggles"
                },
                mod ? mod->getSettingValue<std::string>("nandl-model") : "NFC (Full)",
                "nandl-model",
                [weakSelf](std::string const&) {
                    Loader::get()->queueInMainThread([weakSelf]() {
                        if (auto self = weakSelf.lock(); self && !self->m_closed) {
                            self->rebuildCurrentTab();
                        }
                    });
                }
            );
            if (cfg.nandlDisplay == core::NandlDisplayMode::Custom) {
                addToggleRow(rows, kListW, "Custom: Base L*", "nandl-show-base", cfg.nandlCustom.base);
                addToggleRow(rows, kListW, "Custom: Nerve L*", "nandl-show-nerve", cfg.nandlCustom.nerve);
                addToggleRow(rows, kListW, "Custom: Fatigue L*", "nandl-show-fatigue", cfg.nandlCustom.fatigue);
                addToggleRow(rows, kListW, "Custom: CPS L*", "nandl-show-cps", cfg.nandlCustom.cps);
                addToggleRow(rows, kListW, "Custom: Nerve+Fatigue L*", "nandl-show-nf", cfg.nandlCustom.nerveFatigue);
                addToggleRow(rows, kListW, "Custom: Nerve+CPS L*", "nandl-show-nc", cfg.nandlCustom.nerveCps);
                addToggleRow(rows, kListW, "Custom: Fatigue+CPS L*", "nandl-show-fc", cfg.nandlCustom.fatigueCps);
                addToggleRow(rows, kListW, "Custom: NFC (Full) L*", "nandl-show-nfc", cfg.nandlCustom.nfc);
            }
            addToggleRow(rows, kListW, "Show Pass Chance P(C)", "show-pass-prob", cfg.showPassProb);
            addToggleRow(rows, kListW, "Show Attempt Time E[Ta]", "show-expected-time", cfg.showExpectedTime);
            addNumberInputRow(rows, kListW, "Window FPS", "macro-fps", cfg.nandlParams.macroFps, 60.0, 960.0, 1);
            addNumberInputRow(rows, kListW, "Respawn Time (s)", "respawn-time", cfg.nandlParams.respawnTime, 0.0, 5.0, 2);
            addNumberInputRow(rows, kListW, "Target Clear Time (h)", "target-hours", cfg.nandlParams.targetTime / 3600.0, 0.5, 168.0, 1);
            addNumberInputRow(rows, kListW, "Nerve k_t", "nandl-kt", cfg.nandlParams.kT, 0.0, 0.05, 8);
            addNumberInputRow(rows, kListW, "Fatigue k_u", "nandl-ku", cfg.nandlParams.kU, 0.0, 0.05, 8);
            addNumberInputRow(rows, kListW, "CPS k_c", "nandl-kc", cfg.nandlParams.kC, 0.0, 2.0, 8);

            // Reset NaNDL defaults row
            auto* resetNandlRow = makeRowShell(kListW, 34.f, false);
            auto* rMenu = CCMenu::create();
            rMenu->setPosition({0.f, 0.f});
            rMenu->setContentSize({kListW, 34.f});
            resetNandlRow->addChild(rMenu, 1);
            auto* rSpr = ButtonSprite::create("Reset NaNDL Constants", 150, true, "goldFont.fnt", "GJ_button_05.png", 24.f, 0.36f);
            auto* rBtn = CCMenuItemExt::createSpriteExtra(rSpr, [weakSelf](CCMenuItemSpriteExtra*) {
                auto& ctrl = engine::ProbeController::get();
                ctrl.setSuppressSettingsChanged(true);
                core::SettingsBridge::resetKeys({
                    "macro-fps", "respawn-time", "target-hours", "nandl-kt", "nandl-ku", "nandl-kc"
                });
                ctrl.setSuppressSettingsChanged(false);
                ctrl.onSettingsChanged();
                Loader::get()->queueInMainThread([weakSelf]() {
                    if (auto self = weakSelf.lock(); self && !self->m_closed) {
                        self->rebuildCurrentTab();
                    }
                });
            });
            rBtn->setPosition({kListW * 0.5f, 17.f});
            rMenu->addChild(rBtn);
            rows.push_back(resetNandlRow);
            break;
        }
    }

    float totalH = 0.f;
    for (auto* r : rows) {
        totalH += r->getContentSize().height;
    }
    const float contentH = std::max(kListH, totalH);
    m_scroll->m_contentLayer->setContentSize({kListW, contentH});

    float cursorY = contentH;
    for (auto* r : rows) {
        const float h = r->getContentSize().height;
        cursorY -= h;
        r->setAnchorPoint({0.f, 0.f});
        r->setPosition({0.f, cursorY});
        m_scroll->m_contentLayer->addChild(r);
    }

    m_scroll->scrollToTop();
    geode::cocos::handleTouchPriority(this, false);
    refreshStatsFooter();
}

}
