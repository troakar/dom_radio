#pragma once
#include <JuceHeader.h>
#include "ToleranceModel.h"
#include <cmath>
#include <random>

namespace TroakarDSP
{

inline float fast_tanh(float x) {
    float x2 = x * x;
    float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return a / b;
}

class FastRandom
    {
    public:
        explicit FastRandom(uint32_t seed = 1) : state(seed == 0 ? 1 : seed) {}

        inline uint32_t nextInt() noexcept
        {
            uint32_t value = state;
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
            state = value;
            return value;
        }

        inline float nextFloat() noexcept
        {
            return (nextInt() & 0x7FFFFFFFu) / static_cast<float>(0x7FFFFFFFu);
        }

        void setSeed(uint32_t newSeed) noexcept { state = (newSeed == 0 ? 1 : newSeed); }

    private:
        uint32_t state;
    };

    inline float randomFloat(FastRandom& rng, float minimum, float maximum)
    {
        return minimum + (maximum - minimum) * rng.nextFloat();
    }

// Легковесный биквад без heap-аллокаций в аудио-потоке (RBJ Cookbook, TDF-II).
// Используется вместо juce::dsp::IIR::Filter там, где коэффициенты часто
// пересчитываются под управлением SmoothedValue.
class FastBiquad
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void reset()
    {
        v1 = 0.0;
        v2 = 0.0;
    }

    void setHighShelf(double freq, double Q, double gainLinear)
    {
        const double A     = std::sqrt(juce::jmax(0.0001, gainLinear));
        const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cosw  = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, Q));
        const double sqrtA2alpha = 2.0 * std::sqrt(A) * alpha;

        const double a0     = (A + 1.0) - (A - 1.0) * cosw + sqrtA2alpha;
        const double a0_inv = 1.0 / juce::jmax(1.0e-12, a0);

        b0 =  A * ((A + 1.0) + (A - 1.0) * cosw + sqrtA2alpha) * a0_inv;
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw)        * a0_inv;
        b2 =  A * ((A + 1.0) + (A - 1.0) * cosw - sqrtA2alpha) * a0_inv;
        a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cosw)            * a0_inv;
        a2 = ((A + 1.0) - (A - 1.0) * cosw - sqrtA2alpha)      * a0_inv;
    }

    void setLowShelf(double freq, double Q, double gainLinear)
    {
        const double A     = std::sqrt(juce::jmax(0.0001, gainLinear));
        const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cosw  = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, Q));
        const double sqrtA2alpha = 2.0 * std::sqrt(A) * alpha;

        const double a0     = (A + 1.0) + (A - 1.0) * cosw + sqrtA2alpha;
        const double a0_inv = 1.0 / juce::jmax(1.0e-12, a0);

        b0 =  A * ((A + 1.0) - (A - 1.0) * cosw + sqrtA2alpha) * a0_inv;
        b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw)        * a0_inv;
        b2 =  A * ((A + 1.0) - (A - 1.0) * cosw - sqrtA2alpha) * a0_inv;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw)            * a0_inv;
        a2 = ((A + 1.0) + (A - 1.0) * cosw - sqrtA2alpha)      * a0_inv;
    }

    void setLowPass(double freq, double Q)
    {
        const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cosw  = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, Q));

        const double a0     = 1.0 + alpha;
        const double a0_inv = 1.0 / juce::jmax(1.0e-12, a0);

        b1 = (1.0 - cosw) * a0_inv;
        b0 = b1 * 0.5;
        b2 = b0;
        a1 = -2.0 * cosw   * a0_inv;
        a2 = (1.0 - alpha) * a0_inv;
    }

    void setAllPass(double freq, double Q)
    {
        const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cosw  = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, Q));

        const double a0     = 1.0 + alpha;
        const double a0_inv = 1.0 / juce::jmax(1.0e-12, a0);

        b0 = (1.0 - alpha) * a0_inv;
        b1 = -2.0 * cosw   * a0_inv;
        b2 = (1.0 + alpha) * a0_inv;
        a1 = b1;
        a2 = b0;
    }

    void setPeakFilter(double freq, double Q, double gainLinear)
    {
        const double A     = std::sqrt(juce::jmax(0.0001, gainLinear));
        const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cosw  = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, Q));

        const double a0     = 1.0 + alpha / A;
        const double a0_inv = 1.0 / juce::jmax(1.0e-12, a0);

        b0 = (1.0 + alpha * A) * a0_inv;
        b1 = (-2.0 * cosw)      * a0_inv;
        b2 = (1.0 - alpha * A) * a0_inv;
        a1 = (-2.0 * cosw)      * a0_inv;
        a2 = (1.0 - alpha / A) * a0_inv;
    }

    forcedinline float processSample(float in) noexcept
    {
        const double din = static_cast<double>(in);
        const double out = b0 * din + v1;
        v1 = b1 * din - a1 * out + v2;
        v2 = b2 * din - a2 * out;

        constexpr double threshold = 1.0e-20;
        if (std::abs(v1) < threshold) v1 = 0.0;
        if (std::abs(v2) < threshold) v2 = 0.0;

        return static_cast<float>(out);
    }

    double getMagnitudeForFrequency(double freq) const noexcept
    {
        const double w     = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cosw  = std::cos(w);
        const double cos2w = std::cos(2.0 * w);
        const double num = b0 * b0 + b1 * b1 + b2 * b2
                         + 2.0 * (b0 * b1 + b1 * b2) * cosw
                         + 2.0 * b0 * b2 * cos2w;
        const double den = 1.0 + a1 * a1 + a2 * a2
                         + 2.0 * (a1 + a1 * a2) * cosw
                         + 2.0 * a2 * cos2w;
        return std::sqrt(juce::jmax(0.0, num) / juce::jmax(1.0e-12, den));
    }

