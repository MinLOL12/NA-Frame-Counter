#include "ProbeController.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

using namespace geode::prelude;
using namespace cocos2d;

namespace na_frame_counter::engine {

SpriteBackup SpriteBackup::capture(CCSprite* spr) {
    SpriteBackup state;
    state.sprite = spr;
    if (!spr) return state;
    state.visible = spr->isVisible();
    state.opacity = spr->getOpacity();
    state.position = spr->getPosition();
    state.scaleX = spr->getScaleX();
    state.scaleY = spr->getScaleY();
    return state;
}

void SpriteBackup::restore() const {
    if (!sprite) return;
    sprite->setVisible(visible);
    sprite->setOpacity(opacity);
    sprite->setPosition(position);
    sprite->setScaleX(scaleX);
    sprite->setScaleY(scaleY);
}

GroundState GroundState::capture(GJGroundLayer* layer) {
    GroundState state;
    state.layer = layer;
    if (!layer) return state;
    state.visible = layer->isVisible();
    state.position = layer->getPosition();
    state.scaleX = layer->getScaleX();
    state.scaleY = layer->getScaleY();
    state.showGround = layer->m_showGround;
    state.showGround1 = layer->m_showGround1;
    state.showGround2 = layer->m_showGround2;
    state.ground1 = SpriteBackup::capture(layer->m_ground1Sprite);
    state.ground2 = SpriteBackup::capture(layer->m_ground2Sprite);
    state.line = SpriteBackup::capture(layer->m_lineSprite);
    return state;
}

void GroundState::restore() const {
    if (!layer) return;
    layer->setVisible(visible);
    layer->setPosition(position);
    layer->setScaleX(scaleX);
    layer->setScaleY(scaleY);
    layer->m_showGround = showGround;
    layer->m_showGround1 = showGround1;
    layer->m_showGround2 = showGround2;
    ground1.restore();
    ground2.restore();
    line.restore();
}

PlayerObject* ProbeController::createProbePlayer(PlayLayer* layer) {
    if (!layer || !m_probeLayer) return nullptr;
    auto* player = PlayerObject::create(1, 1, layer, m_probeLayer, true);
    if (!player) return nullptr;
    stopTrail(player);
    player->stopParticles();
    player->setVisible(false);
    player->setOpacity(0);
    m_probeLayer->addChild(player, -100000);
    return player;
}

void ProbeController::clearHistory() {
    auto resetSlot = [](HistorySlot& slot) {
        if (slot.state) slot.state->release();
        slot.state = nullptr;
        slot.frame = -1;
        slot.held = {};
        slot.totalTime = 0.0;
        slot.touchingRings.clear();
        slot.touchedRings.clear();
        slot.ringRelatedSet.clear();
        slot.touchedRing = false;
        slot.touchedCustomRing = false;
        slot.stateRingJump = false;
        slot.stateRingJump2 = false;
        slot.ringJumpRelated = false;
        slot.preInput = false;
        slot.controlsDisabled = false;
        slot.valid = false;
    };

    for (auto& playerHistory : m_history) {
        for (auto& slot : playerHistory) {
            resetSlot(slot);
        }
    }
    for (auto& preSlot : m_preStartSlot) {
        resetSlot(preSlot);
    }
    m_lastHistFrame = {-1, -1};
}

void ProbeController::dropProbe() {
    m_probing = false;
    m_probePlayer = nullptr;
    m_ring = nullptr;
    m_portals.clear();
    stopTrail(m_probeP1);
    stopTrail(m_probeP2);
    m_probeP1 = nullptr;
    m_probeP2 = nullptr;
    m_probeG1 = nullptr;
    m_probeG2 = nullptr;
    m_ground1 = nullptr;
    m_ground2 = nullptr;
    if (m_probeLayer) {
        m_probeLayer->removeFromParentAndCleanup(true);
        m_probeLayer = nullptr;
    }
    clearHistory();
}

void ProbeController::setupProbe(PlayLayer* layer) {
    if (!layer || !layer->m_objectLayer || m_probeLayer || m_platformer) return;
    dropProbe();

    m_probeLayer = CCLayer::create();
    if (!m_probeLayer) return;
    m_probeLayer->setVisible(false);
    m_probeLayer->setID("probe-scratch"_spr);
    layer->m_objectLayer->addChild(m_probeLayer, -1000000);

    m_probeG1 = GJGroundLayer::create(1, 0);
    m_probeG2 = GJGroundLayer::create(1, 0);
    if (m_probeG1) {
        m_probeG1->setVisible(false);
        m_probeLayer->addChild(m_probeG1);
    }
    if (m_probeG2) {
        m_probeG2->setVisible(false);
        m_probeLayer->addChild(m_probeG2);
    }

    m_probeP1 = createProbePlayer(layer);
    m_probeP2 = createProbePlayer(layer);

    m_lastHistFrame = {-1, -1};
    const auto startF = rawFrame(layer);
    if (layer->m_player1) captureSlot(layer, layer->m_player1, m_preStartSlot[0], startF, true);
    if (layer->m_player2) captureSlot(layer, layer->m_player2, m_preStartSlot[1], startF, true);
}

void ProbeController::captureSlot(
    PlayLayer* layer,
    PlayerObject* player,
    HistorySlot& slot,
    std::int64_t f,
    bool preInput
) {
    if (!layer || !player) return;
    if (!slot.state) {
        slot.state = PlayerCheckpoint::create();
        if (slot.state) slot.state->retain();
    }
    if (!slot.state) return;

    player->saveToCheckpoint(slot.state);
    slot.frame = f;
    slot.totalTime = player->m_totalTime;
    slot.preInput = preInput;
    slot.controlsDisabled = player->m_controlsDisabled;
    slot.valid = true;
    for (int btn = static_cast<int>(PlayerButton::Jump);
         btn <= static_cast<int>(PlayerButton::Right); ++btn) {
        auto it = player->m_holdingButtons.find(btn);
        slot.held[btn] = (it != player->m_holdingButtons.end()) && it->second;
    }

    slot.touchingRings.clear();
    if (player->m_touchingRings) {
        const auto count = player->m_touchingRings->count();
        slot.touchingRings.reserve(count);
        for (unsigned int i = 0; i < count; ++i) {
            if (auto* ring = typeinfo_cast<RingObject*>(player->m_touchingRings->objectAtIndex(i))) {
                slot.touchingRings.push_back(ring);
            }
        }
    }
    slot.touchedRings = player->m_touchedRings;
    slot.ringRelatedSet = player->m_ringRelatedSet;
    slot.touchedRing = player->m_touchedRing;
    slot.touchedCustomRing = player->m_touchedCustomRing;
    slot.stateRingJump = player->m_stateRingJump;
    slot.stateRingJump2 = player->m_stateRingJump2;
    slot.ringJumpRelated = player->m_ringJumpRelated;
}

void ProbeController::saveHistory(PlayLayer* layer, PlayerObject* player) {
    if (!layer || layer != m_level || !player || m_probing || !m_started ||
        m_platformer || !m_cfg.enabled || m_attemptDone || m_levelDone) {
        return;
    }
    if (!m_probeLayer) return;

    int index = -1;
    if (player == layer->m_player1) index = 0;
    else if (player == layer->m_player2) index = 1;
    else return;

    const auto f = rawFrame(layer);
    if (m_lastHistFrame[index] == f) return;

    auto& slot = m_history[index][static_cast<std::size_t>(f % kHistSize)];
    captureSlot(layer, player, slot, f, false);
    m_lastHistFrame[index] = f;
}

HistorySlot const* ProbeController::findHistory(bool player1, std::int64_t desiredFrame, bool needPreInput) const {
    const int index = player1 ? 0 : 1;
    if (needPreInput && m_preStartSlot[index].valid && m_preStartSlot[index].state) {
        return &m_preStartSlot[index];
    }

    if (desiredFrame < m_attemptStartFrame) desiredFrame = m_attemptStartFrame;
    const auto span = std::max<std::int64_t>(0, desiredFrame - m_attemptStartFrame);
    const auto maxBack = std::min<std::int64_t>(span, static_cast<std::int64_t>(kHistSize - 1));
    for (std::int64_t back = 0; back <= maxBack; ++back) {
        const auto f = desiredFrame - back;
        auto const& slot = m_history[index][static_cast<std::size_t>(f % kHistSize)];
        if (slot.valid && slot.frame == f && slot.state) {
            return &slot;
        }
    }
    if (desiredFrame == m_attemptStartFrame && m_preStartSlot[index].valid && m_preStartSlot[index].state) {
        return &m_preStartSlot[index];
    }
    return nullptr;
}

bool ProbeController::probeReady(PlayerObject* player) const {
    if (!player || isFlight(player)) return false;
    const bool grounded =
        player->m_stateOnGround != 0 ||
        player->m_isOnGround2 ||
        player->m_isOnGround3 ||
        player->m_isOnGround4 ||
        player->m_lastGroundObject != nullptr;
    return grounded && std::abs(player->getYVelocity()) < 0.05;
}

void ProbeController::probeInput(
    PlayerObject* probe,
    InputEvent const& event,
    bool pressEvent,
    bool forceEnableControls,
    bool forceDisableAfter
) {
    if (!probe) return;
    if (event.holdOnly) {
        probe->m_holdingButtons[static_cast<int>(event.button)] = event.down;
        return;
    }

    if (forceEnableControls) {
        probe->m_controlsDisabled = false;
        probe->m_isLocked = false;
    }

    if (pressEvent) m_probeDown = true;
    m_replayingInput = true;
    if (event.down) probe->pushButton(event.button);
    else probe->releaseButton(event.button);
    m_replayingInput = false;

    if (forceDisableAfter) {
        probe->m_controlsDisabled = true;
    }

    if (pressEvent && !m_probeJumped && (isFlight(probe) || isUfo(probe) || probe->m_isDashing)) {
        m_probeJumped = true;
        m_probeJumpAt = m_probeAt;
        m_probeRingJump = false;
    }
}

PathState ProbeController::runPath(
    PlayLayer* layer,
    ClickJob const& job,
    ScanAxis axis,
    int offset,
    bool removeTarget,
    std::int64_t stopAt
) {
    PathState result;
    if (!layer || !m_probeLayer) return result;
    auto* probe = job.player1 ? m_probeP1 : m_probeP2;
    if (!probe) return result;

    const bool bufferedRing = ringBuffered(job);
    const bool cubePress = (axis == ScanAxis::Press) &&
        job.tryHolding && job.holdReady && job.heldFrames >= 1;

    const auto pressAt = pressFrameOf(job);
    const auto releaseAt = releaseFrameOf(job);
    std::int64_t targetPress = pressAt;
    std::int64_t targetRelease = releaseAt;

    if (removeTarget) {
        if (axis == ScanAxis::Release) {
            if (releaseAt < pressAt) return result;
            targetRelease = -1; // Simulate holding without releasing at releaseAt
        }
    } else {
        if (axis == ScanAxis::Release) {
            if (releaseAt < pressAt) return result;
            targetRelease = releaseAt + offset;
            if (targetRelease < targetPress) return result;
        } else {
            targetPress = std::max<std::int64_t>(m_attemptStartFrame, pressAt + offset);
            if (cubePress) {
                targetRelease = targetPress + job.heldFrames;
            } else if (!job.keepHold || releaseAt < pressAt) {
                targetRelease = -1;
            } else {
                const auto holdFrames = releaseAt - pressAt;
                targetRelease = targetPress + holdFrames;
            }
        }
    }

    result.targetPressAt = (removeTarget && axis == ScanAxis::Press) ? -1 : targetPress;
    result.targetReleaseAt = removeTarget ? -1 : targetRelease;

    std::int64_t earliest = pressAt;
    if (axis == ScanAxis::Release && releaseAt >= pressAt) {
        earliest = removeTarget ? releaseAt : std::min(releaseAt, targetRelease);
    } else if (!removeTarget) {
        earliest = std::min(earliest, targetPress);
    }

    const bool needPreInput = (earliest <= m_attemptStartFrame) &&
        !(axis == ScanAxis::Release && job.activated && job.activationFrame > m_attemptStartFrame);
    const auto desiredSource = std::max<std::int64_t>(m_attemptStartFrame, earliest - 1);
    auto const* source = findHistory(job.player1, desiredSource, needPreInput);
    if (!source || !source->state) return result;

    const bool usePreInput = source->preInput && (source->frame == m_attemptStartFrame);

    const bool oldDebugDraw = layer->m_isDebugDrawEnabled;
    const bool oldHitboxOff = layer->m_disablePlayerHitbox;
    const bool oldHitboxes = layer->m_hitboxesOnDeath;
    const bool oldDead = layer->m_playerDied;
    auto* oldDebugNode = layer->m_debugDrawNode;
    const bool oldDebugVisible = oldDebugNode ? oldDebugNode->isVisible() : false;

    auto* const liveGround1 = layer->m_groundLayer;
    auto* const liveGround2 = layer->m_groundLayer2;
    m_ground1 = liveGround1;
    m_ground2 = liveGround2;
    const auto savedGround1 = GroundState::capture(liveGround1);
    const auto savedGround2 = GroundState::capture(liveGround2);

    const auto oldState = StateBackup::capture(layer->m_gameState);
    const bool oldHideGround = layer->m_hideGround;

    auto* const objectLayer = layer->m_objectLayer;
    const auto savedLayerPos = objectLayer ? objectLayer->getPosition() : CCPoint{0.f, 0.f};
    const float oldScaleX = objectLayer ? objectLayer->getScaleX() : 1.f;
    const float oldScaleY = objectLayer ? objectLayer->getScaleY() : 1.f;
    const float oldAngle = objectLayer ? objectLayer->getRotation() : 0.f;
    const bool oldVisible = objectLayer ? objectLayer->isVisible() : true;

    m_probing = true;

    if (m_probeG1) layer->m_groundLayer = m_probeG1;
    if (m_probeG2) layer->m_groundLayer2 = m_probeG2;
    clearProbeHit();
    m_probeDead = false;
    m_probeDone = false;
    m_probeHit = nullptr;
    m_probeDeath = -1;
    m_probePlayer = probe;
    m_probeDown = false;
    m_probeJumped = false;
    m_probeJumpAt = -1;
    m_probeRingJump = false;
    m_ring = nullptr;
    m_portals.clear();

    probe->loadFromCheckpoint(source->state);
    syncMode(probe, source->state);
    probe->m_totalTime = source->totalTime;
    probe->m_controlsDisabled = source->controlsDisabled;
    for (int btn = static_cast<int>(PlayerButton::Jump);
         btn <= static_cast<int>(PlayerButton::Right); ++btn) {
        probe->m_holdingButtons[btn] = source->held[btn];
    }

    const bool alreadyActive = (axis == ScanAxis::Release) &&
        job.activated && job.activationFrame >= 0 &&
        !usePreInput && source->frame >= job.activationFrame;
    if (bufferedRing && !alreadyActive) {
        probe->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
        probe->m_jumpBuffered = false;
        probe->m_wasJumpBuffered = false;
    }

    const bool holdWave = (axis == ScanAxis::Release) &&
        probe->m_isDart &&
        ((!removeTarget && targetRelease >= 0 && alreadyActive) || removeTarget);
    if (holdWave) {
        m_replayingInput = true;
        probe->m_controlsDisabled = false;
        probe->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
        probe->pushButton(PlayerButton::Jump);
        m_replayingInput = false;
        probe->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
    }
    probe->m_isDead = false;
    probe->setVisible(false);
    probe->setOpacity(0);

    probe->resetTouchedRings(false);
    if (probe->m_touchingRings) {
        probe->m_touchingRings->removeAllObjects();
        for (auto* ring : source->touchingRings) {
            if (ring) probe->m_touchingRings->addObject(ring);
        }
    }
    probe->m_touchedRings = source->touchedRings;
    probe->m_ringRelatedSet = source->ringRelatedSet;
    probe->m_touchedRing = source->touchedRing;
    probe->m_touchedCustomRing = source->touchedCustomRing;
    probe->m_stateRingJump = source->stateRingJump;
    probe->m_stateRingJump2 = source->stateRingJump2;
    probe->m_ringJumpRelated = source->ringJumpRelated;

    probe->resetCollisionLog(false);
    result.started = true;

    if (axis == ScanAxis::Release && source->frame >= job.pressFrame) {
        m_probeDown = true;
    }
    if (alreadyActive) {
        m_probeDown = true;
        m_probeJumped = true;
        m_probeJumpAt = job.activationFrame;
        m_probeRingJump = job.fromRing;
        m_ring = job.activationRing;
        result.activationFrame = job.activationFrame;
        result.activationPos = job.activationPos;
    }

    m_probeEvents.clear();
    if (m_probeEvents.capacity() < m_events.size()) {
        m_probeEvents.reserve(m_events.size());
    }
    auto& playback = m_probeEvents;

    auto cutoffOrder = job.pressOrder;
    if (!cubePress && (axis == ScanAxis::Release || job.keepHold) && job.releaseOrder != kNoOrder) {
        cutoffOrder = job.releaseOrder;
    }

    const bool removePressOnly = removeTarget && (axis == ScanAxis::Press);
    const bool removeReleaseOnly = removeTarget;

    for (auto event : m_events) {
        if (event.order > cutoffOrder) continue;
        if (event.player2 == job.player1) continue;
        const bool isPress = (event.order == job.pressOrder);
        const bool isRelease = (job.releaseOrder != kNoOrder && event.order == job.releaseOrder);
        const bool moveRelease = isRelease && !isPress;

        if (isPress && removePressOnly) continue;
        if (moveRelease && (removeReleaseOnly || cubePress || (axis == ScanAxis::Press && !job.keepHold))) continue;
        if (isPress) event.frame = targetPress;
        if (moveRelease) event.frame = targetRelease;
        if (usePreInput ? (event.frame >= source->frame) : (event.frame > source->frame)) {
            playback.push_back(event);
        }
    }

    if (cubePress && !removePressOnly && targetRelease >= 0) {
        InputEvent fakeRelease;
        fakeRelease.frame = targetRelease;
        fakeRelease.button = static_cast<PlayerButton>(job.button);
        fakeRelease.down = false;
        fakeRelease.player2 = !job.player1;
        fakeRelease.order = job.pressOrder + 1;
        if (usePreInput ? (fakeRelease.frame >= source->frame) : (fakeRelease.frame > source->frame)) {
            playback.push_back(fakeRelease);
        }
    }
    sortInputs(playback);

    m_probeControlEvents.clear();
    auto& controlPlayback = m_probeControlEvents;
    for (auto const& ce : m_controlEvents) {
        if (ce.order > cutoffOrder) continue;
        if (ce.player1 != job.player1) continue;
        if (job.pressControlOrder != kNoOrder && ce.order == job.pressControlOrder) continue;
        const bool isJobReleaseCtrl = (job.releaseControlOrder != kNoOrder && ce.order == job.releaseControlOrder);
        if (isJobReleaseCtrl &&
            (removeTarget || cubePress || axis == ScanAxis::Release || job.fromControlEnable)) {
            continue;
        }
        if (usePreInput ? (ce.frame >= source->frame) : (ce.frame > source->frame)) {
            controlPlayback.push_back(ce);
        }
    }

    std::size_t playbackIndex = 0;
    std::size_t controlIndex = 0;
    std::int64_t now = usePreInput ? (source->frame - 1) : source->frame;
    const auto groundEnd = cubePress ? kCubeLimit : kGroundMax;
    const auto jumpDeadline =
        std::max(targetPress, targetRelease >= 0 ? targetRelease : targetPress) + groundEnd;
    std::int64_t hardStop = (stopAt >= 0) ? stopAt : jumpDeadline;
    std::int64_t minEnd = -1;
    int settledFrames = 0;
    bool flying = false;

    if (alreadyActive) {
        flying = isFlight(probe);
        const auto effectStart = std::max<std::int64_t>(
            targetRelease >= 0 ? targetRelease : targetPress,
            result.activationFrame
        );
        minEnd = effectStart + kReleaseDelay;
        if (stopAt < 0) {
            hardStop = effectStart + (flying ? kFlightMax : groundEnd);
        }
    }

    const auto safetyStop = std::max<std::int64_t>(
        hardStop,
        source->frame + (cubePress ? kHoldLimit : kProbeLimit) + kPadding
    );

    while (!m_probeDead && !m_probeDone && now < safetyStop) {
        ++now;
        m_probeAt = now;

        if (holdWave && ((removeTarget && axis == ScanAxis::Release) || now < targetRelease)) {
            probe->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
        }

        // Apply interleaved ControlEvents and InputEvents in exact chronological (frame, order) order
        while (true) {
            const bool hasInput = (playbackIndex < playback.size() && playback[playbackIndex].frame <= now);
            const bool hasCtrl = (controlIndex < controlPlayback.size() && controlPlayback[controlIndex].frame <= now);
            if (!hasInput && !hasCtrl) break;

            bool runCtrlNext = false;
            if (hasCtrl && !hasInput) {
                runCtrlNext = true;
            } else if (hasCtrl && hasInput) {
                auto const& ev = playback[playbackIndex];
                auto const& ce = controlPlayback[controlIndex];
                if (ce.frame < ev.frame || (ce.frame == ev.frame && ce.order < ev.order)) {
                    runCtrlNext = true;
                }
            }

            if (runCtrlNext) {
                auto const& ce = controlPlayback[controlIndex++];
                probe->m_controlsDisabled = ce.disabled;
            } else {
                auto const& event = playback[playbackIndex++];
                const bool isTargetPress = (event.order == job.pressOrder);
                const bool isTargetRelease = (job.releaseOrder != kNoOrder && event.order == job.releaseOrder);
                const bool forceEnable =
                    (isTargetPress && job.fromControlEnable) ||
                    (isTargetRelease && (job.fromControlEnable || job.fromControlDisable));
                const bool forceDisableAfter =
                    isTargetRelease && job.fromControlDisable && !cubePress;
                probeInput(probe, event, isTargetPress, forceEnable, forceDisableAfter);
            }
        }

        probe->resetTouchedRings(false);
        probe->resetCollisionLog(false);
        constexpr float playerStep = 60.f / static_cast<float>(kPhysicsHz);
        constexpr float timeStep = 1.f / static_cast<float>(kPhysicsHz);
        probe->m_totalTime += timeStep;
        probe->updateInternalActions(timeStep);
        probe->update(playerStep);

        clearProbeHit();
        if (!m_probeDead) {
            layer->checkCollisions(probe, playerStep, false);
        }

        clearProbeHit();
        probe->m_isDead = false;
        if (!m_probeDead) {
            probe->updateSpecial(timeStep);
        }
        probe->m_isDead = false;

        if (m_probeJumped && result.activationFrame < 0) {
            result.activationFrame = (m_probeJumpAt >= 0) ? m_probeJumpAt : now;
            result.activationPos = probe->getPosition();
            flying = isFlight(probe);
            const auto effectStart = std::max<std::int64_t>(
                targetRelease >= 0 ? targetRelease : targetPress,
                result.activationFrame
            );
            minEnd = effectStart + kReleaseDelay;
            if (stopAt < 0) {
                hardStop = effectStart + (flying ? kFlightMax : groundEnd);
            }
        }

        if (m_probeJumped && stopAt < 0) {
            const bool nowFlying = isFlight(probe);
            if (nowFlying != flying) {
                flying = nowFlying;
                hardStop = now + (flying ? kFlightMax : groundEnd);
                settledFrames = 0;
            }
        }

        if (stopAt >= 0) {
            if (now >= stopAt) break;
            continue;
        }

        if (!m_probeJumped) {
            if (now >= jumpDeadline) break;
            continue;
        }
        if (minEnd >= 0 && now < minEnd) continue;
        if (flying) {
            if (now >= hardStop) break;
            continue;
        }
        if (probeReady(probe)) {
            ++settledFrames;
            if (settledFrames >= kGroundSteps) break;
        } else {
            settledFrames = 0;
        }
        if (now >= hardStop) break;
    }

    result.activated = m_probeJumped;
    result.fromRing = m_probeRingJump;
    result.activationRing = m_ring;
    result.died = m_probeDead;
    result.deathFrame = m_probeDeath;
    result.deathObject = m_probeHit;
    result.resolveFrame = result.died ? result.deathFrame : now;
    result.finalPosition = probe->getPosition();
    result.survivedHorizon = !result.died && result.activated &&
        (m_probeDone || stopAt < 0 || now >= stopAt);

    layer->m_groundLayer = liveGround1;
    layer->m_groundLayer2 = liveGround2;
    savedGround1.restore();
    savedGround2.restore();

    oldState.restore(layer->m_gameState);
    layer->m_hideGround = oldHideGround;
    if (objectLayer) {
        objectLayer->setPosition(savedLayerPos);
        objectLayer->setScaleX(oldScaleX);
        objectLayer->setScaleY(oldScaleY);
        objectLayer->setRotation(oldAngle);
        objectLayer->setVisible(oldVisible);
    }
    m_ground1 = nullptr;
    m_ground2 = nullptr;

    layer->m_isDebugDrawEnabled = oldDebugDraw;
    layer->m_disablePlayerHitbox = oldHitboxOff;
    layer->m_hitboxesOnDeath = oldHitboxes;
    layer->m_playerDied = oldDead;
    if (oldDebugNode && layer->m_debugDrawNode == oldDebugNode) {
        oldDebugNode->setVisible(oldDebugVisible);
    }

    m_probing = false;
    m_probePlayer = nullptr;
    m_probeDown = false;
    m_portals.clear();
    probe->m_isDead = false;
    probe->setVisible(false);
    return result;
}

bool ProbeController::sameJump(ClickJob const& job, PathState const& result) const {
    if (!result.started || !result.activated || result.activationFrame < 0) return false;
    if (!job.fromRing) return true;
    if (!result.fromRing) return false;
    if (result.targetPressAt >= 0 && result.activationFrame != result.targetPressAt) return false;
    if (job.activationRing && result.activationRing && job.activationRing != result.activationRing) return false;
    return true;
}

float ProbeController::releaseProgress(ClickJob const& job, PathState const& result) const {
    const auto origin = job.activated ? job.activationPos : job.pressPos;
    return std::abs(result.finalPosition.x - origin.x);
}

bool ProbeController::pickCubeHold(PlayLayer* layer, ClickJob& job) {
    if (!job.tryHolding) return false;
    if (job.holdReady) return job.holdSafe;
    job.holdReady = true;
    job.holdSafe = false;
    job.needsRelease = false;
    if (!layer || !job.activated) return false;

    float bestProgress = -1.f;
    int bestDelay = 1;
    bool bestSafe = false;
    bool gotUsable = false;
    bool seen[241] = {};
    int coarseBest = 1;

    for (int i = 0; i < kHoldCount + 2 * kHoldRefine + 1; ++i) {
        if (i == kHoldCount) {
            if (!gotUsable) break;
            coarseBest = bestDelay;
        }
        const int delay = (i < kHoldCount)
            ? kHoldSteps[i]
            : (coarseBest + i - kHoldCount - kHoldRefine);
        if (delay < 1 || delay > 240 || seen[delay]) continue;
        seen[delay] = true;

        ClickJob trial = job;
        trial.heldFrames = delay;
        trial.holdReady = true;
        trial.holdSafe = false;
        const PathState result = runPath(layer, trial, ScanAxis::Press, 0, false, -1);
        if (!sameJump(trial, result)) continue;

        const bool safe = !result.died;
        const float progress = releaseProgress(trial, result);
        bool better = !gotUsable || (safe && !bestSafe);
        if (gotUsable && safe == bestSafe) {
            better = progress > bestProgress + kReleaseTol ||
                (std::abs(progress - bestProgress) <= kReleaseTol && delay < bestDelay);
        }
        if (!better) continue;
        bestDelay = delay;
        bestProgress = progress;
        bestSafe = safe;
        gotUsable = true;
    }

    job.heldFrames = bestDelay;
    job.holdSafe = gotUsable && bestSafe;
    job.needsRelease = !job.holdSafe;
    return job.holdSafe;
}

bool ProbeController::pathValid(
    ClickJob const& job,
    PathState const& baseline,
    PathState const& candidate,
    ScanAxis axis
) const {
    if (!sameJump(job, candidate)) return false;

    if (job.fromRing && baseline.targetPressAt >= 0 && candidate.targetPressAt >= 0) {
        if (!candidate.fromRing) return false;
        const auto baseDelay = std::max<std::int64_t>(0, baseline.activationFrame - baseline.targetPressAt);
        const auto holdDelay = std::max<std::int64_t>(0, candidate.activationFrame - candidate.targetPressAt);
        if (holdDelay > baseDelay) return false;
    }

    std::int64_t frameShift = 0;
    std::int64_t baseFrame = pressFrameOf(job);
    if (axis == ScanAxis::Release &&
        baseline.targetReleaseAt >= 0 && candidate.targetReleaseAt >= 0) {
        frameShift = candidate.targetReleaseAt - baseline.targetReleaseAt;
        baseFrame = baseline.targetReleaseAt;
    }

    const auto sameEnd = baseline.resolveFrame + frameShift;
    const auto sameDeath = baseline.died ? (baseline.deathFrame + frameShift) : baseline.deathFrame;

    if (!candidate.died) {
        return candidate.survivedHorizon || candidate.resolveFrame >= sameEnd;
    }
    if (!baseline.died) return false;
    if (candidate.deathFrame >= sameDeath) return true;

    const bool sameObject = candidate.deathObject && baseline.deathObject &&
        candidate.deathObject == baseline.deathObject;
    const bool deathMatches = sameObject || (axis == ScanAxis::Release && job.ignoreHit);
    if (!deathMatches || sameDeath <= baseFrame + frameShift) return false;

    const auto compareFrames = std::max<std::int64_t>(1, baseline.resolveFrame - baseFrame);
    const auto earlyBy = sameDeath - candidate.deathFrame;
    return earlyBy < compareFrames * kDeathTol;
}

bool ProbeController::checkRange(
    PlayLayer* layer,
    ClickJob const& job,
    PathState const& baseline,
    ScanAxis axis,
    bool earlier,
    int depth
) {
    const int offset = earlier ? -depth : depth;
    std::int64_t stop = baseline.resolveFrame;
    if (axis == ScanAxis::Release && baseline.targetReleaseAt >= 0) {
        stop += offset;
    }
    PathState result = runPath(layer, job, axis, offset, false, stop);
    return pathValid(job, baseline, result, axis);
}

int ProbeController::scanDepth(
    PlayLayer* layer,
    ClickJob const& job,
    PathState const& baseline,
    ScanAxis axis,
    bool earlier,
    int maxDepth
) {
    if (maxDepth <= 0) return 0;
    if (earlier) {
        const auto legal = (axis == ScanAxis::Release)
            ? (releaseFrameOf(job) - pressFrameOf(job))
            : (pressFrameOf(job) - m_attemptStartFrame);
        if (legal <= 0) return 0;
        if (legal < maxDepth) maxDepth = static_cast<int>(legal);
    }

    int lo = 0;
    int probe = 1;
    while (probe <= maxDepth) {
        if (checkRange(layer, job, baseline, axis, earlier, probe)) {
            lo = probe;
            if (probe == maxDepth) return maxDepth;
            probe = std::max(probe + 1, probe > maxDepth / 2 ? maxDepth : probe * 2);
            continue;
        }

        int hi = probe;
        while (hi - lo > 1) {
            const int mid = lo + (hi - lo) / 2;
            if (checkRange(layer, job, baseline, axis, earlier, mid)) lo = mid;
            else hi = mid;
        }
        return lo;
    }
    return lo;
}

void ProbeController::scanRoute(
    PlayLayer* layer,
    ClickJob const& job,
    PathState const& baseline,
    ScanAxis axis,
    std::uint64_t& windowFrames,
    bool& hitLimit
) {
    windowFrames = 0;
    hitLimit = false;
    const int maxWindow = std::clamp(m_cfg.maxWindowFrames, 1, 240);

    const int earlyPass = scanDepth(layer, job, baseline, axis, true, maxWindow);
    int passed = 1 + earlyPass;
    if (passed > maxWindow) {
        hitLimit = true;
        windowFrames = static_cast<std::uint64_t>(maxWindow);
        return;
    }

    const int lateNeed = maxWindow - passed + 1;
    const int latePass = scanDepth(layer, job, baseline, axis, false, lateNeed);
    passed += latePass;
    if (passed > maxWindow) {
        hitLimit = true;
        windowFrames = static_cast<std::uint64_t>(maxWindow);
        return;
    }

    windowFrames = static_cast<std::uint64_t>(passed);
}

bool ProbeController::scanClick(PlayLayer* layer, ClickJob& job) {
    if (!layer || layer != m_level || m_platformer || m_resetting ||
        !job.canScan || !job.activated) {
        return false;
    }
    if (!m_probeLayer) setupProbe(layer);
    if (!m_probeLayer) return false;

    const bool canRelease = (releaseFrameOf(job) >= pressFrameOf(job)) &&
        (job.releaseOrder != kNoOrder);

    if (job.tryHolding && job.needsRelease) {
        if (!canRelease) return false;
        job.tryHolding = false;
    }

    if (!job.pressDone && job.tryHolding && !job.holdReady) {
        pickCubeHold(layer, job);
        if (!job.holdSafe) {
            job.needsRelease = true;
            if (!canRelease) return false;
            job.tryHolding = false;
        }
    }

    const bool wantPress = m_cfg.measureClicks;
    const bool wantRelease = m_cfg.measureReleases;

    const bool measurePress = !job.pressDone &&
        (job.tryHolding || !job.keepHold || canRelease);
    const bool measureRelease = canRelease && !job.releaseDone &&
        (job.pressDone || (measurePress && !job.tryHolding));
    if (!measurePress && !measureRelease) return false;

    auto baselineAxis = measurePress ? ScanAxis::Press : ScanAxis::Release;
    auto baseline = runPath(layer, job, baselineAxis, 0, false, -1);
    const bool validBaseline = sameJump(job, baseline) && baseline.resolveFrame >= 0;
    if (measurePress && job.tryHolding && (!validBaseline || baseline.died)) {
        ++job.baseFailures;
        job.holdSafe = false;
        job.needsRelease = true;
        if (!canRelease) return false;
        job.tryHolding = false;
        baselineAxis = ScanAxis::Press;
        baseline = runPath(layer, job, baselineAxis, 0, false, -1);
    }

    if (!sameJump(job, baseline) || baseline.resolveFrame < 0) {
        ++job.baseFailures;
        if (measurePress) job.pressDone = true;
        if (measureRelease) job.releaseDone = true;
        return false;
    }

    bool skipped = false;
    bool checkedNoJump = false;
    bool noJumpDied = false;
    const auto basePress = pressFrameOf(job);

    if (measurePress && wantPress && m_cfg.ignoreHarmlessClicks &&
        baseline.died && baseline.deathFrame > basePress) {
        const auto compareFrames = std::max<std::int64_t>(1, baseline.resolveFrame - basePress);
        const auto extra = std::max(kPadding, compareFrames);
        auto noJump = runPath(layer, job, ScanAxis::Press, 0, true, baseline.deathFrame + extra);
        checkedNoJump = true;
        noJumpDied = noJump.died;
        if (noJump.started && !job.tryHolding) {
            if (!noJump.died) {
                skipped = true;
            } else {
                const auto gap = std::llabs(noJump.deathFrame - baseline.deathFrame);
                const auto survivalGain = baseline.deathFrame - noJump.deathFrame;
                const bool closeTiming = gap < compareFrames * kDeathTol;
                skipped = (survivalGain <= 0) || closeTiming || (gap < kDeathGap);
            }
        }
    }

    std::uint64_t pressWindow = 0;
    std::uint64_t releaseWindow = 0;
    bool pressOver = false;
    bool releaseOver = false;
    const bool ignoreRelease = !wantRelease || !canRelease || noReleaseWin(job);

    if (measurePress && wantPress && !skipped) {
        scanRoute(layer, job, baseline, ScanAxis::Press, pressWindow, pressOver);
        if (pressOver && !checkedNoJump && (m_cfg.showOverLimit || m_cfg.showPrecisionHud)) {
            const auto compareFrames = std::max<std::int64_t>(1, baseline.resolveFrame - basePress);
            const auto extra = std::max(kPadding, compareFrames);
            auto noJump = runPath(layer, job, ScanAxis::Press, 0, true, baseline.resolveFrame + extra);
            noJumpDied = noJump.died;
            checkedNoJump = true;
        }
    }

    bool checkedNoRelease = false;
    bool noReleaseDied = false;
    if (measureRelease && !ignoreRelease) {
        scanRoute(layer, job, baseline, ScanAxis::Release, releaseWindow, releaseOver);
        if (releaseOver && (m_cfg.showOverLimit || m_cfg.showPrecisionHud)) {
            const auto baseRel = releaseFrameOf(job);
            const auto compareFrames = std::max<std::int64_t>(1, baseline.resolveFrame - baseRel);
            const auto extra = std::max(kPadding, compareFrames);
            auto noRel = runPath(layer, job, ScanAxis::Release, 0, true, baseline.resolveFrame + extra);
            checkedNoRelease = true;
            noReleaseDied = noRel.died;
        }
    }

    const int maxWin = std::clamp(m_cfg.maxWindowFrames, 1, 240);
    const bool pressIsRequired = (!m_cfg.ignoreHarmlessClicks || !checkedNoJump || noJumpDied);
    const bool showPressOver = pressOver && m_cfg.showOverLimit && pressIsRequired;
    const bool pressNandlIgnored = measurePress && wantPress && !skipped && pressOver && pressIsRequired;

    const bool showPress = measurePress && wantPress && !skipped &&
        ((!pressOver && pressWindow >= 1 && static_cast<int>(pressWindow) <= maxWin) || showPressOver);

    const bool releaseNumeric = measureRelease && !ignoreRelease &&
        !releaseOver && releaseWindow >= 1 && static_cast<int>(releaseWindow) <= maxWin;
    const bool releaseNecessaryOver = measureRelease && !ignoreRelease &&
        releaseOver && checkedNoRelease && noReleaseDied;
    const bool showReleaseOver = releaseNecessaryOver && m_cfg.showOverLimit;
    const bool showRelease = releaseNumeric || showReleaseOver;
    const bool releaseNandlIgnored = releaseNecessaryOver;

    if (measurePress) job.pressDone = true;
    if (measureRelease) job.releaseDone = true;

    if (showPress || showRelease || pressNandlIgnored || releaseNandlIgnored) {
        recordMeasurement(
            layer, job,
            pressWindow, releaseWindow,
            showPress, showPressOver, pressNandlIgnored,
            showRelease, showReleaseOver, releaseNandlIgnored
        );
    }
    return true;
}

void ProbeController::scanClicks(PlayLayer* layer) {
    if (m_probing || m_platformer || m_resetting || !m_cfg.enabled) return;

    const auto startTime = std::chrono::steady_clock::now();
    const auto nowFrame = rawFrame(layer);
    int jobsScanned = 0;

    for (auto& job : m_clicks) {
        if (!job.canScan || !job.activated) continue;

        if (!job.pressDone && job.releaseFrame < 0 &&
            nowFrame >= pressFrameOf(job) + kLongHoldMeasureFrames) {
            job.keepHold = false;
            job.tryHolding = false;
        }

        const bool canRelease = (releaseFrameOf(job) >= pressFrameOf(job)) &&
            (job.releaseOrder != kNoOrder);
        const bool cubeReady = job.tryHolding && !job.needsRelease;
        const bool pressPending = !job.pressDone && (cubeReady || !job.keepHold || canRelease);
        const bool pendingRelease = job.pressDone && !job.releaseDone && canRelease;
        if (!pressPending && !pendingRelease) continue;
        if (pendingRelease && noReleaseWin(job)) {
            job.releaseDone = true;
            continue;
        }

        scanClick(layer, job);
        ++jobsScanned;

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime
        ).count();
        if (jobsScanned >= 2 || elapsedMs >= 4) {
            break;
        }
    }
}

}
