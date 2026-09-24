#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/Keyboard.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

using namespace geode::prelude;
using Clock = std::chrono::steady_clock;

namespace {
struct Settings {
    bool enabled = true;
    bool undeafenOnPause = true;
    bool deafenInPractice = false;
    bool deafenWithStartPos = false;
    int deafenAt = 70;
    int undeafenAt = 100;
    int deafenAfter = 0;
    int shortcut = KEY_Pause;

    static Settings load() {
        auto mod = Mod::get();
        Settings s;
        s.enabled = mod->getSavedValue<bool>("enabled", true);
        s.undeafenOnPause = mod->getSavedValue<bool>("undeafen-on-pause", true);
        s.deafenInPractice = mod->getSavedValue<bool>("deafen-in-practice", false);
        s.deafenWithStartPos = mod->getSavedValue<bool>("deafen-with-startpos", false);
        s.deafenAt = std::clamp(mod->getSavedValue<int>("deafen-at", 70), 0, 100);
        s.undeafenAt = std::clamp(mod->getSavedValue<int>("undeafen-at", 100), 0, 100);
        s.deafenAfter = std::clamp(mod->getSavedValue<int>("deafen-after", 0), 0, 100);
        s.shortcut = mod->getSavedValue<int>("shortcut", KEY_Pause);
        log::info("[AD] settings loaded: enabled={} pause={} practice={} startpos={} at={} until={} after={} key={}", s.enabled, s.undeafenOnPause, s.deafenInPractice, s.deafenWithStartPos, s.deafenAt, s.undeafenAt, s.deafenAfter, s.shortcut);
        return s;
    }

    void save() const {
        auto mod = Mod::get();
        mod->setSavedValue("enabled", enabled);
        mod->setSavedValue("undeafen-on-pause", undeafenOnPause);
        mod->setSavedValue("deafen-in-practice", deafenInPractice);
        mod->setSavedValue("deafen-with-startpos", deafenWithStartPos);
        mod->setSavedValue("deafen-at", deafenAt);
        mod->setSavedValue("undeafen-at", undeafenAt);
        mod->setSavedValue("deafen-after", deafenAfter);
        mod->setSavedValue("shortcut", shortcut);
        log::info("[AD] settings saved: enabled={} pause={} practice={} startpos={} at={} until={} after={} key={}", enabled, undeafenOnPause, deafenInPractice, deafenWithStartPos, deafenAt, undeafenAt, deafenAfter, shortcut);
    }
};

Settings& settings() {
    static Settings value = Settings::load();
    return value;
}

std::string keyName(int value) {
    auto name = CCDirector::sharedDirector()->getKeyboardDispatcher()->keyToString(
        static_cast<enumKeyCodes>(value));
    return name && *name ? name : "Unknown";
}

class Controller {
    bool m_deafened = false;
    bool m_pendingUndeafen = false;
    bool m_attemptActive = false;
    bool m_capturePending = false;
    bool m_deafenUsed = false;
    bool m_undeafenUsed = false;
    bool m_startPosAttempt = false;
    float m_startPercent = 0.f;
    StartPosObject* m_startPosObject = nullptr;
    int m_activeShortcut = KEY_Pause;
    Clock::time_point m_lastPress = Clock::time_point::min();
    Clock::time_point m_syntheticMenuKeyUntil = Clock::time_point::min();
    bool m_timerScheduled = false;
    uint64_t m_attemptGeneration = 0;
    Clock::time_point m_nextProgressLog{};

    bool ready() const {
        return m_lastPress == Clock::time_point::min() ||
            Clock::now() - m_lastPress >= std::chrono::milliseconds(300);
    }

    bool press(int shortcut) {
        if (shortcut <= KEY_None || shortcut > 255) return false;
        INPUT input[2] = {};
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = static_cast<WORD>(shortcut);
        input[1] = input[0];
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        auto sent = SendInput(2, input, sizeof(INPUT));
        log::info("[AD] SendInput key={} sent={}/2 error={}", shortcut, sent, sent == 2 ? 0UL : GetLastError());
        if (sent != 2) {
            if (sent == 1) SendInput(1, &input[1], sizeof(INPUT));
            log::warn("Shortcut injection failed for virtual key {}", shortcut);
            return false;
        }
        m_lastPress = Clock::now();
        if (shortcut == KEY_LeftMenu) {
            m_syntheticMenuKeyUntil = m_lastPress + std::chrono::milliseconds(120);
        }
        return true;
    }