private:
    double sr = 44100.0;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double v1 = 0.0, v2 = 0.0;
};

class TapesDSP
{
public:
    enum class Model { SvemaA4409 = 0, Orwo106, Scotch2500, BasfSPR50 };
    static constexpr float saturationRangeReduction = 0.70f;
    static constexpr float harmonicDistortionTrim = 0.75f; // -25% total harmonic distortion

    struct TapeProfile
    {
        const char* name = "SVEMA A4409";
        float coercivity = 0.478f;
        float reversibility = 0.170f;
        float saturation = 1.00f;
        float balancedSaturation = 1.00f;
        float preEmphasisFreq = 16000.0f;
        float preEmphasisGainDb = 14.0f;
        float gapLossBaseFreq = 16000.0f;
        float dynamicDecayRate = 0.8f;
        float headBumpFreq = 65.0f;
        float headBumpGainDb = 1.5f;
        float lowMidDrive = 1.0f;
        float highFrequencyCompression = 1.0f;
        float highCutAtFullLevel = 14000.0f;
        float glueAmount = 0.0f;
        float oddHarmonics = 0.0f;
        float evenHarmonics = 0.0f;
        float snrDb = -63.0f;

        float midContourFreq = 320.0f;
        float midContourGainDb = -1.5f;
        float midContourQ = 0.85f;
        float grainTextureNoise = 0.035f;
    };

    struct FormatSpecs
    {
        float speedCmS = 38.1f;
        float gapLossFreqHz = 18000.0f;
        float wowBase = 0.035f;
        float flutterBase = 0.035f;
        float headroomDb = 12.0f;
    };

    static FormatSpecs interpolateFormats(const FormatSpecs& a, const FormatSpecs& b, float t) noexcept
    {
        return {
            a.speedCmS + (b.speedCmS - a.speedCmS) * t,
            a.gapLossFreqHz + (b.gapLossFreqHz - a.gapLossFreqHz) * t,
            a.wowBase + (b.wowBase - a.wowBase) * t,
            a.flutterBase + (b.flutterBase - a.flutterBase) * t,
            a.headroomDb + (b.headroomDb - a.headroomDb) * t
        };
    }

    static FormatSpecs getInterpolatedFormat(float formatKnobNorm) noexcept
    {
        const FormatSpecs f10_5 { 38.1f,  18000.0f, 0.035f, 0.035f, 12.0f };
        const FormatSpecs f7    { 19.05f, 16000.0f, 0.065f, 0.065f,  6.0f };
        const FormatSpecs f3    { 9.53f,  12500.0f, 0.095f, 0.095f,  2.0f };
        const FormatSpecs fCas  { 4.75f,   5500.0f, 0.220f, 0.220f, -3.0f };

        if (formatKnobNorm <= 0.333f)
            return interpolateFormats(f10_5, f7, formatKnobNorm / 0.333f);
        else if (formatKnobNorm <= 0.666f)
            return interpolateFormats(f7, f3, (formatKnobNorm - 0.333f) / 0.333f);
        else
            return interpolateFormats(f3, fCas, (formatKnobNorm - 0.666f) / 0.334f);
    }

    static constexpr float minTapeSpeedIps = 1.875f;
    static constexpr float maxTapeSpeedIps = 15.0f;

    static float ipsToCmS(float ips) noexcept
    {
        return ips * 2.54f;
    }

