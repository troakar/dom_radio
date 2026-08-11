#include "DomRadioLookAndFeel.h"

DomRadioLookAndFeel::DomRadioLookAndFeel()
{
    auto typeface = juce::Typeface::createSystemTypefaceFor(BinaryData::GOSTtypeB_ttf, BinaryData::GOSTtypeB_ttfSize);

    if (typeface != nullptr) {
        customKnobFont = juce::Font(juce::FontOptions(typeface).withHeight(9.5f));
    } else {
        customKnobFont = juce::Font(juce::FontOptions(9.5f, juce::Font::bold));
    }

    setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(40, 40, 40));
    setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(80, 80, 80));
    setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(240, 235, 220));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(40, 40, 40));
    createProceduralNoiseTexture();
}

void DomRadioLookAndFeel::createProceduralNoiseTexture()
{
    const int w = 256, h = 256;
    noiseTexture = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Random rng(1978);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float n = (rng.nextFloat() - 0.5f) * 0.18f;
            float val = 0.5f + n;

            if (rng.nextFloat() < 0.003f) val -= 0.25f;

            noiseTexture.setPixelAt(x, y, juce::Colour::fromFloatRGBA(val, val, val, std::abs(n) * 2.2f));
        }
    }
}

juce::Font DomRadioLookAndFeel::getLabelFont(juce::Label&)
{
    return customKnobFont;
}

juce::Font DomRadioLookAndFeel::getHeaderFont(float height) const
{
    auto typeface = juce::Typeface::createSystemTypefaceFor(BinaryData::GOSTtypeB_ttf, BinaryData::GOSTtypeB_ttfSize);
    if (typeface != nullptr)
        return juce::Font(juce::FontOptions(typeface).withHeight(height));
    return juce::Font(juce::FontOptions(height, juce::Font::bold));
}

void DomRadioLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (!label.isBeingEdited())
    {
        const float alpha = label.isEnabled() ? 1.0f : 0.5f;

        // РЕШЕНИЕ: берем цвет, настроенный для конкретного лейбла, вместо хардкода!
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(getLabelFont(label));

        auto textArea = label.getBorderSize().subtractedFrom(label.getLocalBounds());
        juce::String textInAllCaps = label.getText().toUpperCase();

        g.drawText(textInAllCaps, textArea, label.getJustificationType(), true);
    }
}

void DomRadioLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPosProportional, float rotaryStartAngle,
                                           float rotaryEndAngle, juce::Slider&)
{
    const float radius = static_cast<float>(juce::jmin(width / 2, height / 2)) - 3.0f;
    const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float rx = centreX - radius;
    const float ry = centreY - radius;
    const float rw = radius * 2.0f;
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const bool isLargeKnob = (width >= 60 || height >= 60);

    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillEllipse(rx + 2.0f, ry + 4.0f, rw, rw);

    if (isLargeKnob)
    {
        juce::ColourGradient metalGrad(juce::Colour::fromRGB(220, 215, 205), centreX - radius * 0.7f, centreY - radius * 0.7f,
                                       juce::Colour::fromRGB(70, 68, 62), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
        metalGrad.addColour(0.3, juce::Colour::fromRGB(180, 175, 165));
        metalGrad.addColour(0.7, juce::Colour::fromRGB(110, 105, 95));
        g.setGradientFill(metalGrad);
        g.fillEllipse(rx, ry, rw, rw);

        const int numTeeth = 36;
        for (int i = 0; i < numTeeth; ++i) {
            float a = (juce::MathConstants<float>::twoPi / (float)numTeeth) * i;
            float cosA = std::cos(a), sinA = std::sin(a);
            bool isHighlight = (sinA < 0.0f);
            g.setColour(isHighlight ? juce::Colour::fromRGB(230, 225, 215) : juce::Colour::fromRGB(50, 48, 42));
            g.drawLine(centreX + cosA * (radius - 3.5f), centreY + sinA * (radius - 3.5f),
                       centreX + cosA * radius, centreY + sinA * radius, 1.4f);
        }

        const float innerR = radius - 4.0f;
        juce::ColourGradient innerGrad(juce::Colour::fromRGB(205, 200, 190), centreX, centreY - innerR,
                                       juce::Colour::fromRGB(95, 90, 82), centreX, centreY + innerR, false);
        g.setGradientFill(innerGrad);
        g.fillEllipse(centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f);

        g.setColour(juce::Colour::fromRGB(50, 48, 42).withAlpha(0.25f));
        g.drawEllipse(centreX - innerR * 0.75f, centreY - innerR * 0.75f, innerR * 1.5f, innerR * 1.5f, 0.8f);

        juce::Path specArc;
        specArc.addArc(centreX - innerR + 1.0f, centreY - innerR + 1.0f, innerR * 2.0f - 2.0f, innerR * 2.0f - 2.0f, -1.0f, 0.6f, true);
        juce::ColourGradient specGrad(juce::Colours::white.withAlpha(0.40f), centreX, centreY - innerR,
                                      juce::Colours::white.withAlpha(0.0f), centreX, centreY, false);
        g.setGradientFill(specGrad);
        g.fillPath(specArc);

        juce::Path pointer;
        const float pointerLength = innerR * 0.85f;
        const float pointerThickness = 2.5f;
        pointer.addRoundedRectangle(-pointerThickness * 0.5f, -innerR + 2.0f, pointerThickness, pointerLength, 1.0f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.strokePath(pointer, juce::PathStrokeType(1.0f));

        g.setColour(juce::Colour::fromRGB(230, 120, 20));
        g.fillPath(pointer);
    }
    else
    {
        juce::ColourGradient baseGradient(juce::Colour::fromRGB(65, 65, 65), centreX, ry,
                                          juce::Colour::fromRGB(15, 15, 15), centreX, ry + rw, false);
        g.setGradientFill(baseGradient);
        g.fillEllipse(rx, ry, rw, rw);

        const int numTeeth = 24;
        for (int i = 0; i < numTeeth; ++i) {
            float a = (juce::MathConstants<float>::twoPi / (float)numTeeth) * i;
            float cosA = std::cos(a), sinA = std::sin(a);
            bool isHighlight = (sinA < 0.0f);
            g.setColour(isHighlight ? juce::Colour::fromRGB(85, 85, 85) : juce::Colour::fromRGB(10, 10, 10));
            g.drawLine(centreX + cosA * (radius - 2.5f), centreY + sinA * (radius - 2.5f),
                       centreX + cosA * radius, centreY + sinA * radius, 1.2f);
        }

        const float innerRadius = radius * 0.58f;
        juce::ColourGradient domeGradient(juce::Colour::fromRGB(210, 205, 190), centreX - innerRadius, centreY - innerRadius,
                                          juce::Colour::fromRGB(80, 75, 65), centreX + innerRadius, centreY + innerRadius, true);
        g.setGradientFill(domeGradient);
        g.fillEllipse(centreX - innerRadius, centreY - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);

        juce::Path pointer;
        const float pointerLength = radius * 0.82f;
        const float pointerThickness = 2.2f;
        pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 3.0f, pointerThickness, pointerLength, 1.0f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        g.setColour(juce::Colour::fromRGB(225, 125, 25));
        g.fillPath(pointer);
    }
}

void DomRadioLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float, float,
                                           const juce::Slider::SliderStyle, juce::Slider&)
{
    auto trackBounds = juce::Rectangle<float>((float)x, (float)y + (float)height * 0.35f, (float)width, (float)height * 0.30f);

    g.setColour(juce::Colour::fromRGB(130, 125, 115));
    g.drawRoundedRectangle(trackBounds.expanded(1.0f), 3.0f, 1.0f);

    juce::ColourGradient grad(
        juce::Colour::fromRGB(50, 130, 210), trackBounds.getX(), trackBounds.getCentreY(),
        juce::Colour::fromRGB(220, 40, 20), trackBounds.getRight(), trackBounds.getCentreY(), false);
    grad.addColour(0.5, juce::Colour::fromRGB(235, 195, 75));

    g.setGradientFill(grad);
    g.fillRoundedRectangle(trackBounds, 2.0f);

    float thumbW = 16.0f;
    float thumbH = (float)height * 0.85f;
    auto thumbRect = juce::Rectangle<float>(sliderPos - thumbW * 0.5f, (float)y + ((float)height - thumbH) * 0.5f, thumbW, thumbH);

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(thumbRect.translated(1.0f, 2.0f), 2.0f);

    juce::ColourGradient thumbGrad(juce::Colour::fromRGB(85, 85, 85), thumbRect.getX(), thumbRect.getY(),
                                   juce::Colour::fromRGB(25, 25, 25), thumbRect.getX(), thumbRect.getBottom(), false);
    g.setGradientFill(thumbGrad);
    g.fillRoundedRectangle(thumbRect, 2.0f);

    g.setColour(juce::Colour::fromRGB(230, 140, 30));
    g.fillRect(thumbRect.getCentreX() - 1.0f, thumbRect.getY() + 3.0f, 2.0f, thumbRect.getHeight() - 6.0f);

    g.setColour(juce::Colour::fromRGB(20, 20, 20));
    g.drawRoundedRectangle(thumbRect, 2.0f, 1.0f);
}

void DomRadioLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> bounds(0.0f, 0.0f, (float)width, (float)height);

    // Используем цвета темы вместо черного хардкода
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Рисуем стильную винтажную треугольную стрелочку справа
    const float arrowW = 8.0f;
    const float arrowH = 5.0f;
    const float arrowX = (float)width - 15.0f;
    const float arrowY = ((float)height - arrowH) * 0.5f;

    juce::Path arrow;
    arrow.startNewSubPath(arrowX, arrowY);
    arrow.lineTo(arrowX + arrowW, arrowY);
    arrow.lineTo(arrowX + arrowW * 0.5f, arrowY + arrowH);
    arrow.closeSubPath();

    g.setColour(box.findColour(juce::ComboBox::textColourId).withAlpha(0.6f));
    g.fillPath(arrow);
}

void DomRadioLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    auto state = button.getToggleState();

    juce::ColourGradient grad(state ? juce::Colour::fromRGB(80, 80, 80) : juce::Colour::fromRGB(220, 215, 205), bounds.getX(), bounds.getY(),
                              state ? juce::Colour::fromRGB(40, 40, 40) : juce::Colour::fromRGB(160, 155, 145), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(juce::Colour::fromRGB(30, 30, 30));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    g.setColour(state ? juce::Colours::white : juce::Colour::fromRGB(40, 40, 40));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, false);
}
