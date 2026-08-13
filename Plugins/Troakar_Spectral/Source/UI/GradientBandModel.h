#pragma once
#include <JuceHeader.h>
#include <vector>

struct GradientPoint
{
    int id = 0;
    juce::String name;
    juce::Colour color;
    bool active = true;      // Включен ли градиент в DSP
    bool isSelected = false; // Выделен ли он сейчас в интерфейсе (UI)

    float centerFreqHz = 1000.0f;
    float centerGainDb = 0.0f;

    float radiusOctaves = 1.5f;
    float radiusDb = 12.0f;

    float amountPct = 100.0f;
    float upMaxDb = 4.0f;
    float downMaxDb = -4.0f;
    float speedPct = 50.0f;
    float smoothPct = 20.0f;
};

class GradientPointManager
{
public:
    GradientPointManager()
    {
        availableColors = {
            juce::Colour::fromRGB(230, 50, 50),
            juce::Colour::fromRGB(240, 140, 30),
            juce::Colour::fromRGB(40, 200, 100),
            juce::Colour::fromRGB(40, 180, 240),
            juce::Colour::fromRGB(200, 60, 220),
            juce::Colour::fromRGB(220, 200, 40),
            juce::Colour::fromRGB(60, 220, 200),
            juce::Colour::fromRGB(255, 100, 150)
        };
    }

    int addPoint(float freqHz, float gainDb)
    {
        GradientPoint point;
        point.id = nextId++;
        point.name = "G" + juce::String(point.id);
        point.color = availableColors[point.id % availableColors.size()];
        point.centerFreqHz = freqHz;
        point.centerGainDb = gainDb;
        point.active = true;       // По умолчанию работает со звуком
        point.isSelected = false;  // Но пока не выделен

        points.push_back(point);
        return point.id;
    }

    void removePoint(int id)
    {
        points.erase(std::remove_if(points.begin(), points.end(),
            [id](const GradientPoint& p) { return p.id == id; }), points.end());
    }

    GradientPoint* getPoint(int id)
    {
        for (auto& p : points)
            if (p.id == id) return &p;
        return nullptr;
    }

    GradientPoint* getActivePoint() {
        for (auto& p : points) if (p.isSelected) return &p;
        return nullptr;
    }

    void setActivePoint(int id) {
        for (auto& p : points) p.isSelected = (p.id == id);
    }

    void clearActive() {
        for (auto& p : points) p.isSelected = false;
    }

    bool hasActivePoint() const {
        for (const auto& p : points) if (p.isSelected) return true;
        return false;
    }

    float getWeightAt(int pointId, float freqHz, float gainDb) const
    {
        for (const auto& p : points)
        {
            if (p.id != pointId) continue;

            float logDist = std::log2(freqHz / p.centerFreqHz);
            float freqWeight = std::exp(-0.5f * (logDist / p.radiusOctaves) * (logDist / p.radiusOctaves));

            float gainDist = (gainDb - p.centerGainDb) / p.radiusDb;
            float gainWeight = std::exp(-0.5f * gainDist * gainDist);

            return freqWeight * gainWeight;
        }
        return 0.0f;
    }

    std::vector<GradientPoint> points;
    std::vector<juce::Colour> availableColors;

private:
    int nextId = 0;
};
