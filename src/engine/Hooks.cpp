#include "ProbeController.hpp"
#include "../ui/ModMenuPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJGroundLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>
#include <Geode/utils/NodeIDs.hpp>
#include <Geode/utils/string.hpp>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;
using namespace na_frame_counter::engine;

namespace {

bool isPortalObject(GameObject* object) {
    if (!object) return false;
    if (object->m_objectID == 99 || object->m_objectID == 101) {
        return true;
    }
    switch (static_cast<int>(object->m_objectType)) {
        case 4:
        case 5:
        case 6:
        case 16:
        case 19:
        case 26:
        case 27:
        case 33:
        case 41:
        case 42:
            return true;
        default:
            return false;
    }
}

int probeMode(GameObject* object) {
    if (!object) return -1;
    switch (static_cast<int>(object->m_objectType)) {
        case 5:  return 1;
        case 6:  return 0;
        case 16: return 2;
        case 19: return 3;
        case 26: return 5;
        case 27: return 6;
        case 33: return 7;
        case 41: return 4;
        default: return -1;
    }
}

void setProbeMode(PlayerObject* probePlayer, int targetMode) {
    if (!probePlayer) return;
    if (probePlayer->m_isShip) probePlayer->toggleFlyMode(false, true);
    if (probePlayer->m_isBall) probePlayer->toggleRollMode(false, true);
    if (probePlayer->m_isBird) probePlayer->toggleBirdMode(false, true);
    if (probePlayer->m_isSwing) probePlayer->toggleSwingMode(false, true);
    if (probePlayer->m_isDart) probePlayer->toggleDartMode(false, true);
    if (probePlayer->m_isRobot) probePlayer->toggleRobotMode(false, true);
    if (probePlayer->m_isSpider) probePlayer->toggleSpiderMode(false, true);

    switch (targetMode) {
        case 1: probePlayer->toggleFlyMode(true, true); break;
        case 2: probePlayer->toggleRollMode(true, true); break;
        case 3: probePlayer->toggleBirdMode(true, true); break;
        case 4: probePlayer->toggleSwingMode(true, true); break;
        case 5: probePlayer->toggleDartMode(true, true); break;
        case 6: probePlayer->toggleRobotMode(true, true); break;
        case 7: probePlayer->toggleSpiderMode(true, true); break;
        default: break;
    }
    probePlayer->m_isDead = false;
}

bool safeObject(PlayerObject* player, GameObject* object) {
    if (!object) return false;
    if (isPortalObject(object) || (player && player->m_isDart && object->m_objectID == 1755)) {
        return true;
    }
    switch (object->m_objectType) {
        case GameObjectType::Solid:
        case GameObjectType::Hazard:
        case GameObjectType::Slope:
        case GameObjectType::AnimatedHazard:
        case GameObjectType::Breakable:
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::SpiderPad:
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::GreenRing:
        case GameObjectType::GravityRing:
        case GameObjectType::DropRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
        case GameObjectType::CollisionObject:
            return true;
        default:
            return false;
    }
}

bool resetActivation(GameObject* object) {
    if (!object) return false;
    if (isPortalObject(object)) return true;
    switch (object->m_objectType) {
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::SpiderPad:
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::GreenRing:
        case GameObjectType::GravityRing:
        case GameObjectType::DropRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
            return true;
        default:
            return false;
    }
}

CCSprite* createMenuButtonSprite() {
    auto* mod = Mod::get();
    if (mod) {
        std::error_code ec;
        auto path = mod->getResourcesDir() / "button.png";
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            path = mod->getResourcesDir() / "resources" / "button.png";
        }
        if (std::filesystem::is_regular_file(path, ec) && !ec) {
            auto pathStr = geode::utils::string::pathToString(path);
            if (auto* spr = CCSprite::create(pathStr.c_str())) {
                spr->setScale(33.f / std::max(1.f, spr->getContentSize().width));
                return spr;
            }
        }
    }
    auto* fallback = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
    if (fallback) fallback->setScale(0.85f);
    return fallback;
}

} // namespace

