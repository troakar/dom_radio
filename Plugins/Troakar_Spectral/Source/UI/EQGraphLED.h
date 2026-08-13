#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class EQGraphLED : public juce::Component, private juce::Timer
{
public:
    explicit EQGraphLED(TroakarSpectralAudioProcessor& p);
    ~EQGraphLED() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { draggingNode = -1; }
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    void timerCallback() override { repaint(); }
    void rebuildBackgroundCache();
    
    float getTargetCurveDb(double freq) const;
    float getInterpolatedArray(const std::atomic<float>* arr, double freq, double sr) const;
    
    juce::Path buildTargetCurvePath() const;
    juce::Path buildActiveCurvePath() const;
    juce::Path buildDeltaPath(bool isUpward) const;
    void drawSpectrumFog(juce::Graphics& g, const juce::Rectangle<float>& bounds);

    float freqToX(float f) const;
    float xToFreq(float x) const;
    float gainToY(float dB) const;
    float yToGain(float y) const;

    TroakarSpectralAudioProcessor& processor;
    juce::Image backgroundCache;

    int draggingNode = -1;
    int hoveredNode = -1;
    float dragStartFreq = 0.0f;
    float dragStartGain = 0.0f;

    static constexpr float maxDb = 24.0f;
    const juce::Colour phosphor { juce::Colour::fromRGB(255, 176, 40) };
};