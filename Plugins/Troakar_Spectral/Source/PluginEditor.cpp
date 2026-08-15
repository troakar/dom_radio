#include "PluginProcessor.h"
#include "PluginEditor.h"

TroakarSpectralAudioProcessorEditor::TroakarSpectralAudioProcessorEditor (TroakarSpectralAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), eqGraph(p), gradientManager(p.gradientManager)
{
    setLookAndFeel(&customLookAndFeel);

    addAndMakeVisible(eqGraph);

    gradientOverlay = std::make_unique<GradientFilterOverlay>(gradientManager);
    gradientOverlay->onSelectionChanged = [this]() { 
        updateKnobStates(); 
        eqGraph.repaint();
    };
    gradientOverlay->onPointDeleted = [this](int) {
        audioProcessor.syncGradientPointsToAPVTS();
        updateKnobStates();
        eqGraph.repaint();
    };
    eqGraph.onGradientSelectionChanged = [this]() {
        updateKnobStates();
        gradientOverlay->repaint();
    };
    eqGraph.onGradientParamsChanged = [this]() {
        audioProcessor.syncGradientPointsToAPVTS();
        updateKnobStates(); 
    };
    addAndMakeVisible(gradientOverlay.get());

    fftComboBox.addItemList({"512 (Fast)", "1024 (Balanced)", "2048 (Precise)"}, 1);
    fftComboBox.setJustificationType(juce::Justification::centred);
    fftComboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(24, 22, 18));
    addAndMakeVisible(fftComboBox);
    fftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "FFT_MODE", fftComboBox);

    viewRangeComboBox.addItemList({"24dB", "48dB", "72dB", "96dB", "120dB"}, 1);
    viewRangeComboBox.setJustificationType(juce::Justification::centred);
    viewRangeComboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(24, 22, 18));
    addAndMakeVisible(viewRangeComboBox);
    viewRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "VIEW_RANGE", viewRangeComboBox);

    deltaButton.setButtonText("DELTA");
    deltaButton.setClickingTogglesState(true);
    deltaButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 30, 25));
    deltaButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(240, 140, 30));
    addAndMakeVisible(deltaButton);
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "DELTA_MODE", deltaButton);

    inGainKnob  = std::make_unique<GradientKnob>(audioProcessor.apvts, "IN_GAIN",  "IN GAIN",  false);
    outLvlKnob  = std::make_unique<GradientKnob>(audioProcessor.apvts, "OUT_LVL",  "OUT LVL",  false);
    mixKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "MIX",      "MIX",      false);

    linkButton.setButtonText("LINK");
    linkButton.setClickingTogglesState(true);
    linkButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 30, 25));
    linkButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(100, 200, 255));
    linkButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(120, 115, 100));
    linkButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    linkButton.setTooltip("Link In/Out Gain");
    addAndMakeVisible(linkButton);

    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "IO_LINK", linkButton);

    lastInGainValue = inGainKnob->slider.getValue();
    lastOutLvlValue = outLvlKnob->slider.getValue();

    inGainKnob->slider.onValueChange = [this]() {
        const double currentIn = inGainKnob->slider.getValue();
        if (linkButton.getToggleState() && !isUpdatingLink) {
            isUpdatingLink = true;
            double newOutValue = juce::jlimit(-24.0, 24.0, outLvlKnob->slider.getValue() - (currentIn - lastInGainValue));
            outLvlKnob->slider.setValue(newOutValue, juce::sendNotificationSync);
            lastOutLvlValue = newOutValue;
            isUpdatingLink = false;
        }
        lastInGainValue = currentIn;
    };

    outLvlKnob->slider.onValueChange = [this]() {
        const double currentOut = outLvlKnob->slider.getValue();
        if (linkButton.getToggleState() && !isUpdatingLink) {
            isUpdatingLink = true;
            double newInValue = juce::jlimit(-24.0, 24.0, inGainKnob->slider.getValue() - (currentOut - lastOutLvlValue));
            inGainKnob->slider.setValue(newInValue, juce::sendNotificationSync);
            lastInGainValue = newInValue;
            isUpdatingLink = false;
        }
        lastOutLvlValue = currentOut;
    };

    linkButton.onClick = [this]() {
        bool linked = linkButton.getToggleState();
        inGainKnob->setLinked(linked);
        outLvlKnob->setLinked(linked);
        if (linked) {
            lastInGainValue = inGainKnob->slider.getValue();
            lastOutLvlValue = outLvlKnob->slider.getValue();
        }
    };

    amountKnob    = std::make_unique<GradientKnob>(audioProcessor.apvts, "AMOUNT",         "AMOUNT",    true);
    upRangeKnob   = std::make_unique<GradientKnob>(audioProcessor.apvts, "UPWARD_RANGE",   "UP MAX",    true);
    downRangeKnob = std::make_unique<GradientKnob>(audioProcessor.apvts, "DOWNWARD_RANGE", "DOWN MAX",  true);
    speedKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "SPECTRAL_SPEED", "SPEED",     true);
    upSmoothKnob  = std::make_unique<GradientKnob>(audioProcessor.apvts, "UP_SMOOTH",    "UP SMOOTH",   true, true);
    downSmoothKnob= std::make_unique<GradientKnob>(audioProcessor.apvts, "DOWN_SMOOTH",  "DN SMOOTH",   true, true);
    upSelKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "UP_SEL",         "UP SEL",    true, true);
    downSelKnob   = std::make_unique<GradientKnob>(audioProcessor.apvts, "DOWN_SEL",       "DOWN SEL",  true, true);

    speedAutoButton.setButtonText("A");
    speedAutoButton.setClickingTogglesState(true);
    speedAutoButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 30, 25));
    speedAutoButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(255, 176, 40));
    speedAutoButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(120, 115, 100));
    speedAutoButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(speedAutoButton);

    attackKnob    = std::make_unique<GradientKnob>(audioProcessor.apvts, "ATTACK_MS",   "ATTACK",  true);
    releaseKnob   = std::make_unique<GradientKnob>(audioProcessor.apvts, "RELEASE_MS",  "RELEASE", true);
    kneeKnob      = std::make_unique<GradientKnob>(audioProcessor.apvts, "KNEE_WIDTH",  "KNEE",    true);
    lookaheadKnob = std::make_unique<GradientKnob>(audioProcessor.apvts, "LOOKAHEAD_MS", "LOOK-AHD", false);

    speedAutoButton.onClick = [this]() { 
        updateDynamicsKnobsVisibility(); 
        resized();
        repaint();
    };

    speedAutoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "SPEED_AUTO", speedAutoButton);

    addAndMakeVisible(attackKnob.get());
    addAndMakeVisible(releaseKnob.get());
    addAndMakeVisible(kneeKnob.get());
    addAndMakeVisible(lookaheadKnob.get());
    
    addAndMakeVisible(inGainKnob.get());
    addAndMakeVisible(outLvlKnob.get());
    addAndMakeVisible(mixKnob.get());
    addAndMakeVisible(amountKnob.get());
    addAndMakeVisible(upRangeKnob.get());
    addAndMakeVisible(downRangeKnob.get());
    addAndMakeVisible(speedKnob.get());
    addAndMakeVisible(upSmoothKnob.get());
    addAndMakeVisible(downSmoothKnob.get());
    addAndMakeVisible(upSelKnob.get());
    addAndMakeVisible(downSelKnob.get());

    updateDynamicsKnobsVisibility();
    updateKnobStates();
    setSize (1024, 720);
}