class $modify(NAPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        NodeIDs::provideFor(this);
        ProbeController::get().pauseStart(PlayLayer::get());

        auto* menu = typeinfo_cast<CCMenu*>(getChildByID("left-button-menu"));
        if (!menu) {
            menu = typeinfo_cast<CCMenu*>(getChildByID("right-button-menu"));
        }
        if (!menu || menu->getChildByID("minlol12.na-frame-counter/menu-button")) return;

        auto* spr = createMenuButtonSprite();
        if (!spr) return;

        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(NAPauseLayer::onOpenModMenu)
        );
        if (!btn) return;
        btn->setID("minlol12.na-frame-counter/menu-button");
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onOpenModMenu(CCObject*) {
        na_frame_counter::ui::ModMenuPopup::openOrFocus();
    }

    void onResume(CCObject* sender) {
        ProbeController::get().pauseEnd(PlayLayer::get());
        PauseLayer::onResume(sender);
    }

    void onQuit(CCObject* sender) {
        ProbeController::get().pauseEnd(PlayLayer::get());
        PauseLayer::onQuit(sender);
    }

    void onRestart(CCObject* sender) {
        ProbeController::get().pauseEnd(PlayLayer::get());
        PauseLayer::onRestart(sender);
    }

    void onRestartFull(CCObject* sender) {
        ProbeController::get().pauseEnd(PlayLayer::get());
        PauseLayer::onRestartFull(sender);
    }

    void goEdit() {
        ProbeController::get().pauseEnd(PlayLayer::get());
        PauseLayer::goEdit();
    }
};

class $modify(NAFMODAudioEngine, FMODAudioEngine) {
    int playEffectAdvanced(
        gd::string path,
        float speed,
        float unknown,
        float volume,
        float pitch,
        bool fastFourierTransform,
        bool reverb,
        int startMillis,
        int endMillis,
        int fadeIn,
        int fadeOut,
        bool loopEnabled,
        int effectID,
        bool overrideSound,
        bool noPreload,
        int channelID,
        int uniqueID,
        float minInterval,
        int sfxGroup
    ) {
        if (ProbeController::get().probing()) {
            return 0;
        }
        return FMODAudioEngine::playEffectAdvanced(
            path, speed, unknown, volume, pitch, fastFourierTransform, reverb,
            startMillis, endMillis, fadeIn, fadeOut, loopEnabled, effectID,
            overrideSound, noPreload, channelID, uniqueID, minInterval, sfxGroup
        );
    }
};

class $modify(NAGameObject, GameObject) {
    void playShineEffect() {
        if (ProbeController::get().probing()) return;
        GameObject::playShineEffect();
    }
};

class $modify(NAParticleSystem, CCParticleSystem) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("CCParticleSystem::addParticle", Priority::First);
        (void)self.setHookPriorityPre("CCParticleSystem::resetSystem", Priority::First);
        (void)self.setHookPriorityPre("CCParticleSystem::resumeSystem", Priority::First);
    }

    bool addParticle() {
        if (ProbeController::get().skipEffects()) return false;
        return CCParticleSystem::addParticle();
    }

    void resetSystem() {
        if (ProbeController::get().skipEffects()) return;
        CCParticleSystem::resetSystem();
    }

    void resumeSystem() {
        if (ProbeController::get().skipEffects()) return;
        CCParticleSystem::resumeSystem();
    }
};

