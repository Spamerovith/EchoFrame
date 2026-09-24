class EchoFramePopup final : public geode::Popup {
    CCMenu* m_controls = nullptr;
    CCNode* m_autoPage = nullptr;
    CCNode* m_framePage = nullptr;
    TextInput* m_percent[3] = {};
    CCLabelBMFont* m_percentSuffix[3] = {};
    CCLabelBMFont* m_deafenAtLabel = nullptr;
    CCLabelBMFont* m_shortcutLabel = nullptr;
    CCLabelBMFont* m_hint = nullptr;
    CCLabelBMFont* m_frameState = nullptr;
    bool m_capturing = false;
    PlayLayer* m_pauseOwner = nullptr;
    static EchoFramePopup* s_open;

    void releaseOwnedPause() {
        auto layer = PlayLayer::get();
        if (m_pauseOwner && layer == m_pauseOwner && !layer->m_isPaused) {
            layer->resumeSchedulerAndActions();
            log::info("[AD] popup restored its own gameplay suspension");
        }
        m_pauseOwner = nullptr;
    }

    CCLabelBMFont* label(CCNode* parent, char const* text, float x, float y,
                         float scale = .42f, char const* font = "goldFont.fnt") {
        auto result = CCLabelBMFont::create(text, font);
        result->setScale(scale);
        result->setAnchorPoint({0.f, .5f});
        result->setPosition({x, y});
        parent->addChild(result);
        return result;
    }

    CCMenuItemSpriteExtra* button(char const* caption, float x, float y,
                                  SEL_MenuHandler callback) {
        auto item = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(caption, .52f), this, callback);
        item->setPosition({x, y});
        m_controls->addChild(item);
        return item;
    }

    void showPage(bool autoPage) {
        m_autoPage->setVisible(autoPage);
        m_framePage->setVisible(!autoPage);
        m_controls->setVisible(autoPage);
        if (!autoPage) m_capturing = false;
        refresh();
    }

    void refresh() {
        auto const& s = settings();
        bool inactive = s.deafenAfter >= 1;
        m_deafenAtLabel->setColor(inactive ? ccc3(125, 125, 125) : ccc3(255, 255, 255));
        m_percent[0]->setEnabled(!inactive);
        m_percent[0]->getBGSprite()->setVisible(true);
        m_percentSuffix[0]->setColor(inactive ? ccc3(125, 125, 125) : ccc3(255, 255, 255));
        m_shortcutLabel->setString(
            (m_capturing ? "Press a key..." : keyName(s.shortcut)).c_str());
        m_hint->setString(inactive
            ? "Deafen at is saved but inactive while Deafen after > 0%."
            : "Assign Shortcut to the same key in Discord or another app.");
        bool frameEnabled = Mod::get()->getSettingValue<bool>("enabled");
        m_frameState->setString(frameEnabled ? "ON" : "OFF");
        m_frameState->setColor(frameEnabled ? ccc3(108, 226, 158) : ccc3(172, 182, 205));
    }

    void restoreEmptyAfterBlur(float) {
        auto input = m_percent[2];
        if (input && !input->getInputNode()->m_selected && input->getString().empty())
            input->setString("0");
    }

    bool initPopup() {
        log::info("[EchoFrame] popup init");
        auto win = CCDirector::sharedDirector()->getWinSize();
        float width = std::min(390.f, win.width - 24.f);
        float height = std::min(330.f, win.height - 16.f);
        if (!Popup::init(width, height)) return false;
        setTitle("EchoFrame", "goldFont.fnt", .8f);

        auto tabs = CCMenu::create();
        tabs->setPosition({0.f, 0.f});
        m_mainLayer->addChild(tabs);
        auto autoTab = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("AutoDeafen", .47f), this,
            menu_selector(EchoFramePopup::onAutoTab));
        auto frameTab = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Frame Extrapolation", .47f), this,
            menu_selector(EchoFramePopup::onFrameTab));
        autoTab->setPosition({width * .29f, height - 48.f});
        frameTab->setPosition({width * .72f, height - 48.f});
        tabs->addChild(autoTab);
        tabs->addChild(frameTab);

        m_autoPage = CCNode::create();
        m_framePage = CCNode::create();
        m_mainLayer->addChild(m_autoPage);
        m_mainLayer->addChild(m_framePage);
        m_controls = CCMenu::create();
        m_controls->setPosition({0.f, 0.f});
        m_mainLayer->addChild(m_controls);

        float top = height - 100.f;
        float step = (height - 138.f) / 7.f;
        float left = 25.f;
        float right = width - 44.f;
        label(m_autoPage, "AutoDeafen enabled", left, height - 79.f, .38f);
        auto enabledToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(EchoFramePopup::onEnabled), .57f);
        enabledToggle->setPosition({right, height - 79.f});
        enabledToggle->toggle(settings().enabled);
        m_controls->addChild(enabledToggle);
        char const* toggles[] = {
            "Undeafen on Pause", "Deafen in Practice", "Deafen With StartPos"};
        bool states[] = {settings().undeafenOnPause,
                         settings().deafenInPractice, settings().deafenWithStartPos};
        for (int i = 0; i < 3; ++i) {
            float y = top - i * step;
            label(m_autoPage, toggles[i], left, y);
            auto toggle = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(EchoFramePopup::onToggle), .57f);
            toggle->setPosition({right, y});
            toggle->setTag(i);
            toggle->toggle(states[i]);
            m_controls->addChild(toggle);
        }

        char const* percents[] = {"Deafen at", "Undeafen at", "Deafen after"};
        int values[] = {settings().deafenAt, settings().undeafenAt, settings().deafenAfter};
        for (int i = 0; i < 3; ++i) {
            float y = top - (i + 3) * step;
            auto name = label(m_autoPage, percents[i], left, y);
            if (i == 0) m_deafenAtLabel = name;
            auto input = TextInput::create(66.f, "0", "bigFont.fnt");
            input->setPosition({width - 72.f, y});
            input->setScale(.75f);
            input->setCommonFilter(CommonFilter::Uint);
            input->setMaxCharCount(3);
            input->setString(std::to_string(values[i]).c_str());
            input->setCallback([this, i](std::string const& typed) {
                if (typed.empty()) {
                    if (i == 2) {
                        auto& s = settings();
                        if (s.deafenAfter != 0) {
                            s.deafenAfter = 0;
                            s.save();
                        }
                        refresh();
                    }
                    return;
                }
                auto& s = settings();
                if (i == 0 && s.deafenAfter >= 1) return;
                int value = std::clamp(std::stoi(typed), 0, 100);
                int* target = i == 0 ? &s.deafenAt : i == 1 ? &s.undeafenAt : &s.deafenAfter;
                if (*target != value) {
                    *target = value;
                    s.save();
                }
                if (value == 100 && typed != "100")
                    m_percent[i]->setString("100");
                if (i == 2) refresh();
            });
            m_autoPage->addChild(input);
            m_percent[i] = input;
            m_percentSuffix[i] = label(m_autoPage, "%", width - 41.f, y, .42f);
        }

        float shortcutY = top - 6 * step;
        label(m_autoPage, "Shortcut", left, shortcutY);
        button("Set key", right - 24.f, shortcutY,
            menu_selector(EchoFramePopup::onShortcut));
        m_shortcutLabel = label(m_autoPage, "", width - 138.f, shortcutY - step * .67f, .35f);
        m_hint = label(m_autoPage, "", 25.f, 20.f, .26f);

        auto card = CCLayerColor::create({26, 33, 57, 245}, width - 44.f, 78.f);
        card->setPosition({22.f, height - 184.f});
        m_framePage->addChild(card);
        label(card, "FEATURE", 16.f, 58.f, .48f, "chatFont.fnt")
            ->setColor({129, 157, 223});
        label(card, "Frame Extrapolation", 16.f, 35.f, .68f, "bigFont.fnt");
        label(card, "SAVED PREFERENCE", 16.f, 13.f, .43f, "chatFont.fnt")
            ->setColor({159, 173, 203});
        auto toggleOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        auto toggleOn = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        auto frameToggle = CCMenuItemToggler::create(toggleOff, toggleOn, this,
            menu_selector(EchoFramePopup::onFrameToggle));
        frameToggle->toggle(Mod::get()->getSettingValue<bool>("enabled"));
        auto frameMenu = CCMenu::create();
        frameMenu->setPosition({0.f, 0.f});
        frameToggle->setPosition({width - 50.f, height - 142.f});
        frameMenu->addChild(frameToggle);
        m_framePage->addChild(frameMenu);
        m_frameState = label(m_framePage, "", width - 56.f, height - 170.f,
            .52f, "bigFont.fnt");
        label(m_framePage, "SMOOTHER VISUAL MOTION BETWEEN PHYSICS STEPS",
            32.f, height - 90.f, .48f, "chatFont.fnt")
            ->setColor({166, 181, 218});
        label(m_framePage, "VISUAL ONLY  -  PHYSICS UNCHANGED",
            50.f, height - 215.f, .42f, "chatFont.fnt")
            ->setColor({124, 137, 166});

        if (auto layer = PlayLayer::get()) {
            bool schedulerPaused = layer->getScheduler()->isTargetPaused(layer);
            log::info("[AD] popup pause state: native={} scheduler={}", layer->m_isPaused, schedulerPaused);
            if (!layer->m_isPaused && !schedulerPaused) {
                layer->pauseSchedulerAndActions();
                m_pauseOwner = layer;
            }
        }
        showPage(true);
        schedule(schedule_selector(EchoFramePopup::restoreEmptyAfterBlur));
        s_open = this;
        return true;
    }

    void onAutoTab(CCObject*) { showPage(true); }
    void onFrameTab(CCObject*) { showPage(false); }

    void onFrameToggle(CCObject*) {
        bool enabled = !Mod::get()->getSettingValue<bool>("enabled");
        Mod::get()->setSettingValue<bool>("enabled", enabled);
        log::info("[EchoFrame] Frame Extrapolation setting {}", enabled ? "ON" : "OFF");
        refresh();
    }

    void onToggle(CCObject* sender) {
        auto& s = settings();
        switch (static_cast<CCNode*>(sender)->getTag()) {
            case 0: s.undeafenOnPause = !s.undeafenOnPause; break;
            case 1: s.deafenInPractice = !s.deafenInPractice; break;
            case 2: s.deafenWithStartPos = !s.deafenWithStartPos; break;
        }
        s.save();
    }

    void onEnabled(CCObject*) {
        auto& s = settings();
        s.enabled = !s.enabled;
        s.save();
        Controller::get().enabledChanged(s.enabled);
    }

    void onShortcut(CCObject*) {
        log::info("[AD] shortcut capture armed");
        m_capturing = true;
        refresh();
    }

public:
    static EchoFramePopup* create() {
        auto popup = new EchoFramePopup();
        if (popup->initPopup()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
    static EchoFramePopup* openPopup() { return s_open; }
    bool isCapturing() const { return m_capturing; }

    void captureKey(enumKeyCodes key) {
        if (!m_capturing || key <= KEY_None || key > 255) return;
        log::info("[AD] shortcut captured key={}", static_cast<int>(key));
        settings().shortcut = key;
        settings().save();
        m_capturing = false;
        refresh();
    }

    void onClose(CCObject* sender) override {
        log::info("[EchoFrame] popup close");
        s_open = nullptr;
        releaseOwnedPause();
        Popup::onClose(sender);
    }

    void onExit() override {
        if (s_open == this) s_open = nullptr;
        releaseOwnedPause();
        Popup::onExit();
    }

    void keyDown(enumKeyCodes key, double timestamp) override {
        if (m_capturing) {
            captureKey(key);
            return;
        }
        Popup::keyDown(key, timestamp);
    }
};

EchoFramePopup* EchoFramePopup::s_open = nullptr;
