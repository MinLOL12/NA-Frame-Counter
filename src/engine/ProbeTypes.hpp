#pragma once

#include "../core/FrameConfig.hpp"
#include "../math/NandlCalculator.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <vector>

namespace na_frame_counter::engine {

static constexpr std::uint64_t kNoOrder = std::numeric_limits<std::uint64_t>::max();
static constexpr double kPhysicsHz = 240.0;
static constexpr double kDeathTol = 0.10;
static constexpr std::int64_t kDeathGap = 8;
static constexpr std::int64_t kPadding = 240;
static constexpr std::int64_t kReleaseDelay = 8;
static constexpr std::int64_t kFlightMax = 120;
static constexpr std::int64_t kGroundMax = 720;
static constexpr std::int64_t kCubeLimit = 1440;
static constexpr std::int64_t kLongHoldMeasureFrames = 720;
static constexpr int kGroundSteps = 6;
static constexpr int kProbeLimit = 720;
static constexpr int kHoldLimit = 1440;
static constexpr std::size_t kHistSize = 8192;
static constexpr std::size_t kMaxVisibleMarkers = 160;
static constexpr int kHoldSteps[] = {1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 160, 240};
static constexpr int kHoldCount = static_cast<int>(sizeof(kHoldSteps) / sizeof(kHoldSteps[0]));
static constexpr int kHoldRefine = 3;
static constexpr float kReleaseTol = 0.10f;

struct StateBackup {
    float middleGroundOffsetY = 0.f;
    float portalY = 0.f;
    bool gravityRelated = false;
    bool freeMode = false;
    bool dualMode = false;
    float levelFlipProgress = 0.f;
    float cameraZoom = 1.f;
    float targetCameraZoom = 1.f;
    cocos2d::CCPoint cameraOffset{0.f, 0.f};
    cocos2d::CCPoint cameraPosition{0.f, 0.f};
    cocos2d::CCPoint cameraStepDiff{0.f, 0.f};
    float cameraAngle = 0.f;
    float targetCameraAngle = 0.f;
    GameObject* lastPortal1 = nullptr;
    GameObject* lastPortal2 = nullptr;

    static StateBackup capture(GJGameState const& state) {
        return {
            state.m_middleGroundOffsetY,
            state.m_portalY,
            state.m_gravityRelated,
            state.m_isFreeMode,
            state.m_isDualMode,
            state.m_levelFlipProgress,
            state.m_cameraZoom,
            state.m_targetCameraZoom,
            state.m_cameraOffset,
            state.m_cameraPosition,
            state.m_cameraStepDiff,
            state.m_cameraAngle,
            state.m_targetCameraAngle,
            state.m_lastActivatedPortal1,
            state.m_lastActivatedPortal2,
        };
    }

    void restore(GJGameState& state) const {
        state.m_middleGroundOffsetY = middleGroundOffsetY;
        state.m_portalY = portalY;
        state.m_gravityRelated = gravityRelated;
        state.m_isFreeMode = freeMode;
        state.m_isDualMode = dualMode;
        state.m_levelFlipProgress = levelFlipProgress;
        state.m_cameraZoom = cameraZoom;
        state.m_targetCameraZoom = targetCameraZoom;
        state.m_cameraOffset = cameraOffset;
        state.m_cameraPosition = cameraPosition;
        state.m_cameraStepDiff = cameraStepDiff;
        state.m_cameraAngle = cameraAngle;
        state.m_targetCameraAngle = targetCameraAngle;
        state.m_lastActivatedPortal1 = lastPortal1;
        state.m_lastActivatedPortal2 = lastPortal2;
    }
};

struct SpriteBackup {
    cocos2d::CCSprite* sprite = nullptr;
    bool visible = false;
    unsigned char opacity = 255;
    cocos2d::CCPoint position{0.f, 0.f};
    float scaleX = 1.f;
    float scaleY = 1.f;

    static SpriteBackup capture(cocos2d::CCSprite* spr);
    void restore() const;
};

struct GroundState {
    GJGroundLayer* layer = nullptr;
    bool visible = false;
    cocos2d::CCPoint position{0.f, 0.f};
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool showGround = false;
    bool showGround1 = false;
    bool showGround2 = false;
    SpriteBackup ground1;
    SpriteBackup ground2;
    SpriteBackup line;