class $modify(NAPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PlayerObject::playDeathEffect", Priority::First);
        (void)self.setHookPriorityPre("PlayerObject::playerDestroyed", Priority::First);
        (void)self.setHookPriorityBeforePre("PlayerObject::playDeathEffect", "absolllute.megahack");
        (void)self.setHookPriorityBeforePre("PlayerObject::playerDestroyed", "absolllute.megahack");
    }

    bool pushButton(PlayerButton button) {
        auto& controller = ProbeController::get();
        if (!controller.beforeButton(this, button, true)) {
            return false;
        }
        const bool accepted = PlayerObject::pushButton(button);
        controller.afterButton(this, button, true, accepted);
        return accepted;
    }

    bool releaseButton(PlayerButton button) {
        auto& controller = ProbeController::get();
        if (!controller.beforeButton(this, button, false)) {
            return false;
        }
        const bool accepted = PlayerObject::releaseButton(button);
        controller.afterButton(this, button, false, accepted);
        return accepted;
    }

    void disablePlayerControls() {
        auto& controller = ProbeController::get();
        if (controller.probing() && controller.isProbe(this)) {
            PlayerObject::disablePlayerControls();
            return;
        }
        controller.beforeControlToggle(this, false);
        PlayerObject::disablePlayerControls();
        controller.afterControlToggle(this, false);
    }

    void enablePlayerControls() {
        auto& controller = ProbeController::get();
        if (controller.probing() && controller.isProbe(this)) {
            PlayerObject::enablePlayerControls();
            return;
        }
        controller.beforeControlToggle(this, true);
        PlayerObject::enablePlayerControls();
        controller.afterControlToggle(this, true);
    }

    void ringJump(RingObject* object, bool skipCheck) {
        PlayerObject::ringJump(object, skipCheck);
        ProbeController::get().onJumpActive(this, object);
    }

    void incrementJumps() {
        ProbeController::get().onJumpActive(this, nullptr);
        PlayerObject::incrementJumps();
    }

    void activateStreak() {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::activateStreak();
    }

    void toggleGhostEffect(GhostType type) {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::toggleGhostEffect(type);
    }

    void playerDestroyed(bool p0) {
        auto& controller = ProbeController::get();
        if (controller.probing() && controller.isProbe(this)) {
            controller.clearProbeHit();
            m_isDead = false;
            return;
        }
        PlayerObject::playerDestroyed(p0);
    }

    void playDeathEffect() {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::playDeathEffect();
    }

    void playSpawnEffect() {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::playSpawnEffect();
    }

    void spawnCircle() {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::spawnCircle();
    }

    void spawnDualCircle() {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::spawnDualCircle();
    }

    void playBumpEffect(int objectType, GameObject* object) {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::playBumpEffect(objectType, object);
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (ProbeController::get().skipPlayerFx(this)) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }
};

class $modify(NARingObject, RingObject) {
    void spawnCircle() {
        if (ProbeController::get().disableRings()) return;
        RingObject::spawnCircle();
    }
};

class $modify(NAGroundLayer, GJGroundLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("GJGroundLayer::showGround", Priority::First);
        (void)self.setHookPriorityPre("GJGroundLayer::fadeInGround", Priority::First);
        (void)self.setHookPriorityPre("GJGroundLayer::fadeOutGround", Priority::First);
    }

    void showGround() {
        if (ProbeController::get().hideGround(this)) return;
        GJGroundLayer::showGround();
    }

    void fadeInGround(float duration) {
        if (ProbeController::get().hideGround(this)) return;
        GJGroundLayer::fadeInGround(duration);
    }

    void fadeOutGround(float duration) {
        if (ProbeController::get().hideGround(this)) return;
        GJGroundLayer::fadeOutGround(duration);
    }
};

