#pragma once

#include "FrameConfig.hpp"
#include <Geode/Geode.hpp>
#include <string_view>

namespace na_frame_counter::core {

class SettingsBridge {
public:
    static ModSettingsSnapshot read();

    static void setBool(std::string_view key, bool val);
    static void setInt(std::string_view key, std::int64_t val);
    static void setFloat(std::string_view key, double val);
    static void setString(std::string_view key, std::string const& val);
    static void setColor(std::string_view key, cocos2d::ccColor3B val);
    static void resetKeys(std::initializer_list<std::string_view> keys);
    static void resetAll();
};

}
