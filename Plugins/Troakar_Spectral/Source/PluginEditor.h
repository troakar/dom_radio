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
    std::unique_ptr<GradientKnob> upSelKnob, downSelKnob;

    juce::TextButton deltaButton;
    juce::ComboBox fftComboBox;
    juce::ComboBox viewRangeComboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> viewRangeAttachment;

    juce::TextButton linkButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttachment;
    double lastInGainValue = 0.0;
    double lastOutLvlValue = 0.0;
    bool isUpdatingLink = false;

    juce::TextButton speedAutoButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> speedAutoAttachment;
    std::unique_ptr<GradientKnob> attackKnob;
    std::unique_ptr<GradientKnob> releaseKnob;
    std::unique_ptr<GradientKnob> kneeKnob;
    std::unique_ptr<GradientKnob> lookaheadKnob;

    void updateKnobStates();
    void updateDynamicsKnobsVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TroakarSpectralAudioProcessorEditor)
};