    static float cmSToIps(float cmS) noexcept
    {
        return cmS / 2.54f;
    }

    static float speedIpsToNorm(float speedIps) noexcept
    {
        speedIps = juce::jlimit(minTapeSpeedIps, maxTapeSpeedIps, speedIps);
        return std::log2(speedIps / minTapeSpeedIps)
             / std::log2(maxTapeSpeedIps / minTapeSpeedIps);
    }

    static float speedNormToIps(float speedNorm) noexcept
    {
        speedNorm = juce::jlimit(0.0f, 1.0f, speedNorm);
        return minTapeSpeedIps * std::pow(maxTapeSpeedIps / minTapeSpeedIps, speedNorm);
    }

    static FormatSpecs getFormatForSpeedIps(float speedIps) noexcept
    {
        speedIps = juce::jlimit(minTapeSpeedIps, maxTapeSpeedIps, speedIps);

        const FormatSpecs f15    { 38.1f,  18000.0f, 0.035f, 0.035f, 12.0f };
        const FormatSpecs f7_5   { 19.05f, 16000.0f, 0.065f, 0.065f,  6.0f };
        const FormatSpecs f3_75  { 9.53f,  12500.0f, 0.095f, 0.095f,  2.0f };
        const FormatSpecs f1_875 { 4.76f,   5500.0f, 0.220f, 0.220f, -3.0f };

        if (speedIps >= 7.5f)
            return interpolateFormats(f7_5, f15, (speedIps - 7.5f) / 7.5f);

        if (speedIps >= 3.75f)
            return interpolateFormats(f3_75, f7_5, (speedIps - 3.75f) / 3.75f);

        return interpolateFormats(f1_875, f3_75, (speedIps - 1.875f) / 1.875f);
    }

    static float getBalancedSaturationThreshold(float originalThreshold) noexcept
    {
        return 1.0f + (originalThreshold - 1.0f) * (1.0f - saturationRangeReduction);
    }

    static TapeProfile getProfile(Model model) noexcept
    {
        switch (model)
        {
            case Model::SvemaA4409:
                return { "SVEMA A4409", 0.478f, 0.170f, 1.00f, 1.00f, 15000.0f, 13.5f, 11500.0f, 1.2f, 60.0f, 2.2f, 1.35f, 1.25f, 9500.0f, 0.15f, 0.065f, 0.045f, -61.0f, 320.0f, -1.6f, 0.85f, 0.035f };
            case Model::Orwo106:
                return { "ORWO TYP 106", 0.620f, 0.280f, 0.70f, 1.00f, 12000.0f, 10.0f, 11000.0f, 1.8f, 75.0f, 2.6f, 1.10f, 1.45f, 8500.0f, 0.22f, 0.045f, 0.085f, -59.0f, 180.0f, 1.8f, 0.75f, 0.020f };
            case Model::Scotch2500:
                return { "SCOTCH 2500 HAEG", 0.220f, 0.090f, 1.80f, 1.00f, 18000.0f, 6.0f, 19500.0f, 0.2f, 42.0f, 0.8f, 1.10f, 0.15f, 17500.0f, 0.10f, 0.085f, 0.010f, -67.0f, 450.0f, -0.6f, 0.90f, 0.008f };
            case Model::BasfSPR50:
                return { "BASF SPR 50 LHL", 0.180f, 0.050f, 2.20f, 1.00f, 19000.0f, 3.5f, 22000.0f, 0.0f, 32.0f, 0.3f, 0.65f, 0.05f, 21000.0f, 0.30f, 0.025f, 0.005f, -71.0f, 1000.0f, 0.0f, 0.70f, 0.002f };
        }
        return getProfile(Model::SvemaA4409);
    }

    static TapeProfile getBalancedProfile(Model model) noexcept
    {
        auto profile = getProfile(model);
        profile.balancedSaturation = getBalancedSaturationThreshold(profile.saturation);
        return profile;
    }

    static TapeProfile getBalancedProfile(int model) noexcept
    {
        return getBalancedProfile(static_cast<Model>(juce::jlimit(0, 3, model)));
    }

    static float getProfileSaturation(const TapeProfile& profile, float drive) noexcept
    {
        constexpr float driveMin = 1.0f;
        constexpr float driveMax = 10.0f;
        const float normalizedDrive = juce::jlimit(0.0f, 1.0f,
            (drive - driveMin) / (driveMax - driveMin));
        return juce::jmax(0.05f, normalizedDrive * profile.balancedSaturation);
    }
};

