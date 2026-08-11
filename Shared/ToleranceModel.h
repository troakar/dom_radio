#pragma once
#include <JuceHeader.h>
#include <random>
#include <cmath>
#include <chrono>

namespace TroakarDSP
{

class ToleranceModel
{
public:
    struct ComponentTolerances
    {
        // INPUT STAGE
        float inputTransformerGain = 1.0f;      // +/-3%
        float apfResonanceShift = 1.0f;         // +/-5%
        float ironCoreHarmonics = 1.0f;         // +/-8%

        // DRIVE CORE
        float spiralBiasShift = 0.0f;           // +/-4%
        float transistorHfe = 1.0f;             // +/-15% (beta)
        float slewRateVariation = 1.0f;         // +/-10%

        // TAPE MEDIUM
        float tapeCoercivity = 1.0f;            // +/-6%
        float gapLossFrequency = 1.0f;          // +/-3%
        float headBumpQ = 1.0f;                 // +/-12%
        float headBumpFrequency = 1.0f;         // +/-2%

        // MECHANICS
        float wowFrequencyDrift = 1.0f;         // +/-2%
        float flutterFrequencyDrift = 1.0f;     // +/-5%
        float azimuthAmplitude = 1.0f;          // +/-20%

        // OUTPUT STAGE
        float clipperThreshold = 0.0f;          // +/-2 dB
        float outputTransformerLPF = 1.0f;      // +/-300 Hz (множитель)

        // STEREO MATCHING
        float channelImbalance = 0.0f;          // +/-0.5 dB L/R
        float crosstalkVariation = 1.0f;        // +/-10%
    };

    enum class UnitMode
    {
        Calibrated = 0,  // Идеальные номиналы (TMT OFF)
        Typical,         // Средний разброс (+/-50% от макс. допуска)
        Loose,           // Полный допуск (+/-100%)
        Vintage,         // Старение компонентов (+drift +asymmetry)
        Custom           // Пользовательский seed
    };

    ToleranceModel() : mode(UnitMode::Calibrated), seed(0), rng(0) {}

    void setMode(UnitMode newMode, uint32_t customSeed = 0)
    {
        mode = newMode;

        if (mode == UnitMode::Custom && customSeed != 0)
            seed = customSeed;
        else if (mode != UnitMode::Calibrated)
            seed = generateDeviceSeed();
        else
            seed = 0;

        rng.seed(seed);
        regenerate();
    }

    void regenerate()
    {
        if (mode == UnitMode::Calibrated)
        {
            tolerancesL = ComponentTolerances();
            tolerancesR = ComponentTolerances();
            return;
        }

        const float intensity = getIntensity();

        auto generateSide = [&](ComponentTolerances& t) {
            t.inputTransformerGain = randomVariation(0.03f, intensity);
            t.apfResonanceShift   = randomVariation(0.05f, intensity);
            t.ironCoreHarmonics    = randomVariation(0.08f, intensity);

            t.spiralBiasShift      = randomOffset(0.04f, intensity);
            t.transistorHfe        = randomVariation(0.15f, intensity);
            t.slewRateVariation    = randomVariation(0.10f, intensity);

            t.tapeCoercivity      = randomVariation(0.06f, intensity);
            t.gapLossFrequency    = randomVariation(0.03f, intensity);
            t.headBumpQ           = randomVariation(0.12f, intensity);
            t.headBumpFrequency   = randomVariation(0.02f, intensity);

            t.wowFrequencyDrift   = randomVariation(0.02f, intensity);
            t.flutterFrequencyDrift = randomVariation(0.05f, intensity);
            t.azimuthAmplitude    = randomVariation(0.20f, intensity);

            t.clipperThreshold    = randomOffset(2.0f, intensity);
            t.outputTransformerLPF = randomVariation(300.0f / 18000.0f, intensity);

            t.channelImbalance   = randomOffset(0.5f, intensity);
            t.crosstalkVariation = randomVariation(0.10f, intensity);

            if (mode == UnitMode::Vintage)
            {
                t.apfResonanceShift     *= 1.08f;
                t.gapLossFrequency      *= 1.05f;
                t.outputTransformerLPF  *= 1.12f;
                t.inputTransformerGain  *= (1.0f + std::abs(t.spiralBiasShift) * 0.1f);
                t.transistorHfe        *= 0.92f;
                t.azimuthAmplitude     *= 1.4f;
                t.wowFrequencyDrift    *= 1.03f;
                t.headBumpQ            *= 0.85f;
            }
        };

        generateSide(tolerancesL);
        generateSide(tolerancesR);
    }

    const ComponentTolerances& getTolerancesL() const noexcept { return tolerancesL; }
    const ComponentTolerances& getTolerancesR() const noexcept { return tolerancesR; }
    const ComponentTolerances& getTolerances() const noexcept { return tolerancesL; }

    uint32_t getSeed() const noexcept { return seed; }
    UnitMode getMode() const noexcept { return mode; }

    juce::String toStateString() const
    {
        return juce::String(static_cast<int>(mode)) + ":" + juce::String(static_cast<juce::int64>(seed));
    }

    void fromStateString(const juce::String& state)
    {
        auto tokens = juce::StringArray::fromTokens(state, ":", "");
        if (tokens.size() == 2)
        {
            const int modeInt = tokens[0].getIntValue();
            const uint32_t savedSeed = static_cast<uint32_t>(tokens[1].getLargeIntValue());
            setMode(static_cast<UnitMode>(modeInt), savedSeed);
        }
        else
            setMode(UnitMode::Calibrated);
    }

private:
    float getIntensity() const
    {
        switch (mode)
        {
            case UnitMode::Typical:  return 0.5f;
            case UnitMode::Loose:    return 1.0f;
            case UnitMode::Vintage:  return 0.8f;
            case UnitMode::Custom:   return 1.0f;
            default:                 return 0.0f;
        }
    }

    float randomVariation(float tolerance, float intensity)
    {
        if (tolerance <= 0.0f || intensity <= 0.0f) return 1.0f;
        std::normal_distribution<float> dist(1.0f, tolerance * intensity / 3.0f);
        return juce::jlimit(1.0f - tolerance * intensity, 1.0f + tolerance * intensity, dist(rng));
    }

    float randomOffset(float offset, float intensity)
    {
        if (offset <= 0.0f || intensity <= 0.0f) return 0.0f;
        std::uniform_real_distribution<float> dist(-offset * intensity, offset * intensity);
        return dist(rng);
    }

    uint32_t generateDeviceSeed() const
    {
        uint32_t hash = 0x5A7E1234u;
        try {
            auto addresses = juce::MACAddress::getAllAddresses();
            for (const auto& addr : addresses)
                for (int i = 0; i < 6; ++i)
                    hash = (hash << 5) - hash + static_cast<uint32_t>(addr.getBytes()[i]);
        } catch (...) {}
        try { std::random_device rd; hash ^= rd(); }
        catch (...) { hash ^= static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()); }
        if (hash == 0u) hash = 0x12345678u;
        return hash;
    }

    UnitMode mode = UnitMode::Calibrated;
    uint32_t seed = 0;
    ComponentTolerances tolerancesL;
    ComponentTolerances tolerancesR;
    std::mt19937 rng;
};

} // namespace TroakarDSP