    void scheduleUndeafen() {
        if (m_timerScheduled || !m_pendingUndeafen) return;
        m_timerScheduled = true;
        auto wait = m_lastPress + std::chrono::milliseconds(300) - Clock::now();
        auto ms = std::max<std::chrono::milliseconds>(
            std::chrono::duration_cast<std::chrono::milliseconds>(wait) +
                std::chrono::milliseconds(2),
            std::chrono::milliseconds(2));
        std::thread([ms] {
            std::this_thread::sleep_for(ms);
            geode::queueInMainThread([] {
                auto& self = Controller::get();
                self.m_timerScheduled = false;
                self.flushUndeafen();
            });
        }).detach();
    }

    void flushUndeafen() {
        if (!m_pendingUndeafen || !m_deafened) {
            m_pendingUndeafen = false;
            return;
        }
        if (!ready()) {
            scheduleUndeafen();
            return;
        }
        if (press(m_activeShortcut)) {
            m_deafened = false;
            m_pendingUndeafen = false;
        } else {
            scheduleUndeafen();
        }
    }

public:
    static Controller& get() {
        static Controller instance;
        return instance;
    }

    bool syntheticMenuKey() const { return Clock::now() < m_syntheticMenuKeyUntil; }
    uint64_t attemptGeneration() const { return m_attemptGeneration; }

    void undeafen() {
        if (!m_deafened) return;
        m_pendingUndeafen = true;
        flushUndeafen();
    }

    void enabledChanged(bool enabled) {
        log::info("[AD] automation enabled={} attemptActive={} deafened={}", enabled, m_attemptActive, m_deafened);
        if (!enabled) {
            undeafen();
        } else {
            // Re-evaluate the current attempt with the saved thresholds.
            m_deafenUsed = false;
            m_undeafenUsed = false;
        }
    }

    void endAttempt() {
        m_attemptActive = false;
        m_capturePending = false;
        m_deafenUsed = true;
        m_undeafenUsed = true;
        undeafen();
    }

    void beginAttempt(PlayLayer* layer) {
        endAttempt();
        ++m_attemptGeneration;
        m_deafenUsed = false;
        m_undeafenUsed = false;
        m_startPosObject = layer->m_startPosObject;
        m_startPosAttempt = m_startPosObject != nullptr ||
            (layer->m_isPracticeMode && layer->m_currentCheckpoint != nullptr);
        m_startPercent = std::clamp(layer->getCurrentPercent(), 0.f, 100.f);
        // A StartPos may be applied in the first update after reset.
        m_capturePending = m_startPosAttempt && m_startPercent <= 0.f &&
            ((m_startPosObject && m_startPosObject->getPositionX() > 0.f) ||
             layer->m_currentCheckpoint != nullptr);
        m_attemptActive = true;
        m_nextProgressLog = {};
        log::info("[AD] attempt start S={} practice={} startpos={} checkpoint={} capturePending={}", m_startPercent, layer->m_isPracticeMode, static_cast<void*>(m_startPosObject), static_cast<void*>(layer->m_currentCheckpoint), m_capturePending);
    }

    void pause() {
        if (!settings().enabled) return;
        if (settings().undeafenOnPause && m_deafened) {
            // This is still the same attempt. Reopen the deafen transition so
            // play can resume muted if the threshold is already behind us.
            m_deafenUsed = false;
            log::info("[AD] pause undeafen; attempt preserved S={} redeafen eligible=true", m_startPercent);
            undeafen();
        } else {
            log::info("[AD] pause; attempt preserved S={} deafened={}", m_startPercent, m_deafened);
        }
    }

