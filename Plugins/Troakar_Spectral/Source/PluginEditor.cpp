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

    eqGraph.onGradientSelectionChanged = [this]() {
        updateKnobStates();
        gradientOverlay->repaint();
    };

    eqGraph.onGradientParamsChanged = [this]() {
        audioProcessor.syncGradientPointsToAPVTS();
        updateKnobStates(); 
    };
    addAndMakeVisible(gradientOverlay.get());

    inGainKnob  = std::make_unique<GradientKnob>(audioProcessor.apvts, "IN_GAIN",  "IN GAIN",  false);
    outLvlKnob  = std::make_unique<GradientKnob>(audioProcessor.apvts, "OUT_LVL",  "OUT LVL",  false);
    mixKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "MIX",      "MIX",      false);

    amountKnob    = std::make_unique<GradientKnob>(audioProcessor.apvts, "AMOUNT",         "AMOUNT",    true);
    upRangeKnob   = std::make_unique<GradientKnob>(audioProcessor.apvts, "UPWARD_RANGE",   "UP MAX",    true);
    downRangeKnob = std::make_unique<GradientKnob>(audioProcessor.apvts, "DOWNWARD_RANGE", "DOWN MAX",  true);
    speedKnob     = std::make_unique<GradientKnob>(audioProcessor.apvts, "SPECTRAL_SPEED", "SPEED",     true);
    smoothKnob    = std::make_unique<GradientKnob>(audioProcessor.apvts, "SMOOTHING",      "SMOOTH",    true);

    addAndMakeVisible(inGainKnob.get());
    addAndMakeVisible(outLvlKnob.get());
    addAndMakeVisible(mixKnob.get());
    addAndMakeVisible(amountKnob.get());
    addAndMakeVisible(upRangeKnob.get());
    addAndMakeVisible(downRangeKnob.get());
    addAndMakeVisible(speedKnob.get());
    addAndMakeVisible(smoothKnob.get());

    amountKnob->slider.onValueChange = [this]() { onGradientKnobChanged(); };
    upRangeKnob->slider.onValueChange = [this]() { onGradientKnobChanged(); };
    downRangeKnob->slider.onValueChange = [this]() { onGradientKnobChanged(); };
    speedKnob->slider.onValueChange = [this]() { onGradientKnobChanged(); };
    smoothKnob->slider.onValueChange = [this]() { onGradientKnobChanged(); };

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

    inGainKnob->setGradientActive(isGradientMode, currentCapColor);
    outLvlKnob->setGradientActive(isGradientMode, currentCapColor);
    mixKnob->setGradientActive(isGradientMode, currentCapColor);

    amountKnob->setGradientActive(isGradientMode, currentCapColor);
    upRangeKnob->setGradientActive(isGradientMode, currentCapColor);
    downRangeKnob->setGradientActive(isGradientMode, currentCapColor);
    speedKnob->setGradientActive(isGradientMode, currentCapColor);
    smoothKnob->setGradientActive(isGradientMode, currentCapColor);

    if (isGradientMode)
    {
        syncKnobsWithSelectedGradient();
    }
    else
    {
        std::vector<GradientKnob::GradientMarker> amountMarkers, upMarkers, downMarkers, speedMarkers, smoothMarkers;

        for (const auto& p : gradientManager.points)
        {
            amountMarkers.push_back({ p.amountPct / 150.0f, p.color });
            upMarkers.push_back    ({ p.upMaxDb / 24.0f,    p.color });
            downMarkers.push_back  ({ (p.downMaxDb + 24.0f) / 24.0f, p.color });
            speedMarkers.push_back ({ p.speedPct / 100.0f,  p.color });
            smoothMarkers.push_back({ p.smoothPct / 100.0f, p.color });
        }

        amountKnob->setGradientMarkers(amountMarkers);
        upRangeKnob->setGradientMarkers(upMarkers);
        downRangeKnob->setGradientMarkers(downMarkers);
        speedKnob->setGradientMarkers(speedMarkers);
        smoothKnob->setGradientMarkers(smoothMarkers);
    }
}

void TroakarSpectralAudioProcessorEditor::syncKnobsWithSelectedGradient()
{
    auto* point = gradientManager.getActivePoint();
    if (!point) return;

    amountKnob->slider.setValue(point->amountPct, juce::dontSendNotification);
    upRangeKnob->slider.setValue(point->upMaxDb, juce::dontSendNotification);
    downRangeKnob->slider.setValue(point->downMaxDb, juce::dontSendNotification);
    speedKnob->slider.setValue(point->speedPct, juce::dontSendNotification);
    smoothKnob->slider.setValue(point->smoothPct, juce::dontSendNotification);
}

void TroakarSpectralAudioProcessorEditor::onGradientKnobChanged()
{
    if (!gradientManager.hasActivePoint()) return;

    auto* point = gradientManager.getActivePoint();
    if (!point) return;

    point->amountPct   = (float)amountKnob->slider.getValue();
    point->upMaxDb     = (float)upRangeKnob->slider.getValue();
    point->downMaxDb   = (float)downRangeKnob->slider.getValue();
    point->speedPct    = (float)speedKnob->slider.getValue();
    point->smoothPct   = (float)smoothKnob->slider.getValue();

    audioProcessor.syncGradientPointsToAPVTS();
    updateKnobStates();
}

void TroakarSpectralAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB(24, 22, 18));
}

void TroakarSpectralAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);

    gradientOverlay->setBounds(bounds.removeFromTop(26));
    bounds.removeFromTop(6);

    eqGraph.setBounds(bounds.removeFromTop(400));
    bounds.removeFromTop(15);

    int knobWidth = bounds.getWidth() / 8;

    inGainKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    outLvlKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    mixKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    amountKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    upRangeKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    downRangeKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    speedKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
    smoothKnob->setBounds(bounds.removeFromLeft(knobWidth).reduced(4));
}
