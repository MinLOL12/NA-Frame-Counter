#include "ProbeController.hpp"
#include "../audio/SoundPlayer.hpp"
#include "../core/SettingsBridge.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace na_frame_counter::engine {

namespace {
bool s_controllerAlive = false;
} // namespace

CleanupSentinel* CleanupSentinel::create(SentinelKind kind, void* expected) {
    auto* ret = new CleanupSentinel();
    if (ret && ret->init()) {
        ret->m_kind = kind;
        ret->m_expected = expected;
        ret->setVisible(false);
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CleanupSentinel::~CleanupSentinel() {
    if (m_armed && s_controllerAlive) {
        ProbeController::get().onSentinelDestroyed(m_kind, m_expected);
    }
}

ProbeController::ProbeController() {
    s_controllerAlive = true;
}

ProbeController::~ProbeController() {
    s_controllerAlive = false;
}

void ProbeController::onSettingsChanged() {
    if (m_suppressSettingsChanged) return;
    m_cfg = core::SettingsBridge::read();
    audio::SoundPlayer::get().preload(m_cfg);

    if (m_level && !m_levelDone) {
        if (!m_cfg.enabled || m_platformer) {
            clearHUD();
            clearPrecisionHUD();
            clearMarkers();
        } else {
            buildHUD();
            buildPrecisionHUD();
        }
    }
}

void ProbeController::onSentinelDestroyed(SentinelKind kind, void* expected) {
    switch (kind) {
        case SentinelKind::Hud:
            if (m_hud == expected) {
                m_hudSentinel = nullptr;
                m_hud = nullptr;
                m_lastBadgeLabel = nullptr;
                m_counters.clear();
            }
            break;
        case SentinelKind::PrecHud:
            if (m_precHud == expected) {
                m_precHudSentinel = nullptr;
                m_precHud = nullptr;
            }
            break;
        case SentinelKind::MarkerLayer:
            if (m_markerLayer == expected) {
                m_markerSentinel = nullptr;
                m_markerLayer = nullptr;
                m_markers.clear();
            }
            break;
        case SentinelKind::Level:
            if (m_level == expected) {
                m_levelSentinel = nullptr;
                m_hudSentinel = nullptr;
                m_precHudSentinel = nullptr;
                m_markerSentinel = nullptr;

                m_hud = nullptr;
                m_lastBadgeLabel = nullptr;
                m_precHud = nullptr;
                m_markerLayer = nullptr;
                m_counters.clear();
                m_markers.clear();

                m_probeLayer = nullptr;
                m_probeP1 = nullptr;
                m_probeP2 = nullptr;
                m_probePlayer = nullptr;
                m_probeG1 = nullptr;
                m_probeG2 = nullptr;
                m_ground1 = nullptr;
                m_ground2 = nullptr;
                clearHistory();

                m_probing = false;
                m_replayingInput = false;
                m_ring = nullptr;
                m_portals.clear();
                m_level = nullptr;
                m_platformer = false;
                m_levelDone = false;
                m_resetting = false;
                m_paused = false;
                m_resetDepth = 0;

                m_started = false;
                m_clockReady = false;
                m_attemptDone = false;
                m_sendingJump = false;
                m_jumpP1 = true;
                m_lastFrame = 0;
                m_attemptStartFrame = 0;
                m_orderCounter = 1;
                m_jumpCount = 0;

                m_events.clear();
                m_clicks.clear();
                m_jumps.clear();
                m_measuredWindows.clear();
                m_nandlInputs.clear();
                m_nandlSummary = math::NandlSummary{};
                m_hasNandlSummary = false;
                m_stats.currentLStar = 0.0;
                m_stats.lastWindowFrames = 0;
            }
            break;
    }
}

void ProbeController::startLevel(PlayLayer* layer) {
    if (!layer) return;
    resetAllState();

    m_cfg = core::SettingsBridge::read();
    m_level = layer;
    m_levelSentinel = CleanupSentinel::create(SentinelKind::Level, layer);
    if (m_levelSentinel) {
        m_levelSentinel->setID("level-sentinel"_spr);
        layer->addChild(m_levelSentinel, -999999);
    }

    m_platformer = layer->m_level && layer->m_level->isPlatformer();
    m_stats = SessionStats{};
    m_stats.attempts = 1;

    audio::SoundPlayer::get().preload(m_cfg);

    if (m_cfg.enabled && !m_platformer) {
        buildHUD();
        buildPrecisionHUD();
    }
}

void ProbeController::resetAttemptState() {
    m_started = false;
    m_clockReady = false;
    m_attemptDone = false;
    m_sendingJump = false;
    m_jumpP1 = true;
    m_lastFrame = 0;
    m_attemptStartFrame = 0;
    m_orderCounter = 1;
    m_jumpCount = 0;

    m_events.clear();
    m_clicks.clear();
    m_jumps.clear();
    m_measuredWindows.clear();
    m_nandlInputs.clear();
    m_nandlSummary = math::NandlSummary{};
    m_hasNandlSummary = false;
    m_stats.currentLStar = 0.0;
    m_stats.lastWindowFrames = 0;

    clearMarkers();
    resetHUD();
    updatePrecisionHUD();
}

void ProbeController::resetAllState() {
    m_probing = false;
    m_replayingInput = false;
    m_probePlayer = nullptr;
    m_ring = nullptr;
    m_portals.clear();
    dropProbe();
    clearMarkers();
    clearHUD();
    clearPrecisionHUD();

    auto* levelSentinel = m_levelSentinel;
    m_levelSentinel = nullptr;
    if (levelSentinel) {
        levelSentinel->disarm();
        if (levelSentinel->getParent()) {
            levelSentinel->removeFromParentAndCleanup(true);
        }
    }

    m_level = nullptr;
    m_platformer = false;
    m_levelDone = false;
    m_resetting = false;
    m_paused = false;
    m_resetDepth = 0;
    resetAttemptState();
}

std::int64_t ProbeController::rawFrame(PlayLayer* layer) const {
    if (!layer) return m_lastFrame;
    const double levelTime = std::max(0.0, static_cast<double>(layer->m_gameState.m_levelTime));
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(levelTime * kPhysicsHz + 1e-7)));
}