class TapeProfileProcessor
{
public:
    void prepare(double sampleRate, int maximumBlockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(maximumBlockSize);
        spec.numChannels = 1;
        lowDetector.prepare(spec);
        highDetector.prepare(spec);
        dynamicHighShelf.prepare(sr);
        *lowDetector.coefficients = *juce::dsp::IIR::Coefficients<double>::makeLowPass(sr, 180.0, 0.707);
        *highDetector.coefficients = *juce::dsp::IIR::Coefficients<double>::makeHighPass(sr, 2500.0, 0.707);
        reset();
        applyDynamicHighShelf(0.0f, 0.0f);
    }

    void reset()
    {
        lowDetector.reset();
        highDetector.reset();
        dynamicHighShelf.reset();
        fullEnvelope = 0.0f;
        lowEnvelope = 0.0f;
        highEnvelope = 0.0f;
        sampleCounter = 0;
        currentHighLossDb = 0.0f;
        currentHighCutHz = 20000.0f;
        lastAppliedHighLossDb = -999.0f;
        lastAppliedHighCutHz = -999.0f;
    }

    void updateProfile(const TapesDSP::TapeProfile& newProfile)
    {
        profile = newProfile;
        *lowDetector.coefficients = *juce::dsp::IIR::Coefficients<double>::makeLowPass(sr, 180.0, 0.707);
        *highDetector.coefficients = *juce::dsp::IIR::Coefficients<double>::makeHighPass(sr, 2500.0, 0.707);
        currentHighLossDb = 0.0f;
        currentHighCutHz = profile.gapLossBaseFreq;
        lastAppliedHighLossDb = -999.0f;
        lastAppliedHighCutHz = -999.0f;
        applyDynamicHighShelf(0.0f, 0.0f);
    }

    float getCurrentHighLossDb() const noexcept { return currentHighLossDb; }
    float getCurrentHighCutHz() const noexcept { return currentHighCutHz; }