    void tick(PlayLayer* layer) {
        if (Clock::now() >= m_nextProgressLog) {
            log::info("[AD] tick progress={} S={} active={} dead={} practice={} startpos={} deafen={} pendingOff={}", layer->getCurrentPercent(), m_startPercent, m_attemptActive, layer->m_player1 && layer->m_player1->m_isDead, layer->m_isPracticeMode, m_startPosAttempt, m_deafened, m_pendingUndeafen);
            m_nextProgressLog = Clock::now() + std::chrono::seconds(2);
        }
        flushUndeafen();
        if (!m_attemptActive) return;
        // Native Pause can leave PlayLayer in the scene and still run update
        // callbacks. Never re-deafen until gameplay has actually resumed.
        if (layer->m_isPaused) return;
        if (layer->m_player1 && layer->m_player1->m_isDead) {
            log::info("[AD] player dead in postUpdate; ending attempt");
            endAttempt();
            return;
        }
        if (layer->m_startPosObject != m_startPosObject) {
            endAttempt();
            return;
        }
        float progress = std::clamp(layer->getCurrentPercent(), 0.f, 100.f);
        if (m_capturePending) {
            m_startPercent = progress;
            m_capturePending = false;
        }
        if (m_pendingUndeafen) return;

        auto const& s = settings();
        if (!s.enabled) return;
        if ((layer->m_isPracticeMode && !s.deafenInPractice) ||
            (m_startPosAttempt && !s.deafenWithStartPos)) {
            undeafen();
            return;
        }

        float threshold = s.deafenAfter >= 1
            ? m_startPercent + static_cast<float>(s.deafenAfter)
            : static_cast<float>(s.deafenAt);
        if (threshold > 100.f || threshold >= s.undeafenAt) {
            undeafen();
            return;
        }

        if (m_deafened) {
            if (!m_undeafenUsed && progress >= s.undeafenAt) {
                m_undeafenUsed = true;
                undeafen();
            }
        } else if (!m_deafenUsed && progress >= threshold &&
                   progress < s.undeafenAt && ready()) {
            int key = s.shortcut;
            if (press(key)) {
                m_activeShortcut = key;
                m_deafened = true;
                m_deafenUsed = true;
            }
        }
    }
};

#include "EchoFramePopup.hpp"
} // namespace

class $modify(AutoDeafenPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        log::info("[AD] PlayLayer onModify registration");
    }
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        log::info("[AD] PlayLayer init enter");
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        Controller::get().beginAttempt(this);
        return true;
    }

    void resetLevel() {
        log::info("[AD] resetLevel enter");
        Controller::get().endAttempt();
        PlayLayer::resetLevel();
        Controller::get().beginAttempt(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto generation = Controller::get().attemptGeneration();
        PlayLayer::destroyPlayer(player, object);
        // Geometry Dash calls destroyPlayer for collisions that do not actually
        // kill the player (for example when protection is active). Only a real
        // death ends the attempt; a nested reset may already have begun another.
        if (player && player->m_isDead &&
            Controller::get().attemptGeneration() == generation) {
            log::info("[AD] actual player death; ending attempt");
            Controller::get().endAttempt();
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        Controller::get().tick(this);
    }

    void pauseGame(bool unfocused) {
        log::info("[AD] pauseGame unfocused={}", unfocused);
        PlayLayer::pauseGame(unfocused);
        Controller::get().pause();
    }

    void levelComplete() {
        log::info("[AD] levelComplete");
        PlayLayer::levelComplete();
        Controller::get().endAttempt();
    }

    void onExit() {
        // Geometry Dash also calls onExit when its pause scene is pushed.
        // onQuit, levelComplete, and resetLevel handle actual attempt endings.
        log::info("[AD] PlayLayer onExit (attempt preserved; paused={})", m_isPaused);
        PlayLayer::onExit();
    }

    void onQuit() {
        log::info("[AD] PlayLayer onQuit; ending attempt");
        Controller::get().endAttempt();
        PlayLayer::onQuit();
    }
};

$execute {
    log::info("[EchoFrame] registering KeyboardInputEvent");
    geode::queueInMainThread([] {
        auto hooks = Mod::get()->getHooks();
        log::info("[AD] startup hooks={}", hooks.size());
        for (auto hook : hooks) log::info("[AD] hook {} enabled={} address={:#x}", hook->getDisplayName(), hook->isEnabled(), hook->getAddress());
        settings();
    });
    KeyboardInputEvent().listen([](KeyboardInputData& data) {
        if (data.action != KeyboardInputData::Action::Press) {
            return ListenerResult::Propagate;
        }
        if (data.key == KEY_LeftMenu)
            log::info("[AD] Alt input key={} native={} playLayer={}", static_cast<int>(data.key), data.native.code, static_cast<void*>(PlayLayer::get()));
        if (auto open = EchoFramePopup::openPopup()) {
            if (open->isCapturing()) {
                open->captureKey(data.key);
                return ListenerResult::Stop;
            }
        }
        if (data.key != KEY_LeftMenu || !CCDirector::sharedDirector()->getRunningScene()) {
            return ListenerResult::Propagate;
        }
        if (Controller::get().syntheticMenuKey()) {
            return ListenerResult::Stop;
        }
        if (auto open = EchoFramePopup::openPopup()) {
            open->onClose(nullptr);
        } else {
            if (auto layer = PlayLayer::get(); layer && !layer->m_isPaused)
                return ListenerResult::Propagate;
            if (auto popup = EchoFramePopup::create()) popup->show();
        }
        return ListenerResult::Stop;
    }, Priority::VeryEarly).leak();
}