void ProbeController::startClock(PlayLayer* layer) {
    if (!layer) return;
    m_clockReady = true;
    m_lastFrame = rawFrame(layer);
    m_attemptStartFrame = m_lastFrame;
}

void ProbeController::beforeReset(PlayLayer* layer) {
    if (layer != m_level) return;
    ++m_resetDepth;
    m_resetting = true;
    m_probing = false;
    m_probePlayer = nullptr;
    m_probeDown = false;
    m_probeJumped = false;
    m_ring = nullptr;
    m_jumps.clear();
    m_sendingJump = false;
    dropProbe();
}

void ProbeController::onReset(PlayLayer* layer) {
    if (layer != m_level) return;
    if (m_resetDepth > 0) --m_resetDepth;
    m_resetting = m_resetDepth > 0;
    if (!m_resetting) {
        m_levelDone = false;
    }

    resetAttemptState();
    if (!m_resetting) {
        ++m_stats.attempts;
    }
}

void ProbeController::handleDeath(PlayLayer* layer) {
    if (!layer || layer != m_level || m_attemptDone) return;
    m_lastFrame = rawFrame(layer);
    m_attemptDone = true;
    for (auto& job : m_clicks) {
        if (!job.canScan) continue;
        if (!job.pressDone) job.pressDone = true;
        if (!job.releaseDone) job.releaseDone = true;
        job.canScan = false;
    }
}

void ProbeController::completeLevel(PlayLayer* layer) {
    if (!layer || layer != m_level || m_levelDone) return;
    m_levelDone = true;
    m_attemptDone = true;
    m_probing = false;
    m_probePlayer = nullptr;
    m_probeDown = false;
    m_probeJumped = false;
    m_ring = nullptr;
    m_jumps.clear();
    m_sendingJump = false;
    m_clockReady = false;
    m_started = false;
    dropProbe();
}

void ProbeController::afterTick(PlayLayer* layer, float dt) {
    if (!layer || layer != m_level) return;

    if (m_cfg.enabled && !m_platformer) {
        moveMarkers(layer, dt);
        if (!m_hud && m_cfg.showCounters) buildHUD();
        if (!m_precHud && m_cfg.showPrecisionHud) buildPrecisionHUD();
    }

    if (m_levelDone || m_platformer || !m_cfg.enabled) return;

    if (!m_started && layer->m_player1 && layer->isGameplayActive()) {
        m_started = true;
        startClock(layer);
    }

    if (m_started && !m_attemptDone && !m_probeLayer) {
        setupProbe(layer);
    }
}

