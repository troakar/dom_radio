#pragma once
#include <JuceHeader.h>
#include <atomic>
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
        void parameterChanged(const juce::String& paramID, float) override 
        { 
            if (paramID == "VIEW_RANGE") 
            {
                owner.updateViewRange(owner.processor.getViewRangeIndex());
            }
            else
            {
                owner.targetPathDirty = true; 
            }
        }
        EQGraphLED& owner;
    };

    void timerCallback() override
    {
        const int currentVisualFFTSize = processor.getCurrentFFTSize();

        if (currentVisualFFTSize != lastVisualFFTSize) {
            lastVisualFFTSize = currentVisualFFTSize;
            cachedUpFill.clear();
            cachedDownFill.clear();
            cachedUpLine.clear();
            cachedDownLine.clear();
            cachedSpecPath.clear();
            cachedScPath.clear();
            targetPathDirty = true;
        }

        const int currentFFTMode =
            processor.getFFTModeIndex();

        if (currentFFTMode != lastFFTMode) {
            lastFFTMode = currentFFTMode;
            targetPathDirty = true;
        }

        for (auto& p : gradientManager.points) {
            juce::String prefix = "GRADIENT_" + juce::String(p.id);
            
            p.active          = *processor.apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
            
            p.centerFreqHz    = *processor.apvts.getRawParameterValue(prefix + "_CENTER_FREQ");
            p.centerGainDb    = *processor.apvts.getRawParameterValue(prefix + "_CENTER_GAIN");
            p.radiusOctaves   = *processor.apvts.getRawParameterValue(prefix + "_BANDWIDTH");
            p.amountPct       = *processor.apvts.getRawParameterValue(prefix + "_AMOUNT");
            p.upMaxDb         = *processor.apvts.getRawParameterValue(prefix + "_UP_MAX");
            p.downMaxDb       = -(*processor.apvts.getRawParameterValue(prefix + "_DOWN_MAX"));
            p.speedPct        = *processor.apvts.getRawParameterValue(prefix + "_SPEED");
            p.upSmoothPct     = *processor.apvts.getRawParameterValue(prefix + "_UP_SMOOTH");
            p.downSmoothPct   = *processor.apvts.getRawParameterValue(prefix + "_DOWN_SMOOTH");
            p.upSelectivity   = *processor.apvts.getRawParameterValue(prefix + "_UP_SEL");
            p.downSelectivity = *processor.apvts.getRawParameterValue(prefix + "_DOWN_SEL");
        }

        if (targetPathDirty)
            updateTargetCurveCache();

        buildDeltaPaths();
        
        float currentThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");
        updateViewportFollow(currentThresh);
        
        repaint();
    }
    void rebuildBackgroundCache();
    void updateTargetCurveCache();

    float getTargetCurveDb(double freq) const;
    float getInterpolatedArray(const std::atomic<float>* arr, double freq, double sr) const;

    float getTotalTargetDbAtFreq(float freq) const
    {
        float eqDb = getTargetCurveDb(freq);
        float globalThresh = *processor.apvts.getRawParameterValue("GLOBAL_THRESH");
        float gradOffset = 0.0f;

        for (const auto& gp : gradientManager.points)
        {
            if (!gp.active) continue;
            float logDist = std::abs(std::log2(freq / gp.centerFreqHz));
            float normDist = logDist / std::max(0.1f, gp.radiusOctaves);
            if (normDist < 1.0f)
            {
                float weight = 0.5f + 0.5f * std::cos(normDist * juce::MathConstants<float>::pi);
                gradOffset += gp.centerGainDb * weight;
            }
        }
        return eqDb + globalThresh + gradOffset;
    }

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
    mutable juce::Path cachedScPath;

    juce::Path cachedUpFill;
    juce::Path cachedDownFill;
    juce::Path cachedUpLine;
    juce::Path cachedDownLine;

    bool targetPathDirty = true;
    int lastFFTMode = -1;
    int lastVisualFFTSize = 512;
    std::unique_ptr<ParameterListener> paramListener;

    float minDb = -18.0f;  
    float maxDb = 12.0f;     // Запас +12 dB сверху
    float baseViewDepth = 18.0f;
    float viewportOffset = 0.0f;
    float zoomIndicatorAlpha = 0.0f;

    void updateViewRange(int rangeIndex);
    void updateViewportFollow(float currentThreshold);
    void cycleViewRange(int direction);

    // Кэшированная логарифмическая частотная сетка для спектра
    std::vector<float> displayFrequencies;
    int cachedNumDisplayPoints = 0;
    void rebuildDisplayFrequencyGrid();

    const juce::Colour phosphor { juce::Colour::fromRGB(255, 176, 40) };
};
