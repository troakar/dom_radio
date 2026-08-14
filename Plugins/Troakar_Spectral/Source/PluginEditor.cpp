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

    deltaButton.setButtonText("DELTA");
    deltaButton.setClickingTogglesState(true);
    deltaButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 30, 25));
    deltaButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(240, 140, 30));
    addAndMakeVisible(deltaButton);
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "DELTA_MODE", deltaButton);

    inGainKnob  = std::make_unique<GradientKnob>(audioProcessor.apvts, "IN_GAIN",  "IN GAIN",  false);
    outLvlKnob  = std::make_unique<GradientKnob>(audioProcessor.apvts, "OUT_LVL",  "OUT LVL",  false);
    mixKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "MIX",      "MIX",      false);

    amountKnob    = std::make_unique<GradientKnob>(audioProcessor.apvts, "AMOUNT",         "AMOUNT",    true);
    upRangeKnob   = std::make_unique<GradientKnob>(audioProcessor.apvts, "UPWARD_RANGE",   "UP MAX",    true);
    downRangeKnob = std::make_unique<GradientKnob>(audioProcessor.apvts, "DOWNWARD_RANGE", "DOWN MAX",  true);
    speedKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "SPECTRAL_SPEED", "SPEED",     true);
    smoothKnob    = std::make_unique<GradientKnob>(audioProcessor.apvts, "SMOOTHING",      "SMOOTH",    true);
    upSelKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "UP_SEL",         "UP SEL",    true);
    downSelKnob   = std::make_unique<GradientKnob>(audioProcessor.apvts, "DOWN_SEL",       "DOWN SEL",  true);

    addAndMakeVisible(inGainKnob.get());
    addAndMakeVisible(outLvlKnob.get());
    addAndMakeVisible(mixKnob.get());
    addAndMakeVisible(amountKnob.get());
    addAndMakeVisible(upRangeKnob.get());
    addAndMakeVisible(downRangeKnob.get());
    addAndMakeVisible(speedKnob.get());
    addAndMakeVisible(smoothKnob.get());
    addAndMakeVisible(upSelKnob.get());
    addAndMakeVisible(downSelKnob.get());

    updateKnobStates();
    setSize (960, 620);
}

TroakarSpectralAudioProcessorEditor::~TroakarSpectralAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void TroakarSpectralAudioProcessorEditor::updateKnobStates()
{
    bool isGradientMode = gradientManager.hasActivePoint();
    auto* point = gradientManager.getActivePoint();
    juce::Colour currentCapColor = isGradientMode ? point->color : juce::Colour::fromRGB(180, 175, 160);

    inGainKnob->setGradientActive(false, currentCapColor);
    outLvlKnob->setGradientActive(false, currentCapColor);
    mixKnob->setGradientActive(false, currentCapColor);

    amountKnob->setGradientActive(isGradientMode, currentCapColor);
    upRangeKnob->setGradientActive(isGradientMode, currentCapColor);
    downRangeKnob->setGradientActive(isGradientMode, currentCapColor);
    speedKnob->setGradientActive(isGradientMode, currentCapColor);
    smoothKnob->setGradientActive(isGradientMode, currentCapColor);
    upSelKnob->setGradientActive(isGradientMode, currentCapColor);
    downSelKnob->setGradientActive(isGradientMode, currentCapColor);

    if (isGradientMode)
    {
        juce::String prefix = "GRADIENT_" + juce::String(point->id);
        amountKnob->bindToParameter(audioProcessor.apvts, prefix + "_AMOUNT");
        upRangeKnob->bindToParameter(audioProcessor.apvts, prefix + "_UP_MAX");
        downRangeKnob->bindToParameter(audioProcessor.apvts, prefix + "_DOWN_MAX");
        speedKnob->bindToParameter(audioProcessor.apvts, prefix + "_SPEED");
        smoothKnob->bindToParameter(audioProcessor.apvts, prefix + "_SMOOTH");
        upSelKnob->bindToParameter(audioProcessor.apvts, prefix + "_UP_SEL");
        downSelKnob->bindToParameter(audioProcessor.apvts, prefix + "_DOWN_SEL");
    }
    else
    {
        amountKnob->bindToParameter(audioProcessor.apvts, "AMOUNT");
        upRangeKnob->bindToParameter(audioProcessor.apvts, "UPWARD_RANGE");
        downRangeKnob->bindToParameter(audioProcessor.apvts, "DOWNWARD_RANGE");
        speedKnob->bindToParameter(audioProcessor.apvts, "SPECTRAL_SPEED");
        smoothKnob->bindToParameter(audioProcessor.apvts, "SMOOTHING");
        upSelKnob->bindToParameter(audioProcessor.apvts, "UP_SEL");
        downSelKnob->bindToParameter(audioProcessor.apvts, "DOWN_SEL");

        std::vector<GradientKnob::GradientMarker> amountMarkers, upMarkers, downMarkers, speedMarkers, smoothMarkers, upSelMarkers, downSelMarkers;
        for (const auto& p : gradientManager.points) {
            amountMarkers.push_back({ p.id, p.amountPct / 150.0f, p.color });
            upMarkers.push_back    ({ p.id, p.upMaxDb / 24.0f,    p.color });
            downMarkers.push_back  ({ p.id, (-p.downMaxDb) / 24.0f, p.color });
            speedMarkers.push_back ({ p.id, p.speedPct / 100.0f,  p.color });
            smoothMarkers.push_back({ p.id, p.smoothPct / 100.0f, p.color });
            upSelMarkers.push_back ({ p.id, (p.upSelectivity + 100.0f) / 200.0f, p.color });
            downSelMarkers.push_back({p.id, (p.downSelectivity + 100.0f) / 200.0f, p.color });
        }
        amountKnob->setGradientMarkers(amountMarkers);
        upRangeKnob->setGradientMarkers(upMarkers);
        downRangeKnob->setGradientMarkers(downMarkers);
        speedKnob->setGradientMarkers(speedMarkers);
        smoothKnob->setGradientMarkers(smoothMarkers);
        upSelKnob->setGradientMarkers(upSelMarkers);
        downSelKnob->setGradientMarkers(downSelMarkers);
    }
}

void TroakarSpectralAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB(24, 22, 18));
}

void TroakarSpectralAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);

    auto topBar = bounds.removeFromTop(26);
    
    fftComboBox.setBounds(topBar.removeFromRight(120));
    topBar.removeFromRight(10);
    deltaButton.setBounds(topBar.removeFromRight(60));
    topBar.removeFromRight(10);
    
    gradientOverlay->setBounds(topBar);

    bounds.removeFromTop(6);
    eqGraph.setBounds(bounds.removeFromTop(400));
    bounds.removeFromTop(15);

    int knobWidth = bounds.getWidth() / 10;
    inGainKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    outLvlKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    mixKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    amountKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    upRangeKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    downRangeKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    speedKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    smoothKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    upSelKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    downSelKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
}
