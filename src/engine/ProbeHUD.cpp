#include "ProbeController.hpp"
#include "../audio/SoundPlayer.hpp"

#include <Geode/utils/string.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

using namespace geode::prelude;
using namespace cocos2d;

namespace na_frame_counter::engine {

namespace {

bool hasChild(CCNode* parent, CCNode* child) {
    if (!parent || !child) return false;
    auto* children = parent->getChildren();
    return children && children->containsObject(child);
}

ccColor3B toCCColor(core::RGBColor c) {
    return ccc3(c.r, c.g, c.b);
}

std::string formatLastBadge(SessionStats const& st, int maxWin) {
    if (st.lastWindowFrames == 0 && !st.lastOverLimit) {
        return "Last: --";
    }
    const char* kind = st.lastWasRelease ? "Rel" : "Click";
    if (st.lastOverLimit) {
        return fmt::format("Last: >{}F ({})", maxWin, kind);
    }
    return fmt::format("Last: {}F ({})", st.lastWindowFrames, kind);
}

std::string formatProb(double p) {
    if (p <= 0.0) return "0.00%";
    const double pct = p * 100.0;
    if (pct >= 1.0) return fmt::format("{:.2f}%", pct);
    if (pct >= 0.01) return fmt::format("{:.3f}%", pct);
    return fmt::format("{:.2e}%", pct);
}

} // namespace

void ProbeController::clearHUD() {
    auto* hudSentinel = m_hudSentinel;
    m_hudSentinel = nullptr;
    if (hudSentinel) {
        hudSentinel->disarm();
    }

    auto* hud = m_hud;
    m_hud = nullptr;
    m_lastBadgeLabel = nullptr;
    m_counters.clear();

    if (hud && hud->getParent()) {
        hud->removeFromParentAndCleanup(true);
    }
}

void ProbeController::resetHUD() {
    if (!m_level || !m_hud || m_hud->getParent() != m_level) return;

    for (auto& row : m_counters) {
        row.count = 0;
        if (hasChild(m_hud, row.valueLabel)) {
            row.valueLabel->stopAllActions();
            row.valueLabel->setString("0");
            row.valueLabel->setScale(0.48f);
            row.valueLabel->setColor(row.baseColor);
        } else {
            row.valueLabel = nullptr;
        }
    }
    if (hasChild(m_hud, m_lastBadgeLabel)) {
        m_lastBadgeLabel->setString("Last: --");
        m_lastBadgeLabel->setColor({200, 200, 200});
    } else {
        m_lastBadgeLabel = nullptr;
    }
}

void ProbeController::buildHUD() {
    clearHUD();
    if (!m_level || !m_cfg.enabled || !m_cfg.showCounters || m_platformer) return;

    auto* root = CCNode::create();
    if (!root) return;

    const auto winSize = CCDirector::sharedDirector()->getWinSize();
    const auto buckets = core::bandsForMax(m_cfg.maxWindowFrames, m_cfg.oneAtBottom);
    constexpr float kRowSpacing = 14.5f;
    const std::size_t totalRows = buckets.size() + (m_cfg.showLastBadge ? 1 : 0);

    bool alignRight = false;
    float anchorX = 8.f;
    float startY = winSize.height - 14.f;
    float dirY = -1.f;

    switch (m_cfg.hudCorner) {
        case core::HudCorner::TopRight:
            alignRight = true;
            anchorX = winSize.width - 10.f;
            startY = winSize.height - 14.f;
            dirY = -1.f;
            break;
        case core::HudCorner::BottomRight:
            alignRight = true;
            anchorX = winSize.width - 10.f;
            startY = 14.f + kRowSpacing * static_cast<float>(totalRows > 0 ? totalRows - 1 : 0);
            dirY = -1.f;
            break;
        case core::HudCorner::TopLeft:
        default:
            alignRight = false;
            anchorX = 8.f;
            startY = winSize.height - 14.f;
            dirY = -1.f;
            break;
    }

    root->setPosition({anchorX, startY});
    root->setID("tally-hud"_spr);
    m_hudSentinel = CleanupSentinel::create(SentinelKind::Hud, root);
    if (m_hudSentinel) {
        root->addChild(m_hudSentinel, -999999);
    }
    m_level->addChild(root, 1000002);
    m_hud = root;

    m_counters.reserve(buckets.size());

    for (std::size_t i = 0; i < buckets.size(); ++i) {
        auto const& bucket = buckets[i];
        const float y = dirY * kRowSpacing * static_cast<float>(i);
        const auto color = toCCColor(core::colorForWindow(
            static_cast<std::uint64_t>(std::max<std::int64_t>(1, bucket.minFrames)), m_cfg
        ));

        std::uint64_t existingCount = 0;
        for (auto w : m_measuredWindows) {
            if (static_cast<std::int64_t>(w) >= bucket.minFrames &&
                static_cast<std::int64_t>(w) <= bucket.maxFrames) {
                ++existingCount;
            }
        }

        if (auto* rangeLabel = CCLabelBMFont::create(
            fmt::format("{}:", bucket.label).c_str(), "bigFont.fnt"
        )) {
            rangeLabel->setScale(0.40f);
            rangeLabel->setColor(color);
            rangeLabel->setOpacity(245);
            if (alignRight) {
                rangeLabel->setAnchorPoint({1.f, 0.5f});
                rangeLabel->setPosition({-28.f, y});
            } else {
                rangeLabel->setAnchorPoint({0.f, 0.5f});
                rangeLabel->setPosition({0.f, y});
            }
            root->addChild(rangeLabel, 2);
        }

        if (auto* value = CCLabelBMFont::create(fmt::format("{}", existingCount).c_str(), "bigFont.fnt")) {
            value->setScale(0.48f);
            value->setColor(color);
            value->setOpacity(255);
            if (alignRight) {
                value->setAnchorPoint({1.f, 0.5f});
                value->setPosition({0.f, y});
            } else {
                value->setAnchorPoint({0.5f, 0.5f});
                value->setPosition({72.f, y});
            }
            root->addChild(value, 3);
            m_counters.push_back({bucket, existingCount, value, color});
        }
    }

    if (m_cfg.showLastBadge) {
        const float badgeY = dirY * kRowSpacing * static_cast<float>(buckets.size()) - 2.f;
        if (auto* badge = CCLabelBMFont::create(
            formatLastBadge(m_stats, m_cfg.maxWindowFrames).c_str(), "bigFont.fnt"
        )) {
            badge->setScale(0.32f);
            badge->setAnchorPoint({alignRight ? 1.f : 0.f, 0.5f});
            badge->setPosition({0.f, badgeY});
            if (m_stats.lastWindowFrames > 0) {
                badge->setColor(toCCColor(core::colorForWindow(m_stats.lastWindowFrames, m_cfg)));
            } else {
                badge->setColor({200, 200, 200});
            }
            badge->setOpacity(235);
            root->addChild(badge, 2);
            m_lastBadgeLabel = badge;
        }
    }
}

void ProbeController::pulseHUD(std::uint64_t windowFrames, bool isRelease, bool overLimit) {
    if (!m_level || !m_hud || m_hud->getParent() != m_level) return;

    if (hasChild(m_hud, m_lastBadgeLabel)) {
        m_lastBadgeLabel->setString(formatLastBadge(m_stats, m_cfg.maxWindowFrames).c_str());
        if (windowFrames > 0) {
            m_lastBadgeLabel->setColor(toCCColor(core::colorForWindow(windowFrames, m_cfg)));
        }
    } else {
        m_lastBadgeLabel = nullptr;
    }

    if (windowFrames == 0 || overLimit) return;

    for (auto& row : m_counters) {
        if (static_cast<std::int64_t>(windowFrames) < row.bucket.minFrames ||
            static_cast<std::int64_t>(windowFrames) > row.bucket.maxFrames) {
            continue;
        }

        ++row.count;
        if (!hasChild(m_hud, row.valueLabel)) {
            row.valueLabel = nullptr;
            return;
        }
        auto* value = row.valueLabel;

        value->stopAllActions();
        value->setString(fmt::format("{}", row.count).c_str());
        value->setColor({255, 255, 255});
        value->setScale(0.60f);
        value->runAction(CCSpawn::createWithTwoActions(
            CCScaleTo::create(0.20f, 0.48f),
            CCTintTo::create(0.30f, row.baseColor.r, row.baseColor.g, row.baseColor.b)
        ));

        auto* mod = Mod::get();
        if (!mod) return;
        std::error_code ec;
        auto glowPath = mod->getResourcesDir() / "glow.png";
        if (!std::filesystem::is_regular_file(glowPath, ec) || ec) {
            glowPath = mod->getResourcesDir() / "resources" / "glow.png";
        }
        auto glowStr = geode::utils::string::pathToString(glowPath);
        if (auto* glow = CCSprite::create(glowStr.c_str())) {
            constexpr float kGlowDiameter = 24.f;
            const auto sz = glow->getContentSize();
            if (sz.width > 0.f) glow->setScale(kGlowDiameter / sz.width);
            glow->setPosition(value->getPosition());
            if (value->getAnchorPoint().x > 0.75f) {
                glow->setPositionX(value->getPositionX() - 6.f);
            }
            glow->setColor(row.baseColor);
            glow->setOpacity(150);
            m_hud->addChild(glow, 1);
            glow->runAction(CCSequence::createWithTwoActions(
                CCFadeOut::create(0.45f),
                CCRemoveSelf::create()
            ));
        }
        return;
    }
}

void ProbeController::clearPrecisionHUD() {
    auto* precSentinel = m_precHudSentinel;
    m_precHudSentinel = nullptr;
    if (precSentinel) {
        precSentinel->disarm();
    }

    auto* precHud = m_precHud;
    m_precHud = nullptr;
    if (precHud && precHud->getParent()) {
        precHud->removeFromParentAndCleanup(true);
    }
}

void ProbeController::buildPrecisionHUD() {
    clearPrecisionHUD();
    if (!m_level || !m_cfg.enabled || !m_cfg.showPrecisionHud || m_platformer) return;

    auto* node = CCNode::create();
    if (!node) return;
    node->setPosition({8.f, 8.f});
    node->setID("precision-hud"_spr);
    m_precHudSentinel = CleanupSentinel::create(SentinelKind::PrecHud, node);
    if (m_precHudSentinel) {
        node->addChild(m_precHudSentinel, -999999);
    }
    m_level->addChild(node, 1000002);
    m_precHud = node;

    updatePrecisionHUD();
}

void ProbeController::updatePrecisionHUD() {
    if (!m_level || !m_precHud || m_precHud->getParent() != m_level) return;

    // Remove all label children while preserving our sentinel child
    if (auto* children = m_precHud->getChildren()) {
        std::vector<CCNode*> toRemove;
        toRemove.reserve(children->count());
        for (unsigned int i = 0; i < children->count(); ++i) {
            auto* child = static_cast<CCNode*>(children->objectAtIndex(i));
            if (child && child != m_precHudSentinel) {
                toRemove.push_back(child);
            }
        }
        for (auto* child : toRemove) {
            child->removeFromParentAndCleanup(true);
        }
    }

    if (!m_cfg.enabled || !m_cfg.showPrecisionHud || m_platformer) return;

    float y = 0.f;
    auto addLine = [&](std::string const& text, ccColor3B color) {
        if (auto* lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt")) {
            lbl->setAnchorPoint({0.f, 0.f});
            lbl->setPosition({0.f, y});
            lbl->setScale(0.32f);
            lbl->setColor(color);
            lbl->setOpacity(240);
            m_precHud->addChild(lbl);
            y += 12.f;
        }
    };

    if (!m_hasNandlSummary || m_nandlInputs.empty()) {
        addLine("NaNDL L*: 0.00 s/s", {220, 220, 220});
        return;
    }

    auto formatLine = [&](char const* name, math::NandlMetrics const& m) {
        if (m_cfg.showPassProb) {
            return fmt::format("{}: {:.2f} s/s  (P {})", name, m.lStar, formatProb(m.passProbability));
        }
        return fmt::format("{}: {:.2f} s/s", name, m.lStar);
    };

    switch (m_cfg.nandlDisplay) {
        case core::NandlDisplayMode::Base:
            addLine(formatLine("Base L*", m_nandlSummary.base), {255, 255, 255});
            break;
        case core::NandlDisplayMode::Nerve:
            addLine(formatLine("Nerve L*", m_nandlSummary.nerve), {255, 210, 120});
            break;
        case core::NandlDisplayMode::Fatigue:
            addLine(formatLine("Fatigue L*", m_nandlSummary.fatigue), {150, 230, 255});
            break;
        case core::NandlDisplayMode::CPS:
            addLine(formatLine("CPS L*", m_nandlSummary.cps), {160, 255, 160});
            break;
        case core::NandlDisplayMode::AllModels:
            addLine(formatLine("NFC L*", m_nandlSummary.nfc), {255, 130, 130});
            addLine(formatLine("CPS L*", m_nandlSummary.cps), {160, 255, 160});
            addLine(formatLine("Fatigue L*", m_nandlSummary.fatigue), {150, 230, 255});
            addLine(formatLine("Nerve L*", m_nandlSummary.nerve), {255, 210, 120});
            addLine(formatLine("Base L*", m_nandlSummary.base), {255, 255, 255});
            break;
        case core::NandlDisplayMode::NFC:
        default:
            addLine(formatLine("NFC L*", m_nandlSummary.nfc), {255, 255, 255});
            break;
    }
}

void ProbeController::recordNandlAndUpdate(
    std::int64_t actionFrame,
    std::uint64_t windowFrames,
    bool isRelease
) {
    if (windowFrames == 0) return;

    math::InputTiming timing;
    timing.frame = static_cast<double>(std::max<std::int64_t>(0, actionFrame));
    timing.windowFrames = static_cast<double>(windowFrames);
    timing.swiftCount = 1;
    timing.isRelease = isRelease;
    m_nandlInputs.push_back(timing);

    m_nandlSummary = math::NandlCalculator::solveAll(
        m_nandlInputs,
        m_cfg.nandlParams,
        m_hasNandlSummary ? &m_nandlSummary : nullptr
    );
    m_hasNandlSummary = true;

    m_stats.currentLStar = m_nandlSummary.nfc.lStar;
    if (m_stats.currentLStar > m_stats.bestLStar) {
        m_stats.bestLStar = m_stats.currentLStar;
    }

    updatePrecisionHUD();
}

CCPoint ProbeController::ringScreenPos(PlayLayer* layer, CCPoint objectPos) const {
    if (!layer || !layer->m_objectLayer) return objectPos;
    const auto world = layer->m_objectLayer->convertToWorldSpace(objectPos);
    return layer->convertToNodeSpace(world);
}

static void drawRingCircles(
    CCDrawNode* ring,
    ccColor3B color,
    float radius,
    float border,
    bool isRelease,
    bool distinguishRelease,
    float alpha
) {
    if (!ring) return;
    ring->clear();
    const ccColor4F clear{0.f, 0.f, 0.f, 0.f};
    const ccColor4F black{0.f, 0.f, 0.f, alpha};
    const ccColor4F colored{color.r / 255.f, color.g / 255.f, color.b / 255.f, alpha};

    ring->drawCircle({0.f, 0.f}, radius, clear, border + 2.2f, black, 96);
    ring->drawCircle({0.f, 0.f}, radius, clear, border, colored, 96);
    if (isRelease && distinguishRelease) {
        ring->drawCircle({0.f, 0.f}, std::max(3.f, radius * 0.62f), clear, 1.1f, colored, 64);
    }
}

CCNode* ProbeController::createRingMarkerNode(
    std::uint64_t windowFrames,
    bool isRelease,
    bool overLimit,
    CCDrawNode*& outRing,
    CCLabelBMFont*& outLabel
) {
    outRing = nullptr;
    outLabel = nullptr;

    auto* marker = CCNode::create();
    auto* ring = CCDrawNode::create();
    if (!marker || !ring) return nullptr;

    const auto rgb = core::colorForWindow(windowFrames, m_cfg);
    const auto ccCol = toCCColor(rgb);
    const float radius = std::clamp(m_cfg.circleRadius, 5.f, 26.f);
    const float border = std::clamp(m_cfg.circleThickness, 1.f, 5.f);

    drawRingCircles(ring, ccCol, radius, border, isRelease, m_cfg.distinguishRelease, 1.f);
    marker->addChild(ring);
    outRing = ring;

    const auto text = core::formatMarkerText(
        windowFrames,
        isRelease,
        overLimit,
        m_cfg.maxWindowFrames,
        m_cfg.distinguishRelease
    );

    if (auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt")) {
        const float offset = radius + border + 3.5f;
        float labelScale = 0.42f;
        switch (m_cfg.labelPlacement) {
            case core::LabelPlacement::Center:
                label->setAnchorPoint({0.5f, 0.5f});
                label->setPosition({0.f, 0.f});
                labelScale = std::clamp(radius / 26.f, 0.28f, 0.52f);
                break;
            case core::LabelPlacement::Top:
                label->setAnchorPoint({0.5f, 0.f});
                label->setPosition({0.f, offset});
                break;
            case core::LabelPlacement::Right:
                label->setAnchorPoint({0.f, 0.5f});
                label->setPosition({offset, 0.f});
                break;
            case core::LabelPlacement::Left:
            default:
                label->setAnchorPoint({1.f, 0.5f});
                label->setPosition({-offset, 0.f});
                break;
        }
        label->setScale(labelScale);
        label->setColor(ccCol);
        label->setOpacity(255);
        marker->addChild(label, 1);
        outLabel = label;
    }

    return marker;
}

void ProbeController::clearMarkers() {
    auto* markerSentinel = m_markerSentinel;
    m_markerSentinel = nullptr;
    if (markerSentinel) {
        markerSentinel->disarm();
    }

    auto* markerLayer = m_markerLayer;
    m_markerLayer = nullptr;
    m_markers.clear();

    if (markerLayer && markerLayer->getParent()) {
        markerLayer->removeFromParentAndCleanup(true);
    }
}

void ProbeController::moveMarkers(PlayLayer* layer, float dt) {
    if (!layer || layer != m_level || !layer->m_objectLayer) return;
    if (!m_markerLayer || m_markerLayer->getParent() != layer) {
        m_markers.clear();
        return;
    }

    float baseScale = std::clamp(m_cfg.markerScale, 0.5f, 2.0f);
    if (m_cfg.scaleWithZoom) {
        const float zoom = std::clamp(std::abs(layer->m_objectLayer->getScaleY()), 0.45f, 2.0f);
        baseScale *= zoom;
    }

    const float fadeTotal = m_cfg.markerFadeSeconds;

    for (auto it = m_markers.begin(); it != m_markers.end();) {
        if (!hasChild(m_markerLayer, it->node)) {
            it = m_markers.erase(it);
            continue;
        }

        if (fadeTotal > 0.05f && !m_paused && dt > 0.f) {
            it->age += dt;
            if (it->age >= fadeTotal) {
                it->node->removeFromParentAndCleanup(true);
                it = m_markers.erase(it);
                continue;
            }
            const float fadeStart = fadeTotal * 0.55f;
            if (it->age > fadeStart) {
                const float alpha = std::clamp(
                    1.f - (it->age - fadeStart) / std::max(0.01f, fadeTotal - fadeStart),
                    0.f, 1.f
                );
                if (hasChild(it->node, it->ring)) {
                    drawRingCircles(
                        it->ring, it->color, it->radius, it->border,
                        it->isRelease, it->distinguishRelease, alpha
                    );
                }
                if (hasChild(it->node, it->label)) {
                    it->label->setOpacity(static_cast<GLubyte>(alpha * 255.f));
                }
            }
        }

        it->node->setPosition(ringScreenPos(layer, it->worldPosition));
        it->node->setRotation(0.f);
        it->node->setScale(baseScale);
        ++it;
    }
}

void ProbeController::attachMarker(
    PlayLayer* layer,
    CCPoint const& position,
    std::uint64_t windowFrames,
    bool isRelease,
    bool overLimit,
    bool withSound
) {
    if (!layer || layer != m_level || !layer->m_objectLayer) return;

    ++m_stats.totalMeasured;
    if (windowFrames == 1 && !overLimit) {
        ++m_stats.framePerfects;
    }
    m_stats.lastWindowFrames = windowFrames;
    m_stats.lastWasRelease = isRelease;
    m_stats.lastOverLimit = overLimit;

    if (!overLimit && windowFrames >= 1) {
        m_measuredWindows.push_back(windowFrames);
    }

    pulseHUD(windowFrames, isRelease, overLimit);
    if (withSound && !overLimit && windowFrames >= 1) {
        audio::SoundPlayer::get().playForWindow(windowFrames, m_cfg, false);
    }

    if (!m_cfg.showRings || (windowFrames == 0 && !overLimit)) return;

    if (!m_markerLayer || m_markerLayer->getParent() != layer) {
        clearMarkers();
        auto* markerLayer = CCNode::create();
        if (!markerLayer) return;
        markerLayer->setID("markers-layer"_spr);
        m_markerSentinel = CleanupSentinel::create(SentinelKind::MarkerLayer, markerLayer);
        if (m_markerSentinel) {
            markerLayer->addChild(m_markerSentinel, -999999);
        }
        layer->addChild(markerLayer, 1000000);
        m_markerLayer = markerLayer;
    }

    CCDrawNode* ring = nullptr;
    CCLabelBMFont* label = nullptr;
    auto* marker = createRingMarkerNode(windowFrames, isRelease, overLimit, ring, label);
    if (!marker) return;

    m_markerLayer->addChild(marker, 1);
    RingMarker item;
    item.node = marker;
    item.ring = ring;
    item.label = label;
    item.color = toCCColor(core::colorForWindow(windowFrames, m_cfg));
    item.radius = std::clamp(m_cfg.circleRadius, 5.f, 26.f);
    item.border = std::clamp(m_cfg.circleThickness, 1.f, 5.f);
    item.isRelease = isRelease;
    item.distinguishRelease = m_cfg.distinguishRelease;
    item.worldPosition = position;
    item.age = 0.f;
    m_markers.push_back(item);

    while (m_markers.size() > kMaxVisibleMarkers) {
        if (hasChild(m_markerLayer, m_markers.front().node)) {
            m_markers.front().node->removeFromParentAndCleanup(true);
        }
        m_markers.pop_front();
    }
    moveMarkers(layer, 0.f);
}

void ProbeController::recordMeasurement(
    PlayLayer* layer,
    ClickJob const& job,
    std::uint64_t pressWindow,
    std::uint64_t releaseWindow,
    bool showPress,
    bool pressOver,
    bool showRelease,
    bool releaseOver
) {
    if (!layer || !layer->m_objectLayer) return;

    const bool mergeSound = showPress && showRelease && !pressOver && !releaseOver;
    std::uint64_t soundFrames = 0;
    if (mergeSound) {
        if (pressWindow > 0) soundFrames = pressWindow;
        if (releaseWindow > 0 && (soundFrames == 0 || releaseWindow < soundFrames)) {
            soundFrames = releaseWindow;
        }
    }

    if (showPress) {
        const auto pos = (job.activationPos.x != 0.f || job.activationPos.y != 0.f)
            ? job.activationPos
            : job.pressPos;
        attachMarker(layer, pos, pressWindow, false, pressOver, !mergeSound);
        recordNandlAndUpdate(pressFrameOf(job), pressWindow, false);
    }

    if (showRelease) {
        const auto relFrame = releaseFrameOf(job);
        const auto releasePos = (relFrame != job.releaseFrame) ? job.activationPos : job.releasePos;
        attachMarker(layer, releasePos, releaseWindow, true, releaseOver, !mergeSound);
        recordNandlAndUpdate(relFrame, releaseWindow, true);
    }

    if (mergeSound && soundFrames > 0) {
        audio::SoundPlayer::get().playForWindow(soundFrames, m_cfg, false);
    }
}

}
