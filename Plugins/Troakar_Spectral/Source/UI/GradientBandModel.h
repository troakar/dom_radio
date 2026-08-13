#pragma once
#include <JuceHeader.h>
#include <vector>

struct GradientBand
{
    int id = 0;
    juce::String name;
    juce::Colour color;
    bool enabled = false;

    float centerFreqHz = 1000.0f;
    float bandwidthOctaves = 1.5f;

    float amountPct = 100.0f;
    float upMaxDb = 4.0f;
    float downMaxDb = -4.0f;
    float speedPct = 50.0f;
    float smoothPct = 20.0f;
};

class GradientBandManager
{
public:
    GradientBandManager()
    {
        bands = {
            { 0, "Low Sub",        juce::Colour::fromRGB(230, 50, 50),   true,   100.0f, 1.8f },
            { 1, "Low Mid",        juce::Colour::fromRGB(240, 140, 30),  true,   500.0f, 1.5f },
            { 2, "High Mid",       juce::Colour::fromRGB(40, 200, 100),  false, 2500.0f, 1.5f },
            { 3, "High Presence",  juce::Colour::fromRGB(40, 180, 240),  false, 8000.0f, 1.8f }
        };
    }

    int getSelectedBandIndex() const { return selectedBandIndex; }
    void setSelectedBandIndex(int index) { selectedBandIndex = index; }
    void clearSelection() { selectedBandIndex = -1; }
    bool isBandSelected() const { return selectedBandIndex >= 0 && selectedBandIndex < (int)bands.size(); }

    GradientBand* getSelectedBand()
    {
        if (isBandSelected())
            return &bands[(size_t)selectedBandIndex];
        return nullptr;
    }

    std::vector<GradientBand> bands;

private:
    int selectedBandIndex = -1;
};
