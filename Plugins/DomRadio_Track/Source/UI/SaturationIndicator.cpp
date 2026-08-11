#include "SaturationIndicator.h"

SaturationIndicator::SaturationIndicator(DomRadioTrackAudioProcessor& p) : processor(p)
{
    startTimerHz(30);
}

void SaturationIndicator::timerCallback()
{
    inputSat = processor.getInputSaturationLevel();
    tapeSat = processor.getTapeSaturationLevel();
    repaint();
}

void SaturationIndicator::drawVintageLED(juce::Graphics& g, juce::Rectangle<float> bounds, float level, const juce::String& label)
{
    g.setColour(juce::Colour::fromRGB(40, 40, 40));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(label, bounds.withHeight(12), juce::Justification::centred);

    const float ledSize = 13.0f;
    const juce::Rectangle<float> ledArea(bounds.getCentreX() - ledSize * 0.5f, bounds.getY() + 14.0f, ledSize, ledSize);

    juce::ColourGradient socketGrad(juce::Colour::fromRGB(30, 30, 30), ledArea.getX(), ledArea.getY(),
                                    juce::Colour::fromRGB(110, 110, 110), ledArea.getRight(), ledArea.getBottom(), false);
    g.setGradientFill(socketGrad);
    g.fillEllipse(ledArea);

    const float glow = juce::jlimit(0.0f, 1.0f, level);

    juce::Colour drawColor;
    if (glow < 0.25f) {
        drawColor = juce::Colour::fromRGB(40, 180, 60).interpolatedWith(
                    juce::Colour::fromRGB(210, 210, 30), glow / 0.25f);
    } else if (glow < 0.60f) {
        drawColor = juce::Colour::fromRGB(210, 210, 30).interpolatedWith(
                    juce::Colour::fromRGB(255, 120, 10), (glow - 0.25f) / 0.35f);
    } else {
        drawColor = juce::Colour::fromRGB(255, 120, 10).interpolatedWith(
                    juce::Colour::fromRGB(210, 15, 30), juce::jlimit(0.0f, 1.0f, (glow - 0.60f) / 0.40f));
    }

    const juce::Colour finalColor = (glow < 0.05f)
        ? juce::Colour::fromRGB(35, 35, 35)
        : drawColor.withMultipliedBrightness(0.55f + glow * 0.55f);

    g.setColour(finalColor);
    g.fillEllipse(ledArea.reduced(1.8f));

    if (glow > 0.08f) {
        juce::ColourGradient halo(finalColor.withAlpha(0.65f * glow),
                                  ledArea.getCentre().getX(), ledArea.getCentre().getY(),
                                  finalColor.withAlpha(0.0f),
                                  ledArea.getCentre().getX(), ledArea.getCentre().getY() - ledSize * 1.5f,
                                  true);
        g.setGradientFill(halo);
        g.fillEllipse(ledArea.expanded(ledSize * 0.5f * glow));
    }

    g.setColour(juce::Colours::white.withAlpha(0.40f));
    g.fillEllipse(ledArea.getX() + 2.5f, ledArea.getY() + 2.0f, 3.5f, 2.0f);
}

void SaturationIndicator::paint(juce::Graphics& g)
{
    float w = (float)getWidth() / 2.0f;
    drawVintageLED(g, juce::Rectangle<float>(0.0f, 0.0f, w, (float)getHeight()), inputSat, "IN");
    drawVintageLED(g, juce::Rectangle<float>(w, 0.0f, w, (float)getHeight()), tapeSat, "TAPE");
}
