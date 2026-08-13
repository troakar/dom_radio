#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/EQGraphLED.h"
#include "UI/GradientKnob.h"
#include "UI/GradientBandModel.h"
#include "UI/GradientFilterOverlay.h"
#include "UI/DomRadioLookAndFeel.h"

class TroakarSpectralAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    TroakarSpectralAudioProcessorEditor (TroakarSpectralAudioProcessor&);
    ~TroakarSpectralAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    TroakarSpectralAudioProcessor& audioProcessor;
    
    DomRadioLookAndFeel customLookAndFeel;
    EQGraphLED eqGraph;

    GradientPointManager& gradientManager;
    std::unique_ptr<GradientFilterOverlay> gradientOverlay;

    std::unique_ptr<GradientKnob> inGainKnob, outLvlKnob, mixKnob;
    std::unique_ptr<GradientKnob> amountKnob, upRangeKnob, downRangeKnob, speedKnob, smoothKnob;

    void updateKnobStates();
    void syncKnobsWithSelectedGradient();
    void onGradientKnobChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TroakarSpectralAudioProcessorEditor)
};
