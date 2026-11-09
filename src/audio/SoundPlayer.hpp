#pragma once

#include "../core/FrameConfig.hpp"
#include <Geode/Geode.hpp>
#if !defined(GEODE_IS_IOS)
#include <Geode/fmod/fmod.hpp>
#endif
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace na_frame_counter::audio {

class SoundPlayer {
public:
    static SoundPlayer& get() {
        static SoundPlayer instance;
        return instance;
    }

    void preload(core::ModSettingsSnapshot const& cfg);
    void playForWindow(
        std::uint64_t frames,
        core::ModSettingsSnapshot const& cfg,
        bool forceTest = false
    );
    void stopAll();
    void clearCache();

private:
    SoundPlayer() = default;

    std::filesystem::path resolveSoundPath(core::ModSettingsSnapshot const& cfg) const;

#if !defined(GEODE_IS_IOS)
    bool ensureChannelGroup(FMOD::System* sys);
    FMOD::Sound* getOrLoadSound(FMOD::System* sys, std::string const& fullPath);

    FMOD::ChannelGroup* m_channelGroup = nullptr;
    std::unordered_map<std::string, FMOD::Sound*> m_soundCache;
    std::vector<FMOD::Channel*> m_activeChannels;
#else
    std::uint32_t m_soundSerial = 1;
#endif
};

}