void ProbeController::afterUpdate(GJBaseGameLayer* base) {
    if (!base || m_probing || !m_level) return;
    if (static_cast<GJBaseGameLayer*>(m_level) != base) return;
    if (!m_cfg.enabled || m_platformer || m_resetting || !m_started || m_attemptDone || m_levelDone) return;
    scanClicks(m_level);
}

void ProbeController::onQuit(PlayLayer* layer) {
    if (layer != m_level) return;
    audio::SoundPlayer::get().stopAll();
    resetAllState();
}

void ProbeController::onExit(PlayLayer* layer) {
    if (layer != m_level) return;
    if (m_paused) return;
    audio::SoundPlayer::get().stopAll();
    resetAllState();
}

void ProbeController::pauseStart(PlayLayer* layer) {
    if (layer && layer == m_level) {
        m_paused = true;
    }
}

void ProbeController::pauseEnd(PlayLayer* layer) {
    if (!layer || layer == m_level) {
        m_paused = false;
    }
}

bool ProbeController::beforeButton(PlayerObject* player, PlayerButton button, bool down) {
    if (!player || !m_level || m_replayingInput || m_platformer || !m_cfg.enabled) return true;
    if (player != m_level->m_player1 && player != m_level->m_player2) return true;
    if (m_attemptDone || !m_started) return true;

    if (down && button == PlayerButton::Jump) {
        m_sendingJump = true;
        m_jumpP1 = (player == m_level->m_player1);
    }
    return true;
}

void ProbeController::afterButton(PlayerObject* player, PlayerButton button, bool down, bool accepted) {
    m_sendingJump = false;

    if (!player || !m_level || m_replayingInput || m_platformer || !m_cfg.enabled) return;
    if (m_attemptDone || !m_started) return;

    bool isPlayer1 = false;
    if (player == m_level->m_player1) {
        isPlayer1 = true;
    } else if (player == m_level->m_player2) {
        isPlayer1 = false;
    } else {
        return;
    }

    const auto atFrame = m_clockReady ? rawFrame(m_level) : m_lastFrame;

    InputEvent event;
    event.frame = atFrame;
    event.button = button;
    event.down = down;
    event.player2 = !isPlayer1;
    event.order = m_orderCounter++;
    m_events.push_back(event);

    if (button == PlayerButton::Jump && down) {
        ClickJob job;
        job.pressFrame = atFrame;
        job.pressOrder = event.order;
        job.button = static_cast<int>(button);
        job.player1 = isPlayer1;
        job.canScan = true;
        job.pressPos = player->getPosition();
        job.keepHold = true;
        job.ignoreHit = player->m_isShip || player->m_isDart;
        applyJump(job);

        if (!job.activated && accepted && (isFlight(player) || isUfo(player))) {
            confirmJump(job, atFrame, player->getPosition(), false, false, nullptr, false);
        }

        m_clicks.push_back(job);
    } else if (button == PlayerButton::Jump && !down) {
        for (auto it = m_clicks.rbegin(); it != m_clicks.rend(); ++it) {
            if (it->releaseFrame >= 0) continue;
            if (it->button != static_cast<int>(button) || it->player1 != isPlayer1) continue;
            it->releaseFrame = atFrame;
            it->releaseOrder = event.order;
            it->releasePos = player->getPosition();
            it->ignoreHit = it->ignoreHit || player->m_isShip || player->m_isDart;
            break;
        }
    }
}

void ProbeController::confirmJump(
    ClickJob& job,
    std::int64_t activationFrame,
    CCPoint const& activationPos,
    bool fromRing,
    bool wasBuffered,
    RingObject* ring,
    bool tryHolding
) {
    if (job.activated) {
        if (fromRing) {
            job.fromRing = true;
            job.activationFrame = activationFrame;
            job.activationPos = activationPos;
            if (ring) job.activationRing = ring;
        }
        if (wasBuffered) job.wasBuffered = true;
        job.tryHolding = job.tryHolding || tryHolding;
        return;
    }

    job.activationFrame = activationFrame;
    job.activationPos = activationPos;
    job.activated = true;
    job.fromRing = fromRing;
    job.wasBuffered = wasBuffered || activationFrame > job.pressFrame;
    job.activationRing = ring;
    job.tryHolding = tryHolding;
    job.clickIndex = ++m_jumpCount;
}

