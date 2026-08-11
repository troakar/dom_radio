#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class EQGraphLED : public juce::Component, private juce::Timer
{
public:
    explicit EQGraphLED(DomRadioTrackAudioProcessor& p);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { draggingNode = -1; }

private:
    void timerCallback() override { repaint(); }
    void rebuildBackgroundCache();
    
    juce::Path buildResponsePath(const DomRadioTrackAudioProcessor::TapeDisplayState& state) const;
    juce::Path buildNoiseFloorPath(const DomRadioTrackAudioProcessor::TapeDisplayState& state, float noise, float age) const;

    float freqToX(float f) const;
    float xToFreq(float x) const;
    float gainToY(float dB) const;
    float yToGain(float y) const;

    DomRadioTrackAudioProcessor& processor;
    juce::Image backgroundCache;
    juce::Image persistenceBuffer;
    int draggingNode = -1;
    float dragStartFreq = 0.0f;
    float dragStartGain = 0.0f;

    static constexpr float maxDb = 12.0f;
    const juce::Colour phosphor { juce::Colour::fromRGB(255, 176, 40) };
};
