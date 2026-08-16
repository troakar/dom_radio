#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class TroakarSpectralAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer,
                                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit TroakarSpectralAudioProcessorEditor (TroakarSpectralAudioProcessor&);
    ~TroakarSpectralAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    TroakarSpectralAudioProcessor& processor;
    std::unique_ptr<juce::WebBrowserComponent> webView;

    bool contextMenuGuardInjected = false;

    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& path);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TroakarSpectralAudioProcessorEditor)
};
