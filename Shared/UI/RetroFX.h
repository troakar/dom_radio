#pragma once
#include <JuceHeader.h>

namespace RetroFX
{
    inline void drawGlowPath(juce::Graphics& g, const juce::Path& p,
                             juce::Colour c, float coreThickness = 1.8f, int passes = 4)
    {
        for (int i = passes; i >= 1; --i)
        {
            const float t = coreThickness + (float)i * (float)i * 1.6f;
            const float a = 0.30f / (float)(i * i);
            g.setColour(c.withAlpha(a));
            g.strokePath(p, juce::PathStrokeType(t, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        }
        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(coreThickness, juce::PathStrokeType::curved));
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.strokePath(p, juce::PathStrokeType(coreThickness * 0.45f, juce::PathStrokeType::curved));
    }

    inline void drawGlowDot(juce::Graphics& g, juce::Point<float> pos,
                            juce::Colour c, float radius, float intensity = 1.0f)
    {
        juce::ColourGradient halo(c.withAlpha(0.55f * intensity), pos.x, pos.y,
                                  c.withAlpha(0.0f), pos.x + radius * 3.0f, pos.y, true);
        g.setGradientFill(halo);
        g.fillEllipse(pos.x - radius * 3.0f, pos.y - radius * 3.0f, radius * 6.0f, radius * 6.0f);
        g.setColour(c.withMultipliedBrightness(0.6f + 0.6f * intensity));
        g.fillEllipse(pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.6f * intensity));
        g.fillEllipse(pos.x - radius * 0.35f, pos.y - radius * 0.35f, radius * 0.7f, radius * 0.7f);
    }

    inline void drawScanlines(juce::Graphics& g, juce::Rectangle<float> area,
                              float alpha = 0.07f, float pitch = 3.0f)
    {
        g.setColour(juce::Colours::black.withAlpha(alpha));
        for (float y = area.getY(); y < area.getBottom(); y += pitch)
            g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
    }

    inline void drawVignette(juce::Graphics& g, juce::Rectangle<float> area, float strength = 0.35f)
    {
        juce::ColourGradient v(juce::Colours::transparentBlack,
                               area.getCentreX(), area.getCentreY(),
                               juce::Colours::black.withAlpha(strength),
                               area.getX(), area.getY(), true);
        g.setGradientFill(v);
        g.fillRect(area);
    }

    inline juce::Image createGrungeTexture(int w, int h, juce::int64 seed,
                                           float dustAmount = 0.004f)
    {
        juce::Image img(juce::Image::ARGB, w, h, true);
        juce::Random rng(seed);
        juce::Graphics g(img);

        for (int i = 0; i < (int)(w * h * dustAmount); ++i)
        {
            float a = rng.nextFloat() * 0.12f;
            g.setColour(juce::Colours::black.withAlpha(a));
            g.fillEllipse((float)rng.nextInt(w), (float)rng.nextInt(h),
                          1.0f + rng.nextFloat(), 1.0f + rng.nextFloat());
        }
        for (int i = 0; i < 6; ++i)
        {
            float x = (float)rng.nextInt(w), y = (float)rng.nextInt(h);
            float r = 8.0f + rng.nextFloat() * 22.0f;
            juce::ColourGradient spot(juce::Colour::fromRGB(120, 85, 40).withAlpha(0.06f),
                                      x, y, juce::Colours::transparentBlack, x + r, y, true);
            g.setGradientFill(spot);
            g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
        }
        for (int i = 0; i < 5; ++i)
        {
            float x1 = (float)rng.nextInt(w), y1 = (float)rng.nextInt(h);
            g.setColour(juce::Colours::white.withAlpha(0.05f + rng.nextFloat() * 0.05f));
            g.drawLine(x1, y1, x1 + (rng.nextFloat() - 0.5f) * 60.0f,
                       y1 + (rng.nextFloat() - 0.5f) * 60.0f, 0.6f);
        }
        return img;
    }

    inline void drawGlassHighlight(juce::Graphics& g, juce::Rectangle<float> area)
    {
        juce::Path glass;
        glass.addRoundedRectangle(area.getX() + 2.0f, area.getY() + 2.0f,
                                  area.getWidth() - 4.0f, area.getHeight() * 0.40f, 3.0f);
        juce::ColourGradient grad(juce::Colours::white.withAlpha(0.35f),
                                  area.getCentreX(), area.getY(),
                                  juce::Colours::white.withAlpha(0.0f),
                                  area.getCentreX(), area.getY() + area.getHeight() * 0.40f, false);
        g.setGradientFill(grad);
        g.fillPath(glass);
    }
}