void ProbeController::onJumpActive(PlayerObject* player, RingObject* ring) {
    const bool fromRing = (ring != nullptr);
    if (player && m_probing && player == m_probePlayer) {
        if (!m_probeDown) return;
        if (!m_probeJumped) {
            m_probeJumped = true;
            m_probeJumpAt = m_probeAt;
            m_probeRingJump = fromRing;
            m_ring = ring;
        } else if (fromRing && m_probeJumpAt == m_probeAt) {
            m_probeRingJump = true;
            m_ring = ring;
        }
        return;
    }

    if (!player || !m_level || !m_started || m_platformer || !m_cfg.enabled) return;
    if (m_replayingInput || m_attemptDone) return;

    bool isPlayer1 = false;
    if (player == m_level->m_player1) {
        isPlayer1 = true;
    } else if (player == m_level->m_player2) {
        isPlayer1 = false;
    } else {
        return;
    }

    const auto activationFrame = rawFrame(m_level);
    const auto activationPos = player->getPosition();
    const bool wasBuffered = player->m_jumpBuffered || player->m_wasJumpBuffered;
    const bool tryHolding = canHoldCube(player);

    if (fromRing) {
        for (auto it = m_clicks.rbegin(); it != m_clicks.rend(); ++it) {
            if (!it->activated || it->player1 != isPlayer1) continue;
            if (it->activationFrame < activationFrame) break;
            if (it->activationFrame != activationFrame) continue;
            confirmJump(*it, activationFrame, activationPos, true, wasBuffered, ring, tryHolding);
            return;
        }
    }

    for (auto it = m_clicks.rbegin(); it != m_clicks.rend(); ++it) {
        if (it->activated || it->player1 != isPlayer1) continue;
        if (it->pressFrame > activationFrame) continue;
        confirmJump(*it, activationFrame, activationPos, fromRing, wasBuffered, ring, tryHolding);
        return;
    }

    if (!m_sendingJump || m_jumpP1 != isPlayer1) return;

    for (auto& pending : m_jumps) {
        if (pending.player1 == isPlayer1 && pending.frame == activationFrame) {
            if (fromRing) {
                pending.fromRing = true;
                pending.ring = ring;
                pending.position = activationPos;
            }
            if (wasBuffered) pending.wasBuffered = true;
            pending.tryHolding = pending.tryHolding || tryHolding;
            return;
        }
    }

    JumpEvent pending;
    pending.frame = activationFrame;
    pending.player1 = isPlayer1;
    pending.position = activationPos;
    pending.fromRing = fromRing;
    pending.wasBuffered = wasBuffered;
    pending.ring = ring;
    pending.tryHolding = tryHolding;
    m_jumps.push_back(pending);
    while (m_jumps.size() > 8) {
        m_jumps.pop_front();
    }
}

void ProbeController::applyJump(ClickJob& job) {
    for (auto it = m_jumps.begin(); it != m_jumps.end(); ++it) {
        if (it->player1 != job.player1) continue;
        if (it->frame < job.pressFrame || it->frame > job.pressFrame + 1) continue;
        const auto f = it->frame;
        const auto pos = it->position;
        const bool fromRing = it->fromRing;
        const bool wasBuffered = it->wasBuffered;
        auto* ring = it->ring;
        const bool tryHolding = it->tryHolding;
        m_jumps.erase(it);
        confirmJump(job, f, pos, fromRing, wasBuffered, ring, tryHolding);
        return;
    }
}

bool ProbeController::ringBuffered(ClickJob const& job) const {
    return job.activated && job.fromRing && job.activationFrame >= 0 && job.activationFrame > job.pressFrame;
}

bool ProbeController::noReleaseWin(ClickJob const& job) const {
    return job.releaseFrame < job.pressFrame || job.releaseOrder == kNoOrder;
}

std::int64_t ProbeController::pressFrameOf(ClickJob const& job) const {
    return ringBuffered(job) ? job.activationFrame : job.pressFrame;
}

std::int64_t ProbeController::releaseFrameOf(ClickJob const& job) const {
    if (job.releaseFrame < 0) return -1;
    if (ringBuffered(job)) {
        return std::max(job.releaseFrame, job.activationFrame);
    }
    return job.releaseFrame;
}

bool ProbeController::activeLayer(GJBaseGameLayer* layer) const {
    return m_level && static_cast<GJBaseGameLayer*>(m_level) == layer;
}

bool ProbeController::isProbe(PlayerObject* player) const {
    return player && (player == m_probeP1 || player == m_probeP2);
}

bool ProbeController::skipFx(GJBaseGameLayer* layer) const {
    return m_probing && activeLayer(layer);
}

