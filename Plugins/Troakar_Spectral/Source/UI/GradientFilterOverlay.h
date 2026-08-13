#pragma once
#include <JuceHeader.h>
#include "GradientBandModel.h"

class GradientFilterOverlay : public juce::Component
{
public:
    GradientFilterOverlay(GradientBandManager& manager)
        : bandManager(manager)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onSelectionChanged;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colour::fromRGB(18, 15, 12));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour::fromRGB(45, 40, 32));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        int numBands = (int)bandManager.bands.size();
        float buttonWidth = (bounds.getWidth() - 10.0f) / (numBands + 1);
        float h = bounds.getHeight() - 6.0f;

        auto globalRect = juce::Rectangle<float>(5.0f, 3.0f, buttonWidth - 4.0f, h);
        bool isGlobalSelected = !bandManager.isBandSelected();
        drawBadge(g, globalRect, "GLOBAL", juce::Colour::fromRGB(180, 175, 160), isGlobalSelected);

        for (int i = 0; i < numBands; ++i)
        {
            auto& band = bandManager.bands[(size_t)i];
            auto badgeRect = juce::Rectangle<float>(5.0f + (i + 1) * buttonWidth, 3.0f, buttonWidth - 4.0f, h);
            bool isSelected = (bandManager.getSelectedBandIndex() == i);
            drawBadge(g, badgeRect, band.name, band.color, isSelected);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto bounds = getLocalBounds().toFloat();
        int numBands = (int)bandManager.bands.size();
        float buttonWidth = (bounds.getWidth() - 10.0f) / (numBands + 1);

        float x = e.position.x - 5.0f;
        int clickedIndex = static_cast<int>(x / buttonWidth) - 1;

        if (clickedIndex < 0 || clickedIndex >= numBands)
            bandManager.clearSelection();
        else
            bandManager.setSelectedBandIndex(clickedIndex);

        repaint();
        if (onSelectionChanged) onSelectionChanged();
    }

private:
    void drawBadge(juce::Graphics& g, juce::Rectangle<float> rect, const juce::String& text, juce::Colour color, bool isSelected)
    {
        g.setColour(isSelected ? color.withAlpha(0.25f) : juce::Colour::fromRGB(28, 24, 20));
        g.fillRoundedRectangle(rect, 3.0f);

        g.setColour(isSelected ? color : color.withAlpha(0.4f));
        g.drawRoundedRectangle(rect, 3.0f, isSelected ? 1.8f : 1.0f);

        if (isSelected)
        {
            g.setColour(color.withAlpha(0.15f));
            g.fillRoundedRectangle(rect.expanded(2.0f), 4.0f);
        }

        g.setColour(color);
        g.fillEllipse(rect.getX() + 6.0f, rect.getCentreY() - 3.0f, 6.0f, 6.0f);

        g.setColour(isSelected ? juce::Colours::white : juce::Colour::fromRGB(160, 155, 140));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(text, rect.withTrimmedLeft(16.0f), juce::Justification::centredLeft, true);
    }

    GradientBandManager& bandManager;
};