class $modify(NABaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("GJBaseGameLayer::updateDebugDraw", Priority::First);
    }

    void update(float dt) override {
        auto& controller = ProbeController::get();
        controller.beforeUpdate(this);
        GJBaseGameLayer::update(dt);
        controller.afterUpdate(this);
    }

    void updateDebugDraw() override {
        if (ProbeController::get().probing()) return;
        GJBaseGameLayer::updateDebugDraw();
    }

    int checkCollisions(PlayerObject* player, float dt, bool ignoreDamage) {
        auto& controller = ProbeController::get();
        const int result = GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
        if (!controller.probing()) {
            auto* layer = PlayLayer::get();
            if (layer && static_cast<GJBaseGameLayer*>(layer) == this) {
                controller.saveHistory(layer, player);
            }
        }
        return result;
    }

    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int objectCount, float dt) {
        auto& controller = ProbeController::get();
        if (!controller.probing() || !controller.isProbe(player) || !objects || objectCount <= 0) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
            return;
        }

        static thread_local gd::vector<GameObject*> filtered;
        static thread_local std::vector<ActiveState> backups;
        filtered.clear();
        backups.clear();

        if (filtered.capacity() < static_cast<std::size_t>(objectCount)) {
            filtered.reserve(static_cast<std::size_t>(objectCount));
        }

        for (int i = 0; i < objectCount; ++i) {
            auto* object = objects->at(i);
            if (!safeObject(player, object)) continue;

            const int mode = probeMode(object);
            if (mode >= 0) {
                if (controller.usedPortal(object)) continue;
                if (player->getObjectRect().intersectsRect(object->getObjectRect())) {
                    setProbeMode(player, mode);
                    controller.addPortal(object);
                }
                continue;
            }

            const bool portal = isPortalObject(object);
            if (portal && controller.usedPortal(object)) continue;
            filtered.push_back(object);
            if (!resetActivation(object)) continue;

            auto* effect = geode::cast::typeinfo_cast<EffectGameObject*>(object);
            if (!effect) continue;
            backups.push_back({
                effect,
                effect->m_activated,
                effect->m_activatedByPlayer1,
                effect->m_activatedByPlayer2,
                portal,
            });
            effect->m_activated = false;
            effect->m_activatedByPlayer1 = false;
            effect->m_activatedByPlayer2 = false;
        }

        auto* const liveG1 = this->m_groundLayer;
        auto* const liveG2 = this->m_groundLayer2;
        const auto ground1State = GroundState::capture(liveG1);
        const auto ground2State = GroundState::capture(liveG2);
        const auto gameState = StateBackup::capture(this->m_gameState);
        const bool hideGroundState = this->m_hideGround;

        auto* const livePlayLayer = PlayLayer::get();
        auto* const liveObjects = (livePlayLayer &&
            static_cast<GJBaseGameLayer*>(livePlayLayer) == this)
                ? livePlayLayer->m_objectLayer
                : nullptr;
        const auto objectPos = liveObjects ? liveObjects->getPosition() : CCPoint{0.f, 0.f};
        const float objectScaleX = liveObjects ? liveObjects->getScaleX() : 1.f;
        const float objectScaleY = liveObjects ? liveObjects->getScaleY() : 1.f;
        const float objectAngle = liveObjects ? liveObjects->getRotation() : 0.f;
        const bool objectLayerVisible = liveObjects ? liveObjects->isVisible() : true;

        if (auto* g1 = controller.probeG1()) this->m_groundLayer = g1;
        if (auto* g2 = controller.probeG2()) this->m_groundLayer2 = g2;

        controller.clearProbeHit();
        GJBaseGameLayer::collisionCheckObjects(
            player,
            &filtered,
            static_cast<int>(filtered.size()),
            dt
        );
        controller.clearProbeHit();

        this->m_groundLayer = liveG1;
        this->m_groundLayer2 = liveG2;
        ground1State.restore();
        ground2State.restore();

        gameState.restore(this->m_gameState);
        this->m_hideGround = hideGroundState;
        if (liveObjects) {
            liveObjects->setPosition(objectPos);
            liveObjects->setScaleX(objectScaleX);
            liveObjects->setScaleY(objectScaleY);
            liveObjects->setRotation(objectAngle);
            liveObjects->setVisible(objectLayerVisible);
        }

        for (auto const& backup : backups) {
            const bool used = backup.object->m_activated ||
                backup.object->m_activatedByPlayer1 ||
                backup.object->m_activatedByPlayer2;
            if (backup.portal && used) controller.addPortal(backup.object);
            backup.object->m_activated = backup.activated;
            backup.object->m_activatedByPlayer1 = backup.p1;
            backup.object->m_activatedByPlayer2 = backup.p2;
        }
    }

    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (ProbeController::get().probing()) return;
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }

    void playExitDualEffect(PlayerObject* player) {
        auto& controller = ProbeController::get();
        if (controller.probing() && controller.isProbe(player)) return;
        GJBaseGameLayer::playExitDualEffect(player);
    }

    void lightningFlash(CCPoint from, CCPoint to, ccColor3B color, float lineWidth, float duration, int displacement, bool flash, float opacity) {
        if (ProbeController::get().probing()) return;
        GJBaseGameLayer::lightningFlash(from, to, color, lineWidth, duration, displacement, flash, opacity);
    }

    void shakeCamera(float duration, float strength, float interval) {
        if (ProbeController::get().skipFx(this)) return;
        GJBaseGameLayer::shakeCamera(duration, strength, interval);
    }

    CCParticleSystemQuad* spawnParticle(
        char const* plist,
        int zOrder,
        tCCPositionType positionType,
        CCPoint position
    ) {
        auto* particle = GJBaseGameLayer::spawnParticle(plist, zOrder, positionType, position);
        if (particle && ProbeController::get().skipFx(this)) {
            particle->setVisible(false);
            particle->stopSystem();
        }
        return particle;
    }
};