bool ProbeController::skipPlayerFx(PlayerObject* player) const {
    return player && m_probing && isProbe(player);
}

bool ProbeController::hideGround(GJGroundLayer* ground) const {
    if (!m_probing || !m_level || !ground) return false;
    return ground == m_ground1 || ground == m_ground2 ||
           ground == m_level->m_groundLayer || ground == m_level->m_groundLayer2;
}

bool ProbeController::usedPortal(GameObject* object) const {
    return object && std::find(m_portals.begin(), m_portals.end(), object) != m_portals.end();
}

void ProbeController::addPortal(GameObject* object) {
    if (object && !usedPortal(object)) {
        m_portals.push_back(object);
    }
}

void ProbeController::clearProbeHit() {
    if (!m_probing || !m_level) return;
    m_level->m_isDebugDrawEnabled = false;
    m_level->m_hitboxesOnDeath = false;
    m_level->m_playerDied = false;
    if (m_level->m_debugDrawNode) {
        m_level->m_debugDrawNode->setVisible(false);
    }
}

void ProbeController::probeDeath(PlayerObject* player, GameObject* object) {
    if (!m_probing || !isProbe(player)) return;
    clearProbeHit();
    m_probeDead = true;
    m_probeHit = object;
    m_probeDeath = m_probeAt;
    if (player) player->m_isDead = false;
    clearProbeHit();
}

void ProbeController::probeDone() {
    if (m_probing) m_probeDone = true;
}

bool ProbeController::isFlight(PlayerObject const* player) {
    return player && (player->m_isShip || player->m_isDart || player->m_isSwing);
}

bool ProbeController::isUfo(PlayerObject const* player) {
    return player && player->m_isBird;
}

int ProbeController::savedMode(PlayerCheckpoint const* state) {
    if (!state) return 0;
    if (state->m_isShip) return 1;
    if (state->m_isBall) return 2;
    if (state->m_isBird) return 3;
    if (state->m_isSwing) return 4;
    if (state->m_isDart) return 5;
    if (state->m_isRobot) return 6;
    if (state->m_isSpider) return 7;
    return 0;
}

int ProbeController::playerMode(PlayerObject const* player) {
    if (!player) return 0;
    if (player->m_isShip) return 1;
    if (player->m_isBall) return 2;
    if (player->m_isBird) return 3;
    if (player->m_isSwing) return 4;
    if (player->m_isDart) return 5;
    if (player->m_isRobot) return 6;
    if (player->m_isSpider) return 7;
    return 0;
}

bool ProbeController::canHoldCube(PlayerObject const* player) const {
    if (!player || !m_level || m_platformer) return false;
    if (m_level->m_gameState.m_isDualMode) return false;
    return playerMode(player) == 0;
}

void ProbeController::stopTrail(PlayerObject* player) {
    if (!player) return;
    if (auto* trail = player->m_ghostTrail) {
        trail->stopTrail();
        trail->unscheduleAllSelectors();
    }
}

void ProbeController::syncMode(PlayerObject* player, PlayerCheckpoint* state) {
    if (!player || !state) return;

    const int expectedMode = savedMode(state);
    const int actualMode = playerMode(player);
    const bool modeMismatch = (expectedMode != actualMode);
    const bool speedMismatch = std::abs(player->m_playerSpeed - state->m_playerSpeed) > 0.0001f;
    if (!modeMismatch && !speedMismatch) return;

    if (modeMismatch) {
        if (player->m_isShip) player->toggleFlyMode(false, true);
        if (player->m_isBall) player->toggleRollMode(false, true);
        if (player->m_isBird) player->toggleBirdMode(false, true);
        if (player->m_isSwing) player->toggleSwingMode(false, true);
        if (player->m_isDart) player->toggleDartMode(false, true);
        if (player->m_isRobot) player->toggleRobotMode(false, true);
        if (player->m_isSpider) player->toggleSpiderMode(false, true);

        switch (expectedMode) {
            case 1: player->toggleFlyMode(true, true); break;
            case 2: player->toggleRollMode(true, true); break;
            case 3: player->toggleBirdMode(true, true); break;
            case 4: player->toggleSwingMode(true, true); break;
            case 5: player->toggleDartMode(true, true); break;
            case 6: player->toggleRobotMode(true, true); break;
            case 7: player->toggleSpiderMode(true, true); break;
            default: break;
        }
        player->loadFromCheckpoint(state);
    }

    player->m_playerSpeed = state->m_playerSpeed;
}

}
