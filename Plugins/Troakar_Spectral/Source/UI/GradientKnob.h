#pragma once
#include <JuceHeader.h>
#include "GradientBandModel.h"

class GradientKnob : public juce::Component
{
public:
    struct GradientMarker
    {
        int id = 0;
        float normalizedValue = 0.0f;
        juce::Colour color;
    };

    GradientKnob(juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& paramID,
                 const juce::String& labelText,
                 bool allowInGradientMode = true,
                 bool isSmallKnob = false);

    ~GradientKnob() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setGradientActive(bool active, juce::Colour capColor = juce::Colours::grey);
    void setLocked(bool locked);
    void setLinked(bool linked) { isLinkedState = linked; repaint(); }

    void setGradientMarkers(const std::vector<GradientMarker>& markers);

    void bindToParameter(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        attachment.reset();
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, slider);
    }

    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    bool allowGradientMode = true;
    bool isSmall = false;
    bool isLockedState = false;
    bool isGradientSelected = false;
    bool isLinkedState = false;
    juce::Colour activeCapColor { juce::Colour::fromRGB(180, 175, 160) };

    std::vector<GradientMarker> gradientMarkers;

    void drawKnobCap(juce::Graphics& g, juce::Rectangle<float> bounds, float angle);
    void drawOuterRing(juce::Graphics& g, juce::Rectangle<float> bounds, float startAngle, float endAngle);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GradientKnob)
};
