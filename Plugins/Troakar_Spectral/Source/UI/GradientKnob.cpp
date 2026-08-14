#include "GradientKnob.h"


GradientKnob::GradientKnob(juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& paramID,
                           const juce::String& labelText,
                           bool allowInGradientMode,
                           bool isSmallKnob)
    : allowGradientMode(allowInGradientMode), isSmall(isSmallKnob)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, isSmall ? 40 : 70, isSmall ? 12 : 16);
    slider.setPopupDisplayEnabled(true, true, nullptr);
    
    slider.setAlpha(0.0f);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(isSmall ? 9.0f : 11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, isSmall ? juce::Colour::fromRGB(150, 145, 130) : juce::Colour::fromRGB(190, 185, 170));
    addAndMakeVisible(label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, slider);
}

void GradientKnob::setGradientActive(bool active, juce::Colour capColor)
{
    if (!allowGradientMode)
    {
        setLocked(active);
        return;
    }

    isGradientSelected = active;
    activeCapColor = active ? capColor : juce::Colour::fromRGB(180, 175, 160);
    repaint();
}

void GradientKnob::setLocked(bool locked)
{
    isLockedState = locked;
    slider.setEnabled(!locked);
    label.setAlpha(locked ? 0.3f : 1.0f);
    repaint();
}

void GradientKnob::setGradientMarkers(const std::vector<GradientMarker>& markers)
{
    gradientMarkers = markers;
    repaint();
}

void GradientKnob::resized()
{
    auto bounds = getLocalBounds();
    label.setBounds(bounds.removeFromTop(isSmall ? 12 : 16));
    bounds.removeFromTop(2);
    slider.setBounds(bounds);
}

void GradientKnob::paint(juce::Graphics& g)
{
    auto sliderBounds = slider.getBounds().toFloat();
    if (sliderBounds.isEmpty()) return;

    float fullDiameter = juce::jmin(sliderBounds.getWidth(), sliderBounds.getHeight() - (isSmall ? 14.0f : 18.0f));
    float knobDiameter = fullDiameter * (isSmall ? 0.85f : 0.75f); 
    
    auto knobArea = juce::Rectangle<float>(0, 0, knobDiameter, knobDiameter)
                        .withCentre(sliderBounds.getCentre().withY(sliderBounds.getY() + fullDiameter * 0.5f));

    float rotaryStartAngle = juce::MathConstants<float>::pi * 1.2f;
    float rotaryEndAngle   = juce::MathConstants<float>::pi * 2.8f;
    
    float propVal = (float)slider.valueToProportionOfLength(slider.getValue());
    float currentAngle = rotaryStartAngle + propVal * (rotaryEndAngle - rotaryStartAngle);

    drawOuterRing(g, juce::Rectangle<float>(0, 0, fullDiameter, fullDiameter).withCentre(knobArea.getCentre()), rotaryStartAngle, rotaryEndAngle);
    drawKnobCap(g, knobArea, currentAngle);

    if (isLockedState)
    {
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(knobArea);
    }

    if (isLinkedState)
    {
        auto linkBadge = juce::Rectangle<float>(sliderBounds.getRight() - 16.0f, sliderBounds.getY() - 2.0f, 14.0f, 14.0f);
        g.setColour(juce::Colour::fromRGB(100, 200, 255).withAlpha(0.85f));
        g.fillEllipse(linkBadge);
        g.setColour(juce::Colours::black);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("L", linkBadge.toNearestInt(), juce::Justification::centred);
    }
}

void GradientKnob::drawOuterRing(juce::Graphics& g, juce::Rectangle<float> bounds, float startAngle, float endAngle)
{
    auto center = bounds.getCentre();
    float maxRadius = bounds.getWidth() * 0.5f;
    float knobRadius = maxRadius * (isSmall ? 0.85f : 0.75f);

    juce::Path baseTrack;
    baseTrack.addCentredArc(center.x, center.y, knobRadius, knobRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour::fromRGB(40, 36, 30));
    g.strokePath(baseTrack, juce::PathStrokeType(isSmall ? 1.5f : 2.5f));

    if (isGradientSelected && allowGradientMode)
    {
        float propVal = (float)slider.valueToProportionOfLength(slider.getValue());
        float currentAngle = startAngle + propVal * (endAngle - startAngle);
        juce::Path activeArc;
        activeArc.addCentredArc(center.x, center.y, knobRadius, knobRadius, 0.0f, startAngle, currentAngle, true);

        g.setColour(activeCapColor.withAlpha(0.85f));
        g.strokePath(activeArc, juce::PathStrokeType(isSmall ? 2.0f : 3.0f));
    }
    else if (!isLockedState && !isSmall)
    {
        float ringSpacing = (maxRadius - knobRadius) / 4.0f;

        for (const auto& marker : gradientMarkers)
        {
            float currentRadius = knobRadius + 3.0f + marker.id * ringSpacing;
            
            juce::Path orbit;
            orbit.addCentredArc(center.x, center.y, currentRadius, currentRadius, 0.0f, startAngle, endAngle, true);
            g.setColour(juce::Colour::fromRGB(40, 36, 30).withAlpha(0.3f));
            g.strokePath(orbit, juce::PathStrokeType(1.0f));

            float angle = startAngle + marker.normalizedValue * (endAngle - startAngle);
            float arcSpan = 0.08f; 
            
            juce::Path markerArc;
            markerArc.addCentredArc(center.x, center.y, currentRadius, currentRadius, 0.0f, 
                                    angle - arcSpan, angle + arcSpan, true);

            g.setColour(marker.color.withAlpha(0.9f));
            g.strokePath(markerArc, juce::PathStrokeType(2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }
}

void GradientKnob::drawKnobCap(juce::Graphics& g, juce::Rectangle<float> bounds, float angle)
{
    auto center = bounds.getCentre();
    float radius = bounds.getWidth() * 0.5f;

    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillEllipse(bounds.translated(0.0f, 2.5f));

    juce::Colour baseColor = isGradientSelected ? activeCapColor 
                           : (isSmall ? juce::Colour::fromRGB(55, 50, 45) : juce::Colour::fromRGB(80, 75, 68));

    juce::ColourGradient capGrad(
        baseColor.brighter(0.2f), center.x, bounds.getY(),
        baseColor.darker(0.6f),   center.x, bounds.getBottom(), false);

    g.setGradientFill(capGrad);
    g.fillEllipse(bounds);

    g.setColour(baseColor.brighter(0.4f).withAlpha(0.5f));
    g.drawEllipse(bounds, 1.2f);

    juce::Path pointer;
    float pointerLength = radius * 0.75f;
    pointer.addRoundedRectangle(-1.25f, -radius + (isSmall ? 2.0f : 3.0f), 2.5f, pointerLength, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(center.x, center.y));

    juce::Colour pointerColor = isGradientSelected ? juce::Colours::white 
                              : (isSmall ? juce::Colours::white.withAlpha(0.7f) : juce::Colour::fromRGB(255, 176, 40));
    g.setColour(pointerColor);
    g.fillPath(pointer);
}
