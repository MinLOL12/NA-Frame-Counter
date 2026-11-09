#include "SoundPlayer.hpp"

#include <Geode/utils/string.hpp>
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace na_frame_counter::audio {

std::filesystem::path SoundPlayer::resolveSoundPath(core::ModSettingsSnapshot const& cfg) const {
    std::error_code ec;
    if (cfg.soundStyle == core::SoundStyle::CustomFile && !cfg.customSoundFile.empty()) {
        std::filesystem::path custom(cfg.customSoundFile);
        if (std::filesystem::is_regular_file(custom, ec) && !ec) {
            return custom;
        }
    }

    auto* mod = geode::Mod::get();
    if (!mod) return {};

    const char* fname = core::soundAssetFilename(cfg.soundStyle);
    auto primary = mod->getResourcesDir() / fname;
    if (std::filesystem::is_regular_file(primary, ec) && !ec) {
        return primary;
    }

    ec.clear();
    auto fallback = mod->getResourcesDir() / "resources" / "sounds" / fname;
    if (std::filesystem::is_regular_file(fallback, ec) && !ec) {
        return fallback;
    }

    return {};
}

#if !defined(GEODE_IS_IOS)

bool SoundPlayer::ensureChannelGroup(FMOD::System* sys) {
    if (!sys) return false;
    if (m_channelGroup) return true;
    FMOD_RESULT res = sys->createChannelGroup("NAFrameCounterSFX", &m_channelGroup);
    return (res == FMOD_OK && m_channelGroup != nullptr);
}

FMOD::Sound* SoundPlayer::getOrLoadSound(FMOD::System* sys, std::string const& fullPath) {
    if (!sys || fullPath.empty()) return nullptr;
    auto it = m_soundCache.find(fullPath);
    if (it != m_soundCache.end()) {
        return it->second;
    }

    FMOD::Sound* sound = nullptr;
    FMOD_RESULT res = sys->createSound(
        fullPath.c_str(),
        FMOD_DEFAULT | FMOD_LOOP_OFF | FMOD_CREATESAMPLE,
        nullptr,
        &sound
    );
    if (res == FMOD_OK && sound) {
        sound->setMode(FMOD_LOOP_OFF);
        m_soundCache[fullPath] = sound;
        return sound;
    }
    return nullptr;
}

void SoundPlayer::preload(core::ModSettingsSnapshot const& cfg) {
    auto* audio = FMODAudioEngine::get();
    if (!audio || !audio->m_system) return;
    auto path = resolveSoundPath(cfg);
    if (path.empty()) return;
    (void)ensureChannelGroup(audio->m_system);
    (void)getOrLoadSound(audio->m_system, geode::utils::string::pathToString(path));
}

void SoundPlayer::playForWindow(
    std::uint64_t frames,
    core::ModSettingsSnapshot const& cfg,
    bool forceTest
) {
    if (frames == 0) return;
    if (!forceTest) {
        if (!cfg.enabled || !cfg.dingSounds) return;
        if (static_cast<int>(frames) > cfg.soundMaxFrames) return;
    }

    auto* audio = FMODAudioEngine::get();
    if (!audio || !audio->m_system) return;

    float volume = std::clamp(cfg.soundVolume, 0.0f, 1.0f);
    if (cfg.syncGdSfx) {
        volume *= std::clamp(audio->m_sfxVolume, 0.0f, 1.0f);
    }
    if (volume <= 0.001f) return;

    auto path = resolveSoundPath(cfg);
    if (path.empty()) return;
    auto pathStr = geode::utils::string::pathToString(path);

    FMOD::System* sys = audio->m_system;
    if (!ensureChannelGroup(sys)) return;
    FMOD::Sound* sound = getOrLoadSound(sys, pathStr);
    if (!sound) return;

    // Prune stopped channels and enforce max concurrency
    constexpr std::size_t kMaxVoices = 8;
    for (auto it = m_activeChannels.begin(); it != m_activeChannels.end();) {
        bool playing = false;
        if (*it && (*it)->isPlaying(&playing) == FMOD_OK && playing) {
            ++it;
        } else {
            it = m_activeChannels.erase(it);
        }
    }
    while (m_activeChannels.size() >= kMaxVoices) {
        if (m_activeChannels.front()) {
            m_activeChannels.front()->stop();
        }
        m_activeChannels.erase(m_activeChannels.begin());
    }

    const float pitch = core::pitchForWindow(frames, cfg);
    FMOD::Channel* channel = nullptr;
    if (sys->playSound(sound, m_channelGroup, true, &channel) == FMOD_OK && channel) {
        channel->setMode(FMOD_LOOP_OFF);
        channel->setLoopCount(0);
        channel->setPriority(0);
        channel->setVolumeRamp(false);
        channel->setPitch(pitch);
        channel->setVolume(volume);
        channel->setPaused(false);
        m_activeChannels.push_back(channel);
    }
}

void SoundPlayer::stopAll() {
    if (m_channelGroup) {
        m_channelGroup->stop();
    }
    m_activeChannels.clear();
}

void SoundPlayer::clearCache() {
    stopAll();
    for (auto& [_, sound] : m_soundCache) {
        if (sound) sound->release();
    }
    m_soundCache.clear();
    if (m_channelGroup) {
        m_channelGroup->release();
        m_channelGroup = nullptr;
    }
}

#else

void SoundPlayer::preload(core::ModSettingsSnapshot const& cfg) {
    auto* audio = FMODAudioEngine::get();
    if (!audio) return;
    auto path = resolveSoundPath(cfg);
    if (path.empty()) return;
    auto pathStr = geode::utils::string::pathToString(path);
    audio->preloadEffect(pathStr.c_str());
}

void SoundPlayer::playForWindow(
    std::uint64_t frames,
    core::ModSettingsSnapshot const& cfg,
    bool forceTest
) {
    if (frames == 0) return;
    if (!forceTest) {
        if (!cfg.enabled || !cfg.dingSounds) return;
        if (static_cast<int>(frames) > cfg.soundMaxFrames) return;
    }

    auto* audio = FMODAudioEngine::get();
    if (!audio) return;

    float volume = std::clamp(cfg.soundVolume, 0.0f, 1.0f);
    if (cfg.syncGdSfx) {
        volume *= std::clamp(audio->m_sfxVolume, 0.0f, 1.0f);
    }
    if (volume <= 0.001f) return;

    auto path = resolveSoundPath(cfg);
    if (path.empty()) return;
    auto pathStr = geode::utils::string::pathToString(path);

    const float pitch = core::pitchForWindow(frames, cfg);
    constexpr std::uint32_t kBase = 0x52000000u;
    constexpr std::uint32_t kMask = 0x00ffffffu;
    m_soundSerial = (m_soundSerial + 1u) & kMask;
    if (m_soundSerial == 0u) m_soundSerial = 1u;
    const int uniqueId = static_cast<int>(kBase | m_soundSerial);
    const int channelId = audio->getNextChannelID();

    audio->playEffectAdvanced(
        pathStr.c_str(),
        pitch,
        0.f,
        volume,
        1.f,
        false,
        false,
        0,
        0,
        0,
        0,
        false,
        uniqueId,
        false,
        false,
        channelId,
        uniqueId,
        0.f,
        0
    );
}

void SoundPlayer::stopAll() {}
void SoundPlayer::clearCache() {}

#endif

}
