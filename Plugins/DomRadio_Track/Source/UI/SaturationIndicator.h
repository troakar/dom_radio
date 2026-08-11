#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class SaturationIndicator : public juce::Component, private juce::Timer
{
public:
    explicit SaturationIndicator(DomRadioTrackAudioProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;
private:
    DomRadioTrackAudioProcessor& processor;
    float inputSat = 0.0f, tapeSat = 0.0f;
    void drawVintageLED(juce::Graphics& g, juce::Rectangle<float> bounds, float level, const juce::String& label);
};
