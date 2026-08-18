#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class DomRadioMasterAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer,
      private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit DomRadioMasterAudioProcessorEditor (
        DomRadioMasterAudioProcessor&);

    ~DomRadioMasterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DomRadioMasterAudioProcessor& audioProcessor;

    std::unique_ptr<juce::WebBrowserComponent> webView;

    bool contextMenuGuardInjected = false;

    void timerCallback() override;

    void parameterChanged (
        const juce::String& parameterID,
        float newValue) override;

    std::optional<juce::WebBrowserComponent::Resource>
    getResource (const juce::String& requestedPath);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        DomRadioMasterAudioProcessorEditor)
};
