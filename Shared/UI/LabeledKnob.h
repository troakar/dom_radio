#pragma once
#include <JuceHeader.h>

class LabeledKnob : public juce::Component
{
public:
    LabeledKnob(juce::AudioProcessorValueTreeState& apvts,
                const juce::String& paramID, const juce::String& labelText)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled(true, true, nullptr);
        addAndMakeVisible(slider);

        label.setText(labelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(30, 30, 30));
        label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramID, slider);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds(b.removeFromTop(14));
        slider.setBounds(b);
    }

    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