TroakarSpectralAudioProcessorEditor::~TroakarSpectralAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void TroakarSpectralAudioProcessorEditor::updateDynamicsKnobsVisibility()
{
    bool isAutoMode = speedAutoButton.getToggleState();
    
    speedKnob->setVisible(isAutoMode);
    attackKnob->setVisible(!isAutoMode);
    releaseKnob->setVisible(!isAutoMode);
    kneeKnob->setVisible(!isAutoMode);
    lookaheadKnob->setVisible(true);
}

void TroakarSpectralAudioProcessorEditor::updateKnobStates()
{
    bool isGradientMode = gradientManager.hasActivePoint();
    auto* point = gradientManager.getActivePoint();
    juce::Colour currentCapColor = (isGradientMode && point != nullptr) 
                                   ? point->color 
                                   : juce::Colour::fromRGB(180, 175, 160);

    inGainKnob->setGradientActive(false, currentCapColor);
    outLvlKnob->setGradientActive(false, currentCapColor);
    mixKnob->setGradientActive(false, currentCapColor);

    amountKnob->setGradientActive(isGradientMode, currentCapColor);
    upRangeKnob->setGradientActive(isGradientMode, currentCapColor);
    downRangeKnob->setGradientActive(isGradientMode, currentCapColor);
    upSmoothKnob->setGradientActive(isGradientMode, currentCapColor);
    downSmoothKnob->setGradientActive(isGradientMode, currentCapColor);
    upSelKnob->setGradientActive(isGradientMode, currentCapColor);
    downSelKnob->setGradientActive(isGradientMode, currentCapColor);

    bool isAutoMode = speedAutoButton.getToggleState();
    
    if (isAutoMode) {
        speedKnob->setGradientActive(isGradientMode, currentCapColor);
    } else {
        attackKnob->setGradientActive(isGradientMode, currentCapColor);
        releaseKnob->setGradientActive(isGradientMode, currentCapColor);
        kneeKnob->setGradientActive(isGradientMode, currentCapColor);
    }
    
    lookaheadKnob->setGradientActive(false, currentCapColor);

    if (isGradientMode && point != nullptr)
    {
        juce::String prefix = "GRADIENT_" + juce::String(point->id);
        
        amountKnob->bindToParameter(audioProcessor.apvts, prefix + "_AMOUNT");
        upRangeKnob->bindToParameter(audioProcessor.apvts, prefix + "_UP_MAX");
        downRangeKnob->bindToParameter(audioProcessor.apvts, prefix + "_DOWN_MAX");
        upSmoothKnob->bindToParameter(audioProcessor.apvts, prefix + "_UP_SMOOTH");
        downSmoothKnob->bindToParameter(audioProcessor.apvts, prefix + "_DOWN_SMOOTH");
        upSelKnob->bindToParameter(audioProcessor.apvts, prefix + "_UP_SEL");
        downSelKnob->bindToParameter(audioProcessor.apvts, prefix + "_DOWN_SEL");
        
        if (isAutoMode) {
            speedKnob->bindToParameter(audioProcessor.apvts, prefix + "_SPEED");
        } else {
            attackKnob->bindToParameter(audioProcessor.apvts, prefix + "_ATTACK");
            releaseKnob->bindToParameter(audioProcessor.apvts, prefix + "_RELEASE");
            kneeKnob->bindToParameter(audioProcessor.apvts, prefix + "_KNEE");
        }
    }
    else
    {
        amountKnob->bindToParameter(audioProcessor.apvts, "AMOUNT");
        upRangeKnob->bindToParameter(audioProcessor.apvts, "UPWARD_RANGE");
        downRangeKnob->bindToParameter(audioProcessor.apvts, "DOWNWARD_RANGE");
        upSmoothKnob->bindToParameter(audioProcessor.apvts, "UP_SMOOTH");
        downSmoothKnob->bindToParameter(audioProcessor.apvts, "DOWN_SMOOTH");
        upSelKnob->bindToParameter(audioProcessor.apvts, "UP_SEL");
        downSelKnob->bindToParameter(audioProcessor.apvts, "DOWN_SEL");
        
        if (isAutoMode) {
            speedKnob->bindToParameter(audioProcessor.apvts, "SPECTRAL_SPEED");
        } else {
            attackKnob->bindToParameter(audioProcessor.apvts, "ATTACK_MS");
            releaseKnob->bindToParameter(audioProcessor.apvts, "RELEASE_MS");
            kneeKnob->bindToParameter(audioProcessor.apvts, "KNEE_WIDTH");
        }

        std::vector<GradientKnob::GradientMarker> amountMarkers, upMarkers, downMarkers, upSmoothMarkers, downSmoothMarkers, upSelMarkers, downSelMarkers;
        std::vector<GradientKnob::GradientMarker> speedMarkers, attackMarkers, releaseMarkers, kneeMarkers;
        
        for (const auto& p : gradientManager.points) {
            amountMarkers.push_back({ p.id, p.amountPct / 300.0f, p.color });
            upMarkers.push_back({ p.id, p.upMaxDb / 48.0f, p.color });
            downMarkers.push_back({ p.id, (-p.downMaxDb) / 24.0f, p.color });
            upSmoothMarkers.push_back({ p.id, p.upSmoothPct / 100.0f, p.color });
            downSmoothMarkers.push_back({ p.id, p.downSmoothPct / 100.0f, p.color });
            upSelMarkers.push_back({ p.id, (p.upSelectivity + 100.0f) / 200.0f, p.color });
            downSelMarkers.push_back({ p.id, (p.downSelectivity + 100.0f) / 200.0f, p.color });
            
            if (p.useAutoSpeed) {
                speedMarkers.push_back({ p.id, p.speedPct / 100.0f, p.color });
            } else {
                attackMarkers.push_back({ p.id, (p.attackMs - 0.1f) / (100.0f - 0.1f), p.color });
                releaseMarkers.push_back({ p.id, (p.releaseMs - 10.0f) / (1000.0f - 10.0f), p.color });
                kneeMarkers.push_back({ p.id, p.kneeWidthDb / 12.0f, p.color });
            }
        }
        
        amountKnob->setGradientMarkers(amountMarkers);
        upRangeKnob->setGradientMarkers(upMarkers);
        downRangeKnob->setGradientMarkers(downMarkers);
        upSmoothKnob->setGradientMarkers(upSmoothMarkers);
        downSmoothKnob->setGradientMarkers(downSmoothMarkers);
        upSelKnob->setGradientMarkers(upSelMarkers);
        downSelKnob->setGradientMarkers(downSelMarkers);
        
        if (isAutoMode) {
            speedKnob->setGradientMarkers(speedMarkers);
        } else {
            attackKnob->setGradientMarkers(attackMarkers);
            releaseKnob->setGradientMarkers(releaseMarkers);
            kneeKnob->setGradientMarkers(kneeMarkers);
        }
    }
}

void TroakarSpectralAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB(18, 16, 14));

    auto bounds = getLocalBounds();
    
    auto topPanel = bounds.removeFromTop(44);
    g.setColour(juce::Colour::fromRGB(24, 22, 18));
    g.fillRect(topPanel);
    g.setColour(juce::Colour::fromRGB(45, 40, 32));
    g.drawRect(topPanel, 1.0f);

    g.setColour(juce::Colour::fromRGB(255, 176, 40));
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("TROAKAR SPECTRAL", topPanel.removeFromLeft(200).withTrimmedLeft(16), juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(120, 115, 100));
    g.setFont(juce::FontOptions(11.0f, juce::Font::italic));
    g.drawText("SURGICAL SPECTRAL DYNAMICS", topPanel.removeFromLeft(200), juce::Justification::centredLeft);

    auto leftPanel = bounds.removeFromLeft(90);
    g.setColour(juce::Colour::fromRGB(20, 18, 15));
    g.fillRect(leftPanel);
    g.setColour(juce::Colour::fromRGB(35, 30, 25));
    g.drawRect(leftPanel, 1.0f);

    auto bottomPanel = bounds.removeFromBottom(150);
    g.setColour(juce::Colour::fromRGB(22, 20, 17));
    g.fillRect(bottomPanel);
    g.setColour(juce::Colour::fromRGB(35, 30, 25));
    g.drawRect(bottomPanel, 1.0f);
}

void TroakarSpectralAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    auto topPanel = bounds.removeFromTop(44);
    auto topRight = topPanel.removeFromRight(320).reduced(0, 10);
    
    fftComboBox.setBounds(topRight.removeFromRight(110));
    topRight.removeFromRight(10);
    viewRangeComboBox.setBounds(topRight.removeFromRight(80));
    topRight.removeFromRight(10);
    deltaButton.setBounds(topRight.removeFromRight(70));
    
    topPanel.removeFromLeft(400);
    gradientOverlay->setBounds(topPanel.reduced(20, 7));

    auto leftPanel = bounds.removeFromLeft(90);
    int globalKnobH = leftPanel.getHeight() / 3;
    
    auto inArea = leftPanel.removeFromTop(globalKnobH).reduced(4);
    inGainKnob->setBounds(inArea);
    
    auto outArea = leftPanel.removeFromTop(globalKnobH).reduced(4);
    outLvlKnob->setBounds(outArea);
    
    mixKnob->setBounds(leftPanel.reduced(4));
    
    int linkW = 40, linkH = 18;
    linkButton.setBounds(inArea.getCentreX() - linkW/2, inArea.getBottom() - linkH/2 - 2, linkW, linkH);

    auto bottomPanel = bounds.removeFromBottom(150).reduced(0, 4);
    
    int blockW = bottomPanel.getWidth() / 7;

    auto amountArea = bottomPanel.removeFromLeft(blockW).reduced(4);
    amountKnob->setBounds(amountArea);

    auto upBlock = bottomPanel.removeFromLeft(blockW);
    upRangeKnob->setBounds(upBlock.removeFromTop(upBlock.getHeight() * 0.58f).reduced(4, 0));
    upSelKnob->setBounds(upBlock.reduced(10, 0)); 

    auto downBlock = bottomPanel.removeFromLeft(blockW);
    downRangeKnob->setBounds(downBlock.removeFromTop(downBlock.getHeight() * 0.58f).reduced(4, 0));
    downSelKnob->setBounds(downBlock.reduced(10, 0));

    auto timeBlock = bottomPanel.removeFromLeft(blockW * 2);
    speedAutoButton.setBounds(timeBlock.getRight() - 28, timeBlock.getY() + 10, 22, 22);
    timeBlock.removeFromRight(30); 

    if (speedAutoButton.getToggleState()) {
        speedKnob->setBounds(timeBlock.withSizeKeepingCentre(blockW, timeBlock.getHeight()).reduced(4));
    } else {
        int tkW = timeBlock.getWidth() / 3;
        attackKnob->setBounds(timeBlock.removeFromLeft(tkW).reduced(2, 4));
        releaseKnob->setBounds(timeBlock.removeFromLeft(tkW).reduced(2, 4));
        kneeKnob->setBounds(timeBlock.reduced(2, 4));
    }

    // Колонка сглаживания (6-я колонка снизу)
    auto smoothBlock = bottomPanel.removeFromLeft(blockW);
    upSmoothKnob->setBounds(smoothBlock.removeFromTop(smoothBlock.getHeight() * 0.5f).reduced(6, 2));
    downSmoothKnob->setBounds(smoothBlock.reduced(6, 2));

    auto lookArea = bottomPanel.reduced(4);
    lookaheadKnob->setBounds(lookArea);

    eqGraph.setBounds(bounds.reduced(6));
}
