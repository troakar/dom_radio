#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class VUMeterPro : public juce::Component, private juce::Timer
{
public:
    explicit VUMeterPro(DomRadioMasterAudioProcessor& p);
    void paint(juce::Graphics& g) override;
    void resized() override { dialCache = juce::Image(); }

private:
    void timerCallback() override;
    void rebuildDialCache();
    float dbToAngle(float dB) const;

    DomRadioMasterAudioProcessor& processor;
    juce::Image dialCache;

    float needleDb = -20.0f;
    float peakDb = -20.0f;
    int peakHoldFrames = 0;
    float peakLampGlow = 0.0f;

    static constexpr float minDb = -20.0f;
    static constexpr float maxDb = 3.0f;
    static constexpr float angleRange = 0.62f;
};
