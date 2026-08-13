#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "GradientBandModel.h"

class EQGraphLED : public juce::Component, private juce::Timer
{
public:
    explicit EQGraphLED(TroakarSpectralAudioProcessor& p);
    ~EQGraphLED() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { draggingNode = -1; draggingGradientId = -1; }
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void()> onGradientSelectionChanged;
    std::function<void()> onGradientParamsChanged;

private:
    struct ParameterListener : juce::AudioProcessorValueTreeState::Listener
    {
        ParameterListener(EQGraphLED& owner) : owner(owner) {}
        void parameterChanged(const juce::String&, float) override { owner.targetPathDirty = true; }
        EQGraphLED& owner;
    };

    void timerCallback() override
    {
        if (targetPathDirty)
            updateTargetCurveCache();

        buildDeltaPaths();
        repaint();
    }
    void rebuildBackgroundCache();
    void updateTargetCurveCache();

    float getTargetCurveDb(double freq) const;
    float getInterpolatedArray(const std::atomic<float>* arr, double freq, double sr) const;

    juce::Path& buildTargetCurvePath() const;
    void buildDeltaPaths();
    void drawSpectrumFog(juce::Graphics& g, const juce::Rectangle<float>& bounds);

    int createEQBandAt(float freq, float gainDb);

    float freqToX(float f) const;
    float xToFreq(float x) const;
    float gainToY(float dB) const;
    float yToGain(float y) const;

    TroakarSpectralAudioProcessor& processor;
    GradientPointManager& gradientManager;
    juce::Image backgroundCache;

    juce::Slider globalThreshSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> threshAttachment;

    int draggingNode = -1;
    int hoveredNode = -1;
    float dragStartFreq = 0.0f;
    float dragStartGain = 0.0f;

    int draggingGradientId = -1;
    int hoveredGradientId = -1;

    std::vector<float> freqPerPixel;
    std::vector<float> targetDbPerPixel;
    mutable juce::Path cachedTargetPath;
    mutable juce::Path cachedSpecPath;

    juce::Path cachedUpFill;
    juce::Path cachedDownFill;
    juce::Path cachedUpLine;
    juce::Path cachedDownLine;

    bool targetPathDirty = true;
    std::unique_ptr<ParameterListener> paramListener;

    static constexpr float maxDb = 24.0f;
    const juce::Colour phosphor { juce::Colour::fromRGB(255, 176, 40) };
};
