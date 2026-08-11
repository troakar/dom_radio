#pragma once
#include <JuceHeader.h>

class DomRadioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DomRadioLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont(juce::Label&) override;

    juce::Font getHeaderFont(float height = 24.0f) const;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    const juce::Image& getNoiseTexture() const noexcept { return noiseTexture; }

private:
    juce::Image noiseTexture;
    juce::Font customKnobFont;
    void createProceduralNoiseTexture();
};
