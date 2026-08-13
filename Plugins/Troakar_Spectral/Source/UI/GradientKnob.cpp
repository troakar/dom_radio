#include "GradientKnob.h"

GradientKnob::GradientKnob(juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& paramID,
                           const juce::String& labelText,
                           bool allowInGradientMode)
    : allowGradientMode(allowInGradientMode)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setPopupDisplayEnabled(true, true, nullptr);
    
    // Отключаем стандартную отрисовку Slider, чтобы GradientKnob сам рисовал кастомный кноб
    slider.setAlpha(0.0f);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(190, 185, 170));
    addAndMakeVisible(label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, slider);
}

void GradientKnob::setGradientActive(bool active, juce::Colour capColor)
{
    if (!allowGradientMode)
    {
        setLocked(active); // Если ручка не подлежит градиенту (например MIX) - блокируем её
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
    label.setBounds(bounds.removeFromTop(16));
    bounds.removeFromTop(2);
    slider.setBounds(bounds);
}

void GradientKnob::paint(juce::Graphics& g)
{
    auto sliderBounds = slider.getBounds().toFloat();
    if (sliderBounds.isEmpty()) return;

    float diameter = juce::jmin(sliderBounds.getWidth(), sliderBounds.getHeight() - 18.0f);
    auto knobArea = juce::Rectangle<float>(0, 0, diameter, diameter)
                        .withCentre(sliderBounds.getCentre().withY(sliderBounds.getY() + diameter * 0.5f));

    float rotaryStartAngle = juce::MathConstants<float>::pi * 1.2f;
    float rotaryEndAngle   = juce::MathConstants<float>::pi * 2.8f;
    
    // Переведено на стандартный метод JUCE valueToProportionOfLength
    float propVal = (float)slider.valueToProportionOfLength(slider.getValue());
    float currentAngle = rotaryStartAngle + propVal * (rotaryEndAngle - rotaryStartAngle);

    // 1. Отрисовка Окантовки (Outer Ring & Markers)
    drawOuterRing(g, knobArea.expanded(5.0f), rotaryStartAngle, rotaryEndAngle);

    // 2. Отрисовка Крышечки (Knob Cap)
    drawKnobCap(g, knobArea, currentAngle);

    // 3. Отображение Блокировки (Затемнение и Замок при несовместимости)
    if (isLockedState)
    {
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(knobArea);

        g.setColour(juce::Colour::fromRGB(220, 80, 80).withAlpha(0.85f));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("LOCKED", knobArea.toNearestInt(), juce::Justification::centred);
    }
}

void GradientKnob::drawOuterRing(juce::Graphics& g, juce::Rectangle<float> bounds, float startAngle, float endAngle)
{
    float radius = bounds.getWidth() * 0.5f;
    auto center = bounds.getCentre();

    // Базовый фоновый трек
    juce::Path track;
    track.addCentredArc(center.x, center.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour::fromRGB(40, 36, 30));
    g.strokePath(track, juce::PathStrokeType(2.5f));

    if (isGradientSelected && allowGradientMode)
    {
        // РЕЖИМ 1: Выделен один градиент — подсвечиваем окантовку его цветом
        float propVal = (float)slider.valueToProportionOfLength(slider.getValue());
        float currentAngle = startAngle + propVal * (endAngle - startAngle);
        juce::Path activeArc;
        activeArc.addCentredArc(center.x, center.y, radius, radius, 0.0f, startAngle, currentAngle, true);

        g.setColour(activeCapColor.withAlpha(0.85f));
        g.strokePath(activeArc, juce::PathStrokeType(3.0f));
    }
    else if (!isLockedState)
    {
        // РЕЖИМ 2: Ничего не выделено — вывод окантовок/засечек всех активных градиентов
        for (const auto& marker : gradientMarkers)
        {
            float angle = startAngle + marker.normalizedValue * (endAngle - startAngle);
            float px = center.x + std::sin(angle) * radius;
            float py = center.y - std::cos(angle) * radius;

            // Точка-маркер соответствующего градиента на окантовке
            g.setColour(marker.color);
            g.fillEllipse(px - 2.5f, py - 2.5f, 5.0f, 5.0f);
            
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.drawEllipse(px - 2.5f, py - 2.5f, 5.0f, 5.0f, 0.8f);
        }
    }
}

void GradientKnob::drawKnobCap(juce::Graphics& g, juce::Rectangle<float> bounds, float angle)
{
    auto center = bounds.getCentre();
    float radius = bounds.getWidth() * 0.5f;

    // Тень
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillEllipse(bounds.translated(0.0f, 2.5f));

    // Корпус ручки (Градиент крышечки зависит от выделения)
    juce::Colour baseColor = isGradientSelected ? activeCapColor : juce::Colour::fromRGB(80, 75, 68);

    juce::ColourGradient capGrad(
        baseColor.brighter(0.2f), center.x, bounds.getY(),
        baseColor.darker(0.6f),   center.x, bounds.getBottom(), false);

    g.setGradientFill(capGrad);
    g.fillEllipse(bounds);

    // Металлическая или цветная фаска
    g.setColour(baseColor.brighter(0.4f).withAlpha(0.5f));
    g.drawEllipse(bounds, 1.2f);

    // Оранжевая или белая стрелка-указатель
    juce::Path pointer;
    float pointerLength = radius * 0.75f;
    pointer.addRoundedRectangle(-1.25f, -radius + 3.0f, 2.5f, pointerLength, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(center.x, center.y));

    g.setColour(isGradientSelected ? juce::Colours::white : juce::Colour::fromRGB(255, 176, 40));
    g.fillPath(pointer);
}