    forcedinline float process(float input, float tapeDriveNorm, float tapePressure, float tapeBias, float speedNorm = 1.0f, float mixAmount = 1.0f)
    {
        const float formatSensitivity = 1.0f + (1.0f - speedNorm) * 2.5f;

        const float lowLevel = static_cast<float>(std::abs(lowDetector.processSample(static_cast<double>(input))));
        const float highLevel = static_cast<float>(std::abs(highDetector.processSample(static_cast<double>(input))));
        const float fullLevel = std::abs(input);
        
        fullEnvelope += (fullLevel * formatSensitivity > fullEnvelope ? attackCoeff : releaseCoeff) * (fullLevel * formatSensitivity - fullEnvelope);
        lowEnvelope += (lowLevel * formatSensitivity > lowEnvelope ? attackCoeff : releaseCoeff) * (lowLevel * formatSensitivity - lowEnvelope);
        highEnvelope += (highLevel * formatSensitivity > highEnvelope ? attackCoeff : releaseCoeff) * (highLevel * formatSensitivity - highEnvelope);

        const float tapeNorm = juce::jlimit(0.0f, 1.0f, tapeDriveNorm * mixAmount);
        const float pressure = juce::jlimit(0.65f, 2.20f, 1.0f + (tapePressure - 1.0f) * mixAmount);
        const float tapeLoad = juce::jlimit(0.0f, 1.0f, tapeNorm * pressure);
        const float normalizedFullLevel = juce::jlimit(0.0f, 1.0f, fullEnvelope * 2.0f);
        const float normalizedLowLevel = juce::jlimit(0.0f, 1.0f, lowEnvelope * 2.0f);
        const float normalizedHighLevel = juce::jlimit(0.0f, 1.0f, highEnvelope * 2.0f);

        if (++sampleCounter >= 32)
        {
            sampleCounter = 0;
            applyDynamicHighShelf(normalizedHighLevel, tapeLoad, speedNorm, mixAmount);
        }

        const float overBias = juce::jmax(0.0f, tapeBias);

        const float profileDrive = 1.0f
            + tapeNorm * (0.15f + profile.highFrequencyCompression * 0.10f)
              * pressure
            + overBias * 0.020f;

        const float saturated = fast_tanh(input * profileDrive);

        const float saturationBlend = juce::jlimit(0.0f, 0.45f,
            tapeNorm * (0.22f + pressure * 0.28f) * TapesDSP::harmonicDistortionTrim);

        float output = input + (saturated - input) * saturationBlend;

        const float lowLoad = normalizedLowLevel * profile.lowMidDrive * tapeLoad;
        const float glueLoad = normalizedFullLevel * profile.glueAmount * tapeLoad;

        const float loadReduction = 1.0f / (1.0f + lowLoad * 0.025f + glueLoad * 0.060f);

        const float loadMakeup = 1.0f + tapeLoad * 0.085f;

        output *= loadReduction * loadMakeup;

        const float x = output;
        const float x2 = x * x;

        output += profile.oddHarmonics * x2 * x * tapeLoad * 0.55f * TapesDSP::harmonicDistortionTrim;
        output += profile.evenHarmonics * x2 * tapeLoad * 0.45f * TapesDSP::harmonicDistortionTrim * (x >= 0.0f ? 1.0f : -1.0f);

        if (profile.grainTextureNoise > 0.0001f && tapeLoad > 0.01f)
        {
            const float noiseRaw = rng.nextFloat() * 2.0f - 1.0f;
            output += output * noiseRaw * profile.grainTextureNoise * tapeLoad * 0.06f;
        }

        return dynamicHighShelf.processSample(output);
    }

private:
    void applyDynamicHighShelf(float normalizedHighLevel, float tapeLoad, float speedNorm = 1.0f, float mixAmount = 1.0f)
    {
        normalizedHighLevel = juce::jlimit(0.0f, 1.0f, normalizedHighLevel);
        tapeLoad = juce::jlimit(0.0f, 1.0f, tapeLoad);
        const float decayAmount = juce::jlimit(0.0f, 1.0f,
            normalizedHighLevel * profile.dynamicDecayRate * (0.45f + tapeLoad * 0.55f));
        
        // Масштабируем срез ВЧ через ручку MIX
        const float targetLossDb = decayAmount * profile.highFrequencyCompression * 4.0f * mixAmount;
        
        const float speedIps = TapesDSP::speedNormToIps(speedNorm);
        const float speedScale = juce::jlimit(0.125f, 1.0f, speedIps / 15.0f);

        const float speedFreqScale = juce::jlimit(0.22f, 1.0f, std::pow(speedScale, 0.78f));
        const float baseFrequency = juce::jmax(2500.0f, profile.gapLossBaseFreq * speedFreqScale);
        const float fullLevelFrequency = juce::jmax(2500.0f, profile.highCutAtFullLevel * speedFreqScale);
        const float targetCutHz = baseFrequency - decayAmount * (baseFrequency - fullLevelFrequency);
        
        // При MIX=0% частота среза улетает в безопасную зону (22 кГц)
        const float effectiveCutHz = targetCutHz + (22050.0f - targetCutHz) * (1.0f - mixAmount);

        currentHighLossDb += 0.04f * (targetLossDb - currentHighLossDb);
        currentHighCutHz += 0.04f * (effectiveCutHz - currentHighCutHz);

        const float safeCutHz = juce::jlimit(1000.0f,
            static_cast<float>(sr * 0.45), currentHighCutHz);
        const float safeLossDb = juce::jmax(0.0f, currentHighLossDb);

        // Сглаженное применение коэффициентов (убирает зиппер-шум при скачках DECAY)
        const float smoothingFactor = 0.7f; // 70% старое + 30% новое
        float smoothedLossDb = lastAppliedHighLossDb * smoothingFactor
                             + safeLossDb * (1.0f - smoothingFactor);
        float smoothedCutHz = lastAppliedHighCutHz * smoothingFactor
                            + safeCutHz * (1.0f - smoothingFactor);
        if (lastAppliedHighLossDb <= -900.0f)
        {
            smoothedLossDb = safeLossDb;
            smoothedCutHz = safeCutHz;
        }

        dynamicHighShelf.setHighShelf(smoothedCutHz, 0.707f,
                                      juce::Decibels::decibelsToGain(-smoothedLossDb));

        lastAppliedHighLossDb = smoothedLossDb;
        lastAppliedHighCutHz = smoothedCutHz;
    }

    double sr = 44100.0;
    TapesDSP::TapeProfile profile = TapesDSP::getBalancedProfile(TapesDSP::Model::SvemaA4409);
    float fullEnvelope = 0.0f;
    float lowEnvelope = 0.0f;
    float highEnvelope = 0.0f;
    float currentHighLossDb = 0.0f;
    float currentHighCutHz = 20000.0f;
    float lastAppliedHighLossDb = -999.0f;
    float lastAppliedHighCutHz = -999.0f;
    int sampleCounter = 0;
    static constexpr float attackCoeff = 0.0025f;
    static constexpr float releaseCoeff = 0.00015f;
    juce::dsp::IIR::Filter<double> lowDetector;
    juce::dsp::IIR::Filter<double> highDetector;
    FastBiquad dynamicHighShelf;
    FastRandom rng { 777 };
};

} // namespace TroakarDSP
