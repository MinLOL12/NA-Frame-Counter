#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/ui/SliderNode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <vector>

namespace na_frame_counter::ui {

class ModMenuPopup : public geode::Popup {
public:
    enum class Tab : int {
        Engine = 0,
        Visuals = 1,
        Audio = 2,
        HudNandl = 3,
    };

    static ModMenuPopup* create();
    static ModMenuPopup* openOrFocus();
    void closePopup() { this->onClose(nullptr); }

protected:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

private:
    Tab m_activeTab = Tab::Engine;

    cocos2d::CCNode* m_tabBar = nullptr;
    cocos2d::CCNode* m_contentContainer = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    geode::Scrollbar* m_scrollbar = nullptr;
    cocos2d::CCNode* m_previewBox = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;

    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;

    void buildTabBar();
    void selectTab(Tab tab);
    void rebuildCurrentTab();
    void refreshStatsFooter();
    void updateVisualPreview();

    cocos2d::CCNode* makeRowShell(float width, float height, bool shaded);
    void addSectionHeader(std::vector<cocos2d::CCNode*>& rows, float width, char const* title);
    void addToggleRow(
        std::vector<cocos2d::CCNode*>& rows,
        float width,
        char const* label,
        char const* settingKey,
        bool value,
        geode::Function<void(bool)> extraCallback = nullptr
    );
    void addCycleRow(
        std::vector<cocos2d::CCNode*>& rows,
        float width,
        char const* label,
        std::vector<std::string> options,
        std::string const& current,
        std::string const& settingKey,
        geode::Function<void(std::string const&)> extraCallback = nullptr
    );
    void addSliderRow(
        std::vector<cocos2d::CCNode*>& rows,
        float width,
        char const* label,
        std::string const& settingKey,
        float value,
        float minVal,
        float maxVal,
        float step,
        unsigned int precision,
        bool isInt = false,
        geode::Function<void()> extraCallback = nullptr
    );
    void addNumberInputRow(
        std::vector<cocos2d::CCNode*>& rows,
        float width,
        char const* label,
        std::string const& settingKey,
        double value,
        double minVal,
        double maxVal,
        int precision
    );
};

}