class $modify(NAPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First);
        (void)self.setHookPriorityPre("PlayLayer::toggleGroundVisibility", Priority::First);
        (void)self.setHookPriorityBeforePre("PlayLayer::destroyPlayer", "absolllute.megahack");
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }
        ProbeController::get().startLevel(this);
        return true;
    }

    void playGravityEffect(bool flip) override {
        if (ProbeController::get().probing()) return;
        PlayLayer::playGravityEffect(flip);
    }

    void toggleGroundVisibility(bool visible) override {
        if (ProbeController::get().probing()) return;
        PlayLayer::toggleGroundVisibility(visible);
    }

    void spawnCircle() {
        if (ProbeController::get().probing()) return;
        PlayLayer::spawnCircle();
    }

    void playEndAnimationToPos(CCPoint position) {
        if (ProbeController::get().probing()) return;
        PlayLayer::playEndAnimationToPos(position);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) override {
        auto& controller = ProbeController::get();
        if (controller.probing() && controller.isProbe(player)) {
            controller.probeDeath(player, object);
            return;
        }

        PlayLayer::destroyPlayer(player, object);
        if ((player == m_player1 || player == m_player2) &&
            (player->m_isDead || m_playerDied)) {
            controller.handleDeath(this);
        }
    }

    void resetLevel() override {
        auto& controller = ProbeController::get();
        if (controller.probing()) return;
        controller.beforeReset(this);
        PlayLayer::resetLevel();
        controller.onReset(this);
    }

    void resetLevelFromStart() {
        auto& controller = ProbeController::get();
        if (controller.probing()) return;
        controller.beforeReset(this);
        PlayLayer::resetLevelFromStart();
        controller.onReset(this);
    }

    void postUpdate(float dt) override {
        PlayLayer::postUpdate(dt);
        ProbeController::get().afterTick(this, dt);
    }

    void levelComplete() {
        auto& controller = ProbeController::get();
        if (controller.probing()) {
            controller.probeDone();
            return;
        }
        controller.completeLevel(this);
        PlayLayer::levelComplete();
    }

    void pauseGame(bool unfocused) {
        ProbeController::get().pauseStart(this);
        PlayLayer::pauseGame(unfocused);
    }

    void resume() {
        ProbeController::get().pauseEnd(this);
        PlayLayer::resume();
    }

    void onQuit() {
        ProbeController::get().onQuit(this);
        PlayLayer::onQuit();
    }

    void onExit() override {
        ProbeController::get().onExit(this);
        PlayLayer::onExit();
    }
};
