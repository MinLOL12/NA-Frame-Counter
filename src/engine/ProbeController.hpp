#pragma once

#include "ProbeTypes.hpp"

namespace na_frame_counter::engine {

enum class SentinelKind {
    Level,
    Hud,
    PrecHud,
    MarkerLayer,
};

class CleanupSentinel final : public cocos2d::CCNode {
public:
    static CleanupSentinel* create(SentinelKind kind, void* expected);
    void disarm() { m_armed = false; }
    ~CleanupSentinel() override;

private:
    SentinelKind m_kind = SentinelKind::Level;
    void* m_expected = nullptr;
    bool m_armed = true;
};

class ProbeController {
public:
    static ProbeController& get() {
        static ProbeController instance;
        return instance;
    }

    // Settings & Stats
    void setSuppressSettingsChanged(bool suppress) { m_suppressSettingsChanged = suppress; }
    void onSettingsChanged();
    void onSentinelDestroyed(SentinelKind kind, void* expected);
    core::ModSettingsSnapshot const& config() const { return m_cfg; }
    SessionStats const& stats() const { return m_stats; }
    bool isPlatformer() const { return m_platformer; }
    PlayLayer* activePlayLayer() const { return m_level; }

    // Level & Attempt Lifecycle
    void startLevel(PlayLayer* layer);
    void beforeReset(PlayLayer* layer);
    void onReset(PlayLayer* layer);
    void handleDeath(PlayLayer* layer);
    void completeLevel(PlayLayer* layer);
    void beforeUpdate(GJBaseGameLayer* base);
    void afterTick(PlayLayer* layer, float dt);
    void afterUpdate(GJBaseGameLayer* base);
    void onQuit(PlayLayer* layer);
    void onExit(PlayLayer* layer);
    void pauseStart(PlayLayer* layer);
    void pauseEnd(PlayLayer* layer);

    // Input & Activation Recording
    bool beforeButton(PlayerObject* player, PlayerButton button, bool down);
    void afterButton(PlayerObject* player, PlayerButton button, bool down, bool accepted);
    void beforeControlToggle(PlayerObject* player, bool enable);
    void afterControlToggle(PlayerObject* player, bool enable);
    void onJumpActive(PlayerObject* player, RingObject* ring);
    void saveHistory(PlayLayer* layer, PlayerObject* player);

    // Probe Isolation Queries (used by Hooks)
    bool activeLayer(GJBaseGameLayer* layer) const;
    bool probing() const { return m_probing; }
    bool isProbe(PlayerObject* player) const;
    bool skipFx(GJBaseGameLayer* layer) const;
    bool skipEffects() const { return m_probing; }
    bool skipPlayerFx(PlayerObject* player) const;
    bool disableRings() const { return m_probing; }
    bool hideGround(GJGroundLayer* ground) const;
    GJGroundLayer* probeG1() const { return m_probeG1; }
    GJGroundLayer* probeG2() const { return m_probeG2; }
    bool usedPortal(GameObject* object) const;
    void addPortal(GameObject* object);
    void clearProbeHit();
    void probeDeath(PlayerObject* player, GameObject* object);
    void probeDone();

private:
    ProbeController();
    ~ProbeController();

    // Frame timing & mode helpers
    std::int64_t rawFrame(PlayLayer* layer) const;
    void startClock(PlayLayer* layer);
    void resetAttemptState();
    void resetAllState();

    static bool isFlight(PlayerObject const* player);
    static bool isUfo(PlayerObject const* player);
    static int savedMode(PlayerCheckpoint const* state);
    static int playerMode(PlayerObject const* player);
    bool canHoldCube(PlayerObject const* player) const;
    static void stopTrail(PlayerObject* player);
    void syncMode(PlayerObject* player, PlayerCheckpoint* state);

    bool ringBuffered(ClickJob const& job) const;
    bool noReleaseWin(ClickJob const& job) const;
    std::int64_t pressFrameOf(ClickJob const& job) const;
    std::int64_t releaseFrameOf(ClickJob const& job) const;
    void confirmJump(
        ClickJob& job,
        std::int64_t activationFrame,
        cocos2d::CCPoint const& activationPos,
        bool fromRing,
        bool wasBuffered,
        RingObject* ring,
        bool tryHolding
    );
    void applyJump(ClickJob& job);

    // Probe Simulation (ProbeSimulation.cpp)
    PlayerObject* createProbePlayer(PlayLayer* layer);
    void setupProbe(PlayLayer* layer);
    void dropProbe();
    void clearHistory();
    void ensureStarted(PlayLayer* layer);
    void synthesizePreStartHold(PlayLayer* layer, PlayerObject* player, bool isPlayer1);
    void captureSlot(PlayLayer* layer, PlayerObject* player, HistorySlot& slot, std::int64_t frame, bool preInput);
    HistorySlot const* findHistory(bool player1, std::int64_t desiredFrame, bool needPreInput = false) const;
    bool probeReady(PlayerObject* player) const;
    void probeInput(PlayerObject* probe, InputEvent const& event, bool pressEvent, bool forceEnableControls, bool forceDisableAfter);
    PathState runPath(
        PlayLayer* layer,
        ClickJob const& job,
        ScanAxis axis,
        int offset,
        bool removeTarget,
        std::int64_t stopAt
    );
    bool sameJump(ClickJob const& job, PathState const& result) const;
    float releaseProgress(ClickJob const& job, PathState const& result) const;
    bool pickCubeHold(PlayLayer* layer, ClickJob& job);
    bool pathValid(
        ClickJob const& job,
        PathState const& baseline,
        PathState const& candidate,
        ScanAxis axis
    ) const;
    bool checkRange(
        PlayLayer* layer,
        ClickJob const& job,
        PathState const& baseline,
        ScanAxis axis,
        bool earlier,
        int depth
    );
    int scanDepth(
        PlayLayer* layer,
        ClickJob const& job,
        PathState const& baseline,
        ScanAxis axis,
        bool earlier,
        int maxDepth
    );
    void scanRoute(
        PlayLayer* layer,
        ClickJob const& job,
        PathState const& baseline,
        ScanAxis axis,
        std::uint64_t& windowFrames,
        bool& hitLimit
    );
    bool scanClick(PlayLayer* layer, ClickJob& job);
    void scanClicks(PlayLayer* layer);

