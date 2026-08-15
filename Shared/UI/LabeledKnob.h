#pragma once
#include <JuceHeader.h>

class LabeledKnob : public juce::Component
{
public:
    LabeledKnob(juce::AudioProcessorValueTreeState& apvts,
                const juce::String& paramID, 
                const juce::String& labelText,
                bool isLarge = false)
        : isLargeKnob(isLarge)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        // Компактный текстовый бокс (высота 14px вместо 18px, ширина на весь компонент)
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 85, 14);
        slider.setPopupDisplayEnabled(true, true, nullptr);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(slider);
        
        label.setText(labelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::FontOptions(isLarge ? 11.0f : 9.5f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(185, 180, 165));
        label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramID, slider);
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds();
        const int labelH = isLargeKnob ? 16 : 13;
        label.setBounds(bounds.removeFromTop(labelH));
        // Отдаем ВСЕ оставшееся место слайдеру
        slider.setBounds(bounds);
    }

    juce::Slider slider;

private:
    bool isLargeKnob = false;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};