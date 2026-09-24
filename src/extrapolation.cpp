#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    constexpr double kPhysicsStep = 1.0 / 240.0;
    constexpr float kPositionEpsilon = 0.0001f;
    constexpr float kMaxStepDistance = 32.f;

    struct MotionSample {
        CCNode* node = nullptr;
        CCPoint position = {0.f, 0.f};
        CCPoint step = {0.f, 0.f};

        void reset(CCNode* current) {
            node = current;
            position = current ? current->getPosition() : CCPoint{0.f, 0.f};
            step = CCPoint{0.f, 0.f};
        }

        void capture(CCNode* current, int steps, bool cameraZoomChanged = false) {
            if (node != current) {
                reset(current);
                return;
            }
            if (!node) return;

            auto next = node->getPosition();
            CCPoint movement = {next.x - position.x, next.y - position.y};
            if (steps > 0 || std::abs(movement.x) > kPositionEpsilon ||
                std::abs(movement.y) > kPositionEpsilon) {
                auto divisor = static_cast<float>(std::max(1, steps));
                CCPoint perStep = {movement.x / divisor, movement.y / divisor};
                // Portals, respawns and camera jumps are discontinuities, not velocity.
                if (!cameraZoomChanged &&
                    (std::abs(perStep.x) > kMaxStepDistance ||
                     std::abs(perStep.y) > kMaxStepDistance)) {
                    step = CCPoint{0.f, 0.f};
                } else {
                    step = perStep;
                }
                position = next;
            }
        }

        void capturePlayer(CCNode* current, int steps) {
            if (node != current) {
                reset(current);
                return;
            }
            if (!node) return;
            if (steps == 0) {
                // Zoom can make GD adjust the player's visual node between
                // physics steps. Track that native position, but do not treat
                // the adjustment as a full physics-step velocity.
                position = node->getPosition();
                return;
            }
            capture(current, steps);
        }
    };

    // A zoom changes the camera's scale and its position around the zoom
    // focus at the same time. They must be extrapolated as one transform.
    struct CameraSample {
        MotionSample motion;
        float scaleX = 1.f;
        float scaleY = 1.f;
        float stepScaleX = 0.f;
        float stepScaleY = 0.f;

        void reset(CCNode* node) {
            motion.reset(node);
            scaleX = node ? node->getScaleX() : 1.f;
            scaleY = node ? node->getScaleY() : 1.f;
            stepScaleX = stepScaleY = 0.f;
        }

        void capture(CCNode* node, int steps, bool zoomChanged) {
            if (motion.node != node) {
                reset(node);
                return;
            }
            if (!node) return;

            float nextX = node->getScaleX();
            float nextY = node->getScaleY();
            bool scaleChanged = std::abs(nextX - scaleX) > kPositionEpsilon ||
                                std::abs(nextY - scaleY) > kPositionEpsilon;
            if (steps == 0) {
                // A transform changed without a physics step was advanced by
                // the game's frame animation. It is already current for this
                // render, so the physics residual must not advance it again.
                motion.reset(node);
                scaleX = nextX;
                scaleY = nextY;
                stepScaleX = stepScaleY = 0.f;
                return;
            }
            // Camera translation may exceed the teleport threshold solely
            // because zoom changed. Keep it paired with the scale delta.
            motion.capture(node, steps, zoomChanged || scaleChanged);
            if (steps > 0 || scaleChanged) {
                float divisor = static_cast<float>(std::max(1, steps));
                stepScaleX = (nextX - scaleX) / divisor;
                stepScaleY = (nextY - scaleY) / divisor;
                scaleX = nextX;
                scaleY = nextY;
            }
        }
    };

    // The screen-space origin of the object layer includes *both* camera
    // nodes. Interpolating their local positions and scales independently
    // introduces a scale*translation cross term, which can be very large in
    // a long level during a zoom animation.
    struct CameraOriginSample {
        CCNode* world = nullptr;
        CCPoint origin = {0.f, 0.f};
        CCPoint step = {0.f, 0.f};

        void reset(CCNode* node) {
            world = node;
            origin = node ? node->convertToWorldSpace({0.f, 0.f}) : CCPoint{0.f, 0.f};
            step = CCPoint{0.f, 0.f};
        }

        void capture(CCNode* node, int steps, bool zoomChanged) {
            if (world != node) {
                reset(node);
                return;
            }
            if (!node) return;
            auto next = node->convertToWorldSpace({0.f, 0.f});
            if (steps == 0) {
                origin = next;
                step = CCPoint{0.f, 0.f};
                return;
            }
            CCPoint perStep = {
                (next.x - origin.x) / steps,
                (next.y - origin.y) / steps
            };
            if (!zoomChanged &&
                (std::abs(perStep.x) > kMaxStepDistance ||
                 std::abs(perStep.y) > kMaxStepDistance)) {
                step = CCPoint{0.f, 0.f};
            } else {
                step = perStep;
            }
            origin = next;
        }
    };

    struct CameraOriginShift {
        CCNode* world = nullptr;
        CCPoint position = {0.f, 0.f};

        void apply(CameraOriginSample const& sample, float fraction) {
            if (!sample.world || fraction == 0.f) return;
            auto parent = sample.world->getParent();
            if (!parent) return;
            world = sample.world;
            position = world->getPosition();
            auto actual = world->convertToWorldSpace({0.f, 0.f});
            CCPoint target = {
                sample.origin.x + sample.step.x * fraction,
                sample.origin.y + sample.step.y * fraction
            };
            auto actualInParent = parent->convertToNodeSpace(actual);
            auto targetInParent = parent->convertToNodeSpace(target);
            world->CCNode::setPosition({
                position.x + targetInParent.x - actualInParent.x,
                position.y + targetInParent.y - actualInParent.y
            });
        }

        void restore() {
            if (world) world->CCNode::setPosition(position);
        }
    };

    struct CameraShift {
        CCNode* node = nullptr;
        CCPoint position = {0.f, 0.f};
        float scaleX = 1.f;
        float scaleY = 1.f;

        bool apply(CameraSample const& sample, float fraction) {
            if (!sample.motion.node || fraction == 0.f) return false;
            node = sample.motion.node;
            position = node->getPosition();
            scaleX = node->getScaleX();
            scaleY = node->getScaleY();

            // If GD advanced this camera node after the physics update,
            // its current render transform is already newer than our sample.
            // Adding the old delta again would create a visible jump.
            if (std::abs(position.x - sample.motion.position.x) > kPositionEpsilon ||
                std::abs(position.y - sample.motion.position.y) > kPositionEpsilon ||
                std::abs(scaleX - sample.scaleX) > kPositionEpsilon ||
                std::abs(scaleY - sample.scaleY) > kPositionEpsilon) {
                node = nullptr;
                return false;
            }

            float visualX = scaleX + sample.stepScaleX * fraction;
            float visualY = scaleY + sample.stepScaleY * fraction;
            // A one-step instant zoom is a discontinuity. Keep that native
            // camera transform for this frame, while player motion continues.
            if (!std::isfinite(visualX) || !std::isfinite(visualY) ||
                visualX * scaleX <= 0.f || visualY * scaleY <= 0.f) {
                node = nullptr;
                return false;
            }

            CCPoint offset = {
                sample.motion.step.x * fraction,
                sample.motion.step.y * fraction
            };
            bool moved = offset.x != 0.f || offset.y != 0.f;
            bool scaled = sample.stepScaleX != 0.f || sample.stepScaleY != 0.f;
            if (!moved && !scaled) {
                node = nullptr;
                return false;
            }
            if (moved) node->CCNode::setPosition({position.x + offset.x, position.y + offset.y});
            if (scaled) {
                node->CCNode::setScaleX(visualX);
                node->CCNode::setScaleY(visualY);
            }
            return moved || scaled;
        }

        void restore() {
            if (!node) return;
            node->CCNode::setScaleX(scaleX);
            node->CCNode::setScaleY(scaleY);
            node->CCNode::setPosition(position);
        }
    };

    struct RenderShift {
        CCNode* node = nullptr;
        CCPoint position = {0.f, 0.f};

        bool apply(MotionSample const& sample, float fraction) {
            if (!sample.node || fraction == 0.f ||
                (sample.step.x == 0.f && sample.step.y == 0.f)) return false;
            node = sample.node;
            position = node->getPosition();
            // Call CCNode directly: PlayerObject::setPosition updates gameplay state.
            node->CCNode::setPosition({
                position.x + sample.step.x * fraction,
                position.y + sample.step.y * fraction
            });
            return true;
        }

        void restore() {
            if (node) node->CCNode::setPosition(position);
        }
    };

    CCPoint playerScreen(PlayerObject* player) {
        return player ? player->convertToWorldSpace(player->getAnchorPointInPoints())
                      : CCPoint{0.f, 0.f};
    }

    // During a zoom the camera follows the player in the same native update.
    // The player's local delta alone is therefore not its visible movement.
    struct ScreenMotionSample {
        PlayerObject* node = nullptr;
        CCPoint position = {0.f, 0.f};
        CCPoint step = {0.f, 0.f};

        void reset(PlayerObject* current) {
            node = current;
            position = playerScreen(current);
            step = CCPoint{0.f, 0.f};
        }

        void capture(PlayerObject* current, int steps) {
            if (node != current) {
                reset(current);
                return;
            }
            if (!node) return;
            auto next = playerScreen(node);
            CCPoint movement = {next.x - position.x, next.y - position.y};
            if (steps > 0 || std::abs(movement.x) > kPositionEpsilon ||
                std::abs(movement.y) > kPositionEpsilon) {
                auto divisor = static_cast<float>(std::max(1, steps));
                CCPoint perStep = {movement.x / divisor, movement.y / divisor};
                if (std::abs(perStep.x) > kMaxStepDistance ||
                    std::abs(perStep.y) > kMaxStepDistance) {
                    step = CCPoint{0.f, 0.f};
                } else {
                    step = perStep;
                }
                position = next;
            }
        }
    };

    struct ScreenRenderShift {
        PlayerObject* node = nullptr;
        CCPoint position = {0.f, 0.f};
        CCPoint targetScreen = {0.f, 0.f};

        bool prepare(ScreenMotionSample const& sample, float fraction) {
            if (!sample.node || !sample.node->getParent()) return false;
            auto current = playerScreen(sample.node);
            if (std::abs(current.x - sample.position.x) > 0.05f ||
                std::abs(current.y - sample.position.y) > 0.05f) return false;
            node = sample.node;
            position = node->getPosition();
            targetScreen = CCPoint{
                sample.position.x + sample.step.x * fraction,
                sample.position.y + sample.step.y * fraction
            };
            return true;
        }

        bool apply() {
            if (!node) return false;
            auto current = playerScreen(node);
            CCPoint delta = {
                targetScreen.x - current.x,
                targetScreen.y - current.y
            };
            auto transform = node->getParent()->nodeToWorldTransform();
            double determinant = static_cast<double>(transform.a) * transform.d -
                static_cast<double>(transform.b) * transform.c;
            if (!std::isfinite(determinant) || std::abs(determinant) < 0.00000001) {
                node = nullptr;
                return false;
            }
            CCPoint local = {
                static_cast<float>((transform.d * delta.x - transform.c * delta.y) / determinant),
                static_cast<float>((-transform.b * delta.x + transform.a * delta.y) / determinant)
            };
            node->CCNode::setPosition({position.x + local.x, position.y + local.y});
            return local.x != 0.f || local.y != 0.f;
        }

        void restore() {
            if (node) node->CCNode::setPosition(position);
        }
    };

    struct ExtrapolationState {
        GJBaseGameLayer* layer = nullptr;
        MotionSample normalWorld;
        CameraSample world;
        CameraSample cameraParent;
        CameraOriginSample cameraOrigin;
        MotionSample player1;
        MotionSample player2;
        ScreenMotionSample player1Screen;
        ScreenMotionSample player2Screen;
        int step = 0;
        float cameraZoom = 1.f;
        bool zoomActive = false;
        bool cameraScaleChangedThisAttempt = false;
        bool enabled = false;
        uint64_t updates = 0;
        uint64_t visits = 0;
        uint64_t shifted = 0;

        void reset(GJBaseGameLayer* current) {
            layer = current;
            normalWorld.reset(current ? current->m_objectLayer : nullptr);
            world.reset(current ? current->m_objectLayer : nullptr);
            cameraParent.reset(current && current->m_objectLayer
                ? current->m_objectLayer->getParent() : nullptr);
            cameraOrigin.reset(current ? current->m_objectLayer : nullptr);
            player1.reset(current ? current->m_player1 : nullptr);
            player2.reset(current ? current->m_player2 : nullptr);
            player1Screen.reset(current ? current->m_player1 : nullptr);
            player2Screen.reset(current ? current->m_player2 : nullptr);
            step = current ? current->m_currentStep : 0;
            cameraZoom = current ? current->m_gameState.m_cameraZoom : 1.f;
            zoomActive = false;
            cameraScaleChangedThisAttempt = false;
            updates = visits = shifted = 0;
        }
    };

    ExtrapolationState s_state;

    bool inGameplay(GJBaseGameLayer* layer) {
        auto play = PlayLayer::get();
        return play && layer == play && play->m_started && !play->m_isPaused &&
            !play->m_playerDied && !play->m_levelEndAnimationStarted;
    }

    float renderFraction(GJBaseGameLayer* layer) {
        // GD 2.2081 rounds the accumulated dt to the nearest 240 Hz step.
        // m_extraDelta is signed, so frames just after an early physics step
        // need a small backward visual correction.
        double timeWarp = std::min(1.0, static_cast<double>(layer->m_gameState.m_timeWarp));
        double duration = timeWarp * kPhysicsStep;
        if (duration <= 0.00001 || !std::isfinite(layer->m_extraDelta)) return 0.f;
        return static_cast<float>(std::clamp(layer->m_extraDelta / duration, -0.5, 0.5));
    }
}