    static GroundState capture(GJGroundLayer* layer);
    void restore() const;
};

enum class ScanAxis {
    Press,
    Release,
};

struct InputEvent {
    std::int64_t frame = 0;
    PlayerButton button = PlayerButton::Jump;
    bool down = false;
    bool player2 = false;
    std::uint64_t order = 0;
    bool holdOnly = false;
    bool fromControl = false;
};

struct ControlEvent {
    std::int64_t frame = 0;
    bool player1 = true;
    bool disabled = false;
    std::uint64_t order = 0;
};

inline bool inputBefore(InputEvent const& a, InputEvent const& b) {
    if (a.frame != b.frame) return a.frame < b.frame;
    return a.order < b.order;
}

inline void sortInputs(std::vector<InputEvent>& events) {
    std::stable_sort(events.begin(), events.end(), inputBefore);
}

struct JumpEvent {
    std::int64_t frame = -1;
    bool player1 = true;
    cocos2d::CCPoint position{0.f, 0.f};
    bool fromRing = false;
    bool wasBuffered = false;
    RingObject* ring = nullptr;
    bool tryHolding = false;
};

struct ClickJob {
    std::size_t clickIndex = 0;
    std::int64_t pressFrame = -1;
    std::int64_t releaseFrame = -1;
    std::int64_t activationFrame = -1;
    std::uint64_t pressOrder = kNoOrder;
    std::uint64_t releaseOrder = kNoOrder;
    int button = static_cast<int>(PlayerButton::Jump);
    bool player1 = true;
    cocos2d::CCPoint pressPos{0.f, 0.f};
    cocos2d::CCPoint activationPos{0.f, 0.f};
    cocos2d::CCPoint releasePos{0.f, 0.f};
    bool activated = false;
    bool fromRing = false;
    bool wasBuffered = false;
    RingObject* activationRing = nullptr;

    bool tryHolding = false;
    std::int64_t heldFrames = -1;
    bool holdReady = false;
    bool holdSafe = false;
    bool needsRelease = false;
    int baseFailures = 0;
    bool pressDone = false;
    bool releaseDone = false;
    bool canScan = false;
    bool keepHold = true;
    bool ignoreHit = false;
    bool fromControlEnable = false;
    bool fromControlDisable = false;
    bool synthesizedPreHold = false;
    std::uint64_t pressControlOrder = kNoOrder;
    std::uint64_t releaseControlOrder = kNoOrder;
};

struct HistorySlot {
    PlayerCheckpoint* state = nullptr;
    std::int64_t frame = -1;
    std::array<bool, 4> held{};
    double totalTime = 0.0;

    std::vector<RingObject*> touchingRings;
    gd::unordered_set<int> touchedRings{};
    gd::unordered_set<int> ringRelatedSet{};
    bool touchedRing = false;
    bool touchedCustomRing = false;
    bool stateRingJump = false;
    bool stateRingJump2 = false;
    bool ringJumpRelated = false;
    bool preInput = false;
    bool controlsDisabled = false;
    bool valid = false;
};

struct PathState {
    bool started = false;
    bool activated = false;
    bool died = false;
    bool survivedHorizon = false;
    bool fromRing = false;
    RingObject* activationRing = nullptr;
    std::int64_t targetPressAt = -1;
    std::int64_t targetReleaseAt = -1;
    std::int64_t activationFrame = -1;
    std::int64_t deathFrame = -1;
    std::int64_t resolveFrame = -1;
    GameObject* deathObject = nullptr;
    cocos2d::CCPoint activationPos{0.f, 0.f};
    cocos2d::CCPoint finalPosition{0.f, 0.f};
};

struct RingMarker {
    cocos2d::CCNode* node = nullptr;
    cocos2d::CCDrawNode* ring = nullptr;
    cocos2d::CCLabelBMFont* label = nullptr;
    cocos2d::ccColor3B color{255, 255, 255};
    float radius = 10.f;
    float border = 2.f;
    bool isRelease = false;
    bool distinguishRelease = true;
    cocos2d::CCPoint worldPosition{0.f, 0.f};
    float age = 0.f;
};

struct ActiveState {
    EffectGameObject* object = nullptr;
    bool activated = false;
    bool p1 = false;
    bool p2 = false;
    bool portal = false;
};

struct CountRow {
    core::WindowBand bucket;
    std::uint64_t count = 0;
    cocos2d::CCLabelBMFont* valueLabel = nullptr;
    cocos2d::ccColor3B baseColor{255, 255, 255};
};

struct SessionStats {
    std::uint64_t attempts = 0;
    std::uint64_t totalMeasured = 0;
    std::uint64_t framePerfects = 0;
    std::uint64_t lastWindowFrames = 0;
    bool lastWasRelease = false;
    bool lastOverLimit = false;
    double currentLStar = 0.0;
    double bestLStar = 0.0;
};

}