    // HUD, Markers, NaNDL Overlay (ProbeHUD.cpp)
    void buildHUD();
    void clearHUD();
    void resetHUD();
    void pulseHUD(std::uint64_t windowFrames, bool isRelease, bool overLimit);
    void buildPrecisionHUD();
    void clearPrecisionHUD();
    void updatePrecisionHUD();
    void recordNandlAndUpdate(std::int64_t actionFrame, std::uint64_t windowFrames, bool isRelease, bool ignored = false);

    cocos2d::CCPoint ringScreenPos(PlayLayer* layer, cocos2d::CCPoint objectPos) const;
    cocos2d::CCNode* createRingMarkerNode(
        std::uint64_t windowFrames,
        bool isRelease,
        bool overLimit,
        cocos2d::CCDrawNode*& outRing,
        cocos2d::CCLabelBMFont*& outLabel
    );
    void moveMarkers(PlayLayer* layer, float dt);
    void clearMarkers();
    void attachMarker(
        PlayLayer* layer,
        cocos2d::CCPoint const& position,
        std::uint64_t windowFrames,
        bool isRelease,
        bool overLimit,
        bool withSound
    );
    void recordMeasurement(
        PlayLayer* layer,
        ClickJob const& job,
        std::uint64_t pressWindow,
        std::uint64_t releaseWindow,
        bool showPress,
        bool pressOver,
        bool pressNandlIgnored,
        bool showRelease,
        bool releaseOver,
        bool releaseNandlIgnored
    );

    // State
    core::ModSettingsSnapshot m_cfg;
    SessionStats m_stats;

    PlayLayer* m_level = nullptr;
    bool m_platformer = false;
    bool m_started = false;
    bool m_clockReady = false;
    bool m_attemptDone = false;
    bool m_levelDone = false;
    bool m_resetting = false;
    bool m_paused = false;
    int m_resetDepth = 0;

    std::int64_t m_lastFrame = 0;
    std::int64_t m_attemptStartFrame = 0;
    std::uint64_t m_orderCounter = 1;
    std::size_t m_jumpCount = 0;

    bool m_sendingJump = false;
    bool m_jumpP1 = true;
    bool m_pendingButtonLocked = false;
    PlayerObject* m_inControlEnablePlayer = nullptr;
    PlayerObject* m_inControlDisablePlayer = nullptr;
    std::uint64_t m_activeControlOrder = kNoOrder;

    std::vector<InputEvent> m_events;
    std::vector<InputEvent> m_probeEvents;
    std::vector<ControlEvent> m_controlEvents;
    std::vector<ControlEvent> m_probeControlEvents;
    std::vector<ClickJob> m_clicks;
    std::deque<JumpEvent> m_jumps;

    // Probe objects
    cocos2d::CCLayer* m_probeLayer = nullptr;
    PlayerObject* m_probeP1 = nullptr;
    PlayerObject* m_probeP2 = nullptr;
    PlayerObject* m_probePlayer = nullptr;
    GJGroundLayer* m_probeG1 = nullptr;
    GJGroundLayer* m_probeG2 = nullptr;
    GJGroundLayer* m_ground1 = nullptr;
    GJGroundLayer* m_ground2 = nullptr;

    std::array<std::array<HistorySlot, kHistSize>, 2> m_history{};
    std::array<HistorySlot, 2> m_preStartSlot{};
    std::array<std::int64_t, 2> m_lastHistFrame{-1, -1};

    bool m_probing = false;
    bool m_replayingInput = false;
    bool m_probeDead = false;
    bool m_probeDone = false;
    bool m_probeDown = false;
    bool m_probeJumped = false;
    bool m_probeRingJump = false;
    std::int64_t m_probeAt = 0;
    std::int64_t m_probeJumpAt = -1;
    std::int64_t m_probeDeath = -1;
    GameObject* m_probeHit = nullptr;
    RingObject* m_ring = nullptr;
    std::vector<GameObject*> m_portals;

    // HUD & Markers
    cocos2d::CCNode* m_hud = nullptr;
    cocos2d::CCLabelBMFont* m_lastBadgeLabel = nullptr;
    cocos2d::CCNode* m_precHud = nullptr;
    cocos2d::CCNode* m_markerLayer = nullptr;
    CleanupSentinel* m_levelSentinel = nullptr;
    CleanupSentinel* m_hudSentinel = nullptr;
    CleanupSentinel* m_precHudSentinel = nullptr;
    CleanupSentinel* m_markerSentinel = nullptr;
    bool m_suppressSettingsChanged = false;
    std::vector<CountRow> m_counters;
    std::vector<std::uint64_t> m_measuredWindows; // for rebuilding HUD mid-attempt
    std::deque<RingMarker> m_markers;

    // NaNDL live run state
    std::vector<math::InputTiming> m_nandlInputs;
    math::NandlSummary m_nandlSummary;
    bool m_hasNandlSummary = false;
};

}