class $modify(FrameExtrapolationGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);

        bool enabled = Mod::get()->getSettingValue<bool>("enabled");
        if (enabled != s_state.enabled) {
            s_state.enabled = enabled;
            s_state.reset(nullptr);
            log::info("Frame Extrapolation {}: gameplay hooks {}",
                enabled ? "enabled" : "disabled", enabled ? "armed" : "idle");
        }
        if (!enabled || !inGameplay(this)) {
            if (s_state.layer == this) s_state.reset(nullptr);
            return;
        }
        if (s_state.layer != this || m_currentStep < s_state.step) {
            s_state.reset(this);
            log::info("Frame Extrapolation: PlayLayer update hook active; physics step {}", m_currentStep);
        }

        int elapsedSteps = m_currentStep - s_state.step;
        bool zoomChanged = std::abs(m_gameState.m_cameraZoom - s_state.cameraZoom) >
            kPositionEpsilon;
        auto parent = m_objectLayer ? m_objectLayer->getParent() : nullptr;
        bool cameraScaleChanged =
            (parent == s_state.cameraParent.motion.node && parent &&
                (std::abs(parent->getScaleX() - s_state.cameraParent.scaleX) > kPositionEpsilon ||
                 std::abs(parent->getScaleY() - s_state.cameraParent.scaleY) > kPositionEpsilon)) ||
            (m_objectLayer == s_state.world.motion.node && m_objectLayer &&
                (std::abs(m_objectLayer->getScaleX() - s_state.world.scaleX) > kPositionEpsilon ||
                 std::abs(m_objectLayer->getScaleY() - s_state.world.scaleY) > kPositionEpsilon));
        bool zoomActive = zoomChanged || cameraScaleChanged ||
            (elapsedSteps == 0 && s_state.zoomActive);
        if (zoomChanged || cameraScaleChanged) {
            s_state.cameraScaleChangedThisAttempt = true;
        }
        if (zoomActive || s_state.zoomActive != zoomActive) {
            // Keep the original world-motion sampler out of zoom transitions.
            // Outside zoom it must retain its velocity through zero-step
            // updates exactly as the working Frame Extrapolation did.
            s_state.normalWorld.reset(m_objectLayer);
        } else {
            s_state.normalWorld.capture(m_objectLayer, elapsedSteps);
        }
        s_state.zoomActive = zoomActive;
        s_state.cameraParent.capture(m_objectLayer ? m_objectLayer->getParent() : nullptr,
            elapsedSteps, zoomChanged);
        s_state.world.capture(m_objectLayer, elapsedSteps, zoomChanged);
        s_state.cameraOrigin.capture(m_objectLayer, elapsedSteps,
            zoomChanged || cameraScaleChanged);
        if (s_state.cameraScaleChangedThisAttempt && zoomActive) {
            s_state.player1.capturePlayer(m_player1, elapsedSteps);
            s_state.player2.capturePlayer(m_player2, elapsedSteps);
        } else {
            // Once the zoom render path ends, player and world must both
            // sample native zero-step movement with the ordinary algorithm.
            s_state.player1.capture(m_player1, elapsedSteps);
            s_state.player2.capture(m_player2, elapsedSteps);
        }
        s_state.player1Screen.capture(m_player1, elapsedSteps);
        s_state.player2Screen.capture(m_player2, elapsedSteps);
        s_state.step = m_currentStep;
        s_state.cameraZoom = m_gameState.m_cameraZoom;
        ++s_state.updates;
    }

    void visit() {
        if (!s_state.enabled || s_state.layer != this || !inGameplay(this)) {
            GJBaseGameLayer::visit();
            return;
        }

        float fraction = renderFraction(this);
        CameraShift cameraParent, world;
        CameraOriginShift cameraOrigin;
        RenderShift normalWorld, player1, player2;
        ScreenRenderShift screenPlayer1, screenPlayer2;
        bool screenPlayer1Ready = s_state.zoomActive &&
            screenPlayer1.prepare(s_state.player1Screen, fraction);
        bool screenPlayer2Ready = s_state.zoomActive &&
            screenPlayer2.prepare(s_state.player2Screen, fraction);
        bool moved = false;
        auto cameraNative = s_state.zoomActive && m_objectLayer &&
            m_objectLayer == s_state.world.motion.node &&
            m_objectLayer->getParent() == s_state.cameraParent.motion.node &&
            std::abs(m_objectLayer->getPositionX() - s_state.world.motion.position.x) <= kPositionEpsilon &&
            std::abs(m_objectLayer->getPositionY() - s_state.world.motion.position.y) <= kPositionEpsilon &&
            std::abs(m_objectLayer->getScaleX() - s_state.world.scaleX) <= kPositionEpsilon &&
            std::abs(m_objectLayer->getScaleY() - s_state.world.scaleY) <= kPositionEpsilon;
        if (cameraNative && s_state.cameraParent.motion.node) {
            auto parent = s_state.cameraParent.motion.node;
            cameraNative =
                std::abs(parent->getPositionX() - s_state.cameraParent.motion.position.x) <= kPositionEpsilon &&
                std::abs(parent->getPositionY() - s_state.cameraParent.motion.position.y) <= kPositionEpsilon &&
                std::abs(parent->getScaleX() - s_state.cameraParent.scaleX) <= kPositionEpsilon &&
                std::abs(parent->getScaleY() - s_state.cameraParent.scaleY) <= kPositionEpsilon;
        }
        if (cameraNative) {
            auto origin = m_objectLayer->convertToWorldSpace({0.f, 0.f});
            cameraNative =
                std::abs(origin.x - s_state.cameraOrigin.origin.x) <= 0.01f &&
                std::abs(origin.y - s_state.cameraOrigin.origin.y) <= 0.01f;
        }
        if (cameraNative) {
            moved |= cameraParent.apply(s_state.cameraParent, fraction);
            moved |= world.apply(s_state.world, fraction);
            // Correct the combined camera origin after scaling the nodes.
            // This preserves the actual screen transform, including its zoom
            // focus, instead of accumulating a large cross term.
            cameraOrigin.apply(s_state.cameraOrigin, fraction);
        } else if (!s_state.zoomActive) {
            moved |= normalWorld.apply(s_state.normalWorld, fraction);
        }
        if (screenPlayer1Ready) {
            bool shifted = screenPlayer1.apply();
            moved |= screenPlayer1.node ? shifted : player1.apply(s_state.player1, fraction);
        } else {
            moved |= player1.apply(s_state.player1, fraction);
        }
        if (screenPlayer2Ready) {
            bool shifted = screenPlayer2.apply();
            moved |= screenPlayer2.node ? shifted : player2.apply(s_state.player2, fraction);
        } else {
            // In single-player mode player 2 may still exist.
            moved |= player2.apply(s_state.player2, fraction);
        }
        GJBaseGameLayer::visit();

        player2.restore();
        player1.restore();
        screenPlayer2.restore();
        screenPlayer1.restore();
        cameraOrigin.restore();
        world.restore();
        cameraParent.restore();
        normalWorld.restore();
        ++s_state.visits;
        if (moved) ++s_state.shifted;
        if (s_state.visits == 1 || s_state.visits % 1200 == 0) {
            log::info("Frame Extrapolation render hook: updates={}, visits={}, shifted={}, step={}, residual={} ms, fraction={}",
                s_state.updates, s_state.visits, s_state.shifted, m_currentStep,
                m_extraDelta * 1000.0, fraction);
        }
    }
};
