#pragma once
#include <JuceHeader.h>
#include "TapesDSP.h"
#include "ToleranceModel.h"
#include <cmath>
#include <cstring>
#include <cstdint>

namespace TroakarDSP
{

enum class NoiseMode { off, staticNoise, dynamicNoise };
enum class DriveType { silicon, germanium };

class InputTransformer
{
public:
    void prepare(double sampleRate, int maximumBlockSize = 65536)
    {
        sr = sampleRate;
        lowShelf.prepare(sampleRate);
        lowShelf.setLowShelf(40.0, 0.707, 1.0); // Изначально прозрачен
        lowPass.prepare(sampleRate);
        lowPass.setLowPass(120.0, 0.707);
        fluxCoeff = 1.0f - std::exp(-1.0f / (0.035f * static_cast<float>(sampleRate)));
        reset();
    }
    void reset()
    {
        lowShelf.reset();
        lowPass.reset();
        flux = 0.0f;
    }
    void updateParameters(float mixAmount)
    {
        // Вместо фазово-опасного HPF плавно поджимаем сверхнизкие частоты
        // При MIX = 0% фильтр дает 0 dB (абсолютно прозрачен)
        lowShelf.setLowShelf(40.0, 0.707, juce::Decibels::decibelsToGain(-6.0f * mixAmount));
    }
    void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept
    {
        tolerances = t;
    }

    forcedinline float process(float input, float ironAmount, float mixAmount = 1.0f)
    {
        // 1. Пропускаем через сглаживающий LowShelf
        float value = lowShelf.processSample(input);

        // 2. TMT: разброс усиления входного трансформатора (масштабируется от MIX)
        const float gainVariation = tolerances ? tolerances->inputTransformerGain : 1.0f;
        value *= (1.0f + (gainVariation - 1.0f) * mixAmount);

        const float amount = juce::jlimit(0.0f, 1.0f, ironAmount);
        if (amount <= 0.0001f)
            return input + (value - input) * mixAmount;

        // 3. Детектор магнитного потока и нелинейное насыщение железа
        const float low = lowPass.processSample(value);
        flux += fluxCoeff * (low - flux);

        const float drive = 1.0f + amount * 4.5f;
        const float x = juce::jlimit(-4.0f, 4.0f, flux * drive);
        const float x3 = x * x * x;
        const float x5 = x3 * x * x;

        const float harmVar = tolerances ? tolerances->ironCoreHarmonics : 1.0f;
        constexpr float hdTrim = TapesDSP::harmonicDistortionTrim;

        const float magnetised = fast_tanh(x - 0.08f * hdTrim * amount * x3 * harmVar
                                             + 0.015f * hdTrim * amount * x5 * harmVar);

        const float processed = value + (magnetised - low) * juce::jlimit(0.0f, 1.0f, amount * 1.5f * hdTrim);

        // 4. Smart Mix: плавный бленд с гарантией сохранения фазы
        return input + (processed - input) * mixAmount;
    }

private:
    double sr = 44100.0;
    float flux = 0.0f;
    float fluxCoeff = 0.0f;
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
    FastBiquad lowShelf;
    FastBiquad lowPass;
};

class PlatinumSlew {
public:
    void prepare(double sampleRate) {
        overallscale = sampleRate / 44100.0;
        std::memset(buf, 0, sizeof(buf));
        std::memset(runningSum, 0, sizeof(runningSum));
        second = 0.0;
        third = 0.0;
        sustain = 0.0;
        count = 0;
    }

    forcedinline float process(float input, float transient) {
        double dry = static_cast<double>(input);

        if (transient >= 0.99f) {
            third = second;
            second = dry;
            sustain *= 0.9;
            return input;
        }

        const double amount = 1.0 - static_cast<double>(transient);
        const double compresity = std::pow(amount * 0.95, 2.0) * 8.0;

        double diff1 = (dry - second) * overallscale;
        double diff2 = (second - third) * overallscale;

        sustain += std::abs(diff1 - diff2);
        sustain *= 0.9;

        // ИСПРАВЛЕНО: абсолютная оценка ВЧ вместо относительной (|diff1|/|dry|).
        // Относительная давала blow-up на тихом сигнале → цифровой "песок" ~-40 дБ.
        const double hfContent = std::min(1.0, std::abs(diff1) * 2.0);
        const double hfMultiplier = 1.0 + hfContent * 0.3;
        sustain *= hfMultiplier;

        double x = third;
        double depthAmt = std::min(std::sin(std::min(sustain,
                                                       juce::MathConstants<double>::halfPi))
                                   * compresity, 9.0);
        int depth = static_cast<int>(depthAmt);
        double trim = depthAmt - static_cast<double>(depth);

        for (int layer = 0; layer < depth; ++layer)
            x = lane(layer, x);

        double remainder = lane(depth, x);

        for (int layer = depth + 1; layer < 10; ++layer) {
            const int length = std::max(1, static_cast<int>(2.0 * (layer + 1.0) * overallscale));
            const int safeLength = std::min(length, 319);
            buf[count % safeLength][layer] = x;
        }

        if (++count > 619315200)
            count = 0;

        third = second;
        second = dry;
        return static_cast<float>(x * (1.0 - trim) + remainder * trim);
    }

private:
    forcedinline double lane(int layer, double input) {
        const int sl = std::min(319, std::max(1, static_cast<int>(2.0 * (layer + 1.0) * overallscale)));

        buf[count % 320][layer] = input;

        if ((count & 1023) == 0)
        {
            double sum = 0.0;
            for (int i = 0; i < sl; ++i)
                sum += buf[(count + 320 - i) % 320][layer];
            runningSum[layer] = sum;
        }
        else
        {
            const int readPos = (count + 320 - sl) % 320;
            runningSum[layer] += input - buf[readPos][layer];
        }

        return runningSum[layer] / static_cast<double>(sl);
    }

    double buf[320][10] = {};
    double runningSum[10] = {0.0};
    double second = 0.0, third = 0.0, sustain = 0.0;
    double overallscale = 1.0;
    int count = 0;
};

// --- Вспомогательная математика из ChowTape (Jiles-Atherton) ---
static inline float fast_coth(float x) noexcept
{
    const float x_sq = x * x;
    // Аппроксимация [7/6] Паде для гиперболического котангенса из ChowTape
    const float num = x * (135135.0f + x_sq * (17325.0f + x_sq * (378.0f + x_sq)));
    const float den = 135135.0f + x_sq * (62370.0f + x_sq * (3150.0f + x_sq * 28.0f));
    return den / (num + 1.0e-9f); // защита от деления на ноль
}

static inline float langevin(float x) noexcept
{
    if (std::abs(x) < 0.001f)
        return x / 3.0f;
    // Защита от переполнения при экстремальных полях (TAPE_DRIVE=100% + pressure=3.0)
    if (std::abs(x) > 15.0f)
        return x >= 0.0f ? 1.0f : -1.0f;
    return fast_coth(x) - (1.0f / x);
}
// ----------------------------------------------------------------

class MagneticTapeCore {
public:
    void prepare(double sampleRate) {
        juce::ignoreUnused(sampleRate);
        reset();
    }

    void reset() {
        prevInput = 0.0f;
        fluxState = 0.0f;
        previousOutput = 0.0f;
    }

    // TMT: подставить допуски (nullptr = Calibrated)
    void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept { tolerances = t; }

    forcedinline float process(float input, float tapeDriveNorm, float tapePressure,
                               float bias, const TapesDSP::TapeProfile& profile) {
        const float underBias = juce::jmax(0.0f, -bias);
        const float overBias = juce::jmax(0.0f, bias);
        const float driveNorm = juce::jlimit(0.0f, 1.0f, tapeDriveNorm);

        // 1. Формируем магнитное поле (H)
        const float tapeGain = 1.0f + driveNorm * (0.85f + underBias * 1.4f - overBias * 0.6f);
        const float H = input * tapeGain * juce::jlimit(0.95f, 1.45f, tapePressure);
        const float dH = H - prevInput;

        // 2. Коэрцитивная сила (ширина петли гистерезиса)
        // TMT: разброс коэрцитивности ленты (+/-6%)
        const float coerVar = tolerances ? tolerances->tapeCoercivity : 1.0f;

        const float biasCoercivityScale = 1.0f + underBias * 1.3f - overBias * 0.65f;

        const float coercivity = profile.coercivity
            * biasCoercivityScale
            * (1.0f - (tapePressure - 1.0f) * 0.04f)
            * coerVar;

        const float saturationThreshold = juce::jmax(0.35f, profile.balancedSaturation);
        const float H_eff = H / saturationThreshold;

        // 3. Безгистерезисная кривая через Ланжевена, БЕЗ сдвига на коэрцитивность
        //    (убрана "мёртвая зона" → нет кроссовер-искажений).
        //    Компенсируем коэффициент ослабления 1/3 функции Ланжевена
        const float H_eff_scaled = H_eff * 3.0f;
        const float targetFlux = langevin(H_eff_scaled);

        // 4. Малые петли (Minor Loops): коэрцитивность управляет СКОРОСТЬЮ
        //    намагничивания, формируя гистерезис без гейта на нуле.
        const float rate = juce::jlimit(0.01f, 0.75f,
            std::abs(dH) * (1.4f - profile.reversibility * 0.25f)
            / juce::jmax(0.25f, coercivity));
        fluxState += (targetFlux - fluxState) * rate;

        float output = fluxState;

        // 5. Нелинейности полиномов (доменная шероховатость)
        const float sqOut = output * output;
        constexpr float hdTrim = TapesDSP::harmonicDistortionTrim;

        output -= profile.oddHarmonics * hdTrim * sqOut * output * (1.0f + driveNorm * 1.25f);
        output += profile.evenHarmonics * hdTrim * sqOut * std::abs(output) * (1.0f + driveNorm * 0.75f);

        if (underBias > 0.001f)
            output += underBias * 0.035f * hdTrim * sqOut * (output >= 0.0f ? 1.0f : -1.0f);

        // 6. Инерция
        const float follow = 1.0f - overBias * 0.08f;
        output = previousOutput + (output - previousOutput) * follow;

        prevInput = H;
        previousOutput = output;

        // Музыкальный dry/wet магнитного ядра.
        // Даже при большом Tape Drive не отдаём 100% flux-модели,
        // потому что она может естественно терять уровень.
        float magneticBlend = juce::jlimit(0.0f, 1.0f, driveNorm / 0.55f);
        magneticBlend = magneticBlend * magneticBlend * (3.0f - 2.0f * magneticBlend);

        // Для мастеринг-ленты максимум ядра увеличиваем для большей теплоты
        magneticBlend *= 0.60f * TapesDSP::harmonicDistortionTrim;

        // Bias тоже может чуть увеличивать слышимость магнитной модели,
        // но не должен резко выбивать уровень.
        const float biasBlendBoost = juce::jlimit(0.0f, 0.25f, std::abs(bias) * 0.40f);
        magneticBlend = juce::jlimit(0.0f, 0.75f, magneticBlend + biasBlendBoost);

        // Небольшая компенсация потерь магнитной модели.
        // Увеличена компенсация (было 0.22f): предотвращает падение громкости
        // при сильном намагничивании.
        const float coreMakeup = 1.0f + magneticBlend * 0.45f;

        return input + ((output * coreMakeup) - input) * magneticBlend;
    }

private:
    float prevInput = 0.0f;
    float fluxState = 0.0f;
    float previousOutput = 0.0f;
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
};

class DynamicBiasSag
{
public:
    void prepare(double sampleRate)
    {
        attackCoeff = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate)));
        releaseCoeff = 1.0f - std::exp(-1.0f / (0.250f * static_cast<float>(sampleRate)));
        reset();
    }
    void reset()
    {
        envelope = 0.0f;
    }
    forcedinline float process(float input, float amount)
    {
        const float level = std::abs(input);
        const float coeff = level > envelope ? attackCoeff : releaseCoeff;
        envelope += coeff * (level - envelope);
        const float normalized = juce::jlimit(0.0f, 1.0f, envelope);
        // NPN-специфика: положительные пики проседают сильнее
        const float polarity = input >= 0.0f ? 1.15f : 0.85f;
        return normalized * juce::jlimit(0.0f, 1.0f, amount) * polarity;
    }
private:
    float envelope = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
};

class Spiral2Core {
public:
    void prepare() {}

    // TMT: подставить допуски (nullptr = Calibrated)
    void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept { tolerances = t; }

    forcedinline float process(float input, double drive, double presence, float mixAmount = 1.0f) {
        juce::ignoreUnused(presence);
        const float biasShift = tolerances ? tolerances->spiralBiasShift : 0.0f;
        const float hfeVar    = tolerances ? tolerances->transistorHfe    : 1.0f;

        const float adjustedInput = input * (1.0f + biasShift * 0.04f);
        const double adjustedDrive = drive * static_cast<double>(hfeVar);

        // 1. PUSH: накачиваем громкость перед транзисторами.
        const double pushGain = 1.0 + (adjustedDrive - 1.0) * 0.85 * TapesDSP::harmonicDistortionTrim;

        // 2. CLIP: жесткое транзисторное ограничение
        float in = fast_tanh(static_cast<float>(adjustedInput * pushGain));

        // PULL убран: горячий сигнал (до 1.0) летит прямо в ленту

        const float absIn = std::abs(in);
        if (absIn < 1.0e-4f)
            return in;

        // 3. SHAPE: добавляем транзисторную "искристость"
        const float shaped = std::sin(in * absIn) / absIn;
        float processed = in + (shaped - in) * TapesDSP::harmonicDistortionTrim;
        return input + (processed - input) * mixAmount;
    }

private:
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
};

class ArchiveWearHF {
public:
    void prepare(double sampleRate) {
        juce::ignoreUnused(sampleRate);
        reset();
    }

    void reset() {
        for (int i = 0; i < 4; ++i) {
            std::fill(std::begin(history[i]), std::end(history[i]), 0.0f);
            prevOutput[i] = 0.0f;
            indices[i] = 0;
        }
    }

    forcedinline float process(float input, float wearAmount, float hfEnergy) {
        if (wearAmount <= 0.001f) return input;

        const float stages = wearAmount * 4.0f;
        float w[4] = {
            juce::jlimit(0.0f, 1.0f, stages),
            juce::jlimit(0.0f, 1.0f, stages - 1.0f),
            juce::jlimit(0.0f, 1.0f, stages - 2.0f),
            juce::jlimit(0.0f, 1.0f, stages - 3.0f)
        };

        const float dynamicScale = 1.0f + hfEnergy * wearAmount * 2.5f; // Ослабили размытие ВЧ
        // ИСПРАВЛЕНО: Уменьшено число тапов (было 16.0f), чтобы сохранять читаемость и "воздух" ВЧ
        const int activeTaps = juce::jlimit(2, 10, static_cast<int>(2.0f + wearAmount * 7.0f * dynamicScale));
        const float tapWeight = 1.0f / static_cast<float>(activeTaps);

        float output = input;

        for (int s = 0; s < 4; ++s) {
            if (w[s] <= 0.0f) continue;

            float slew = output - prevOutput[s];
            history[s][indices[s]] = slew;

            float avgSlew = 0.0f;
            for (int i = 0; i < activeTaps; ++i) {
                int readIdx = (indices[s] - i + 20) % 20;
                avgSlew += history[s][readIdx] * tapWeight;
            }

            indices[s] = (indices[s] + 1) % 20;

            // ИСПРАВЛЕНО: Коррекция сглажена на 45%, верхний шельф/транзиенты больше не "съедаются" в ноль
            float correction = (slew - avgSlew) * 0.45f;
            prevOutput[s] = output;

            float chewed = output - correction;
            output = chewed * w[s] + output * (1.0f - w[s]);
        }

        return output;
    }

private:
    float history[4][20] = {};
    float prevOutput[4] = {0.0f};
    int indices[4] = {0};
};

class TapeEqualizer {
public:
    // TMT: подставить допуски (nullptr = Calibrated). Пересчёт EQ выполнит вызывающий
    // код повторным вызовом updateParameters().
    void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept { tolerances = t; }

    struct TapeCurve
    {
        float frequency = 16000.0f;
        float gainDb = 14.0f;
    };

    void prepare(double sampleRate, int maximumBlockSize = 65536)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(maximumBlockSize), 1 };
        preEmphasis.prepare(spec);
        airEmphasis.prepare(sampleRate);
        deEmphasis.prepare(spec);
        headBumpPrimary.prepare(spec);
        headBumpDip.prepare(spec);
        headBumpSecondary.prepare(spec);
        gapLoss.prepare(sampleRate);
        profileMidContour.prepare(sampleRate);
        biasLowShelf.prepare(sampleRate);
        biasMidPeak.prepare(sampleRate);
        biasHighShelf.prepare(sampleRate);
        archiveCompensationPeak.prepare(sampleRate);
        archiveCompensationBody.prepare(sampleRate);
        archiveCompensationDip.prepare(sampleRate);
preEmphasis.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sampleRate, 1000.0, 0.707, 1.0);
deEmphasis.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sampleRate, 1000.0, 0.707, 1.0);
        headBumpPrimary.coefficients = juce::dsp::IIR::Coefficients<double>::makeAllPass(sampleRate, 1000.0);
        headBumpDip.coefficients = juce::dsp::IIR::Coefficients<double>::makeAllPass(sampleRate, 1000.0);
        headBumpSecondary.coefficients = juce::dsp::IIR::Coefficients<double>::makeAllPass(sampleRate, 1000.0);
        outputTrim = 1.0f;
        thermalDrift = 0.0f;
        thermalPhase = 0.0f;
        thermalUpdateCounter = 0;
        currentRecordFreq = 16000.0f;
        currentRecordGainDb = 0.0f;
        currentPlaybackFreq = 16000.0f;
        currentPlaybackGainDb = 0.0f;

        lastSpeed = -1;
        lastEq = -1;
        lastAge = -1.0f;
        lastMix = -1.0f;
        lastModelName = nullptr;
        currentAir = -999.0f;
        currentDecay = -999.0f;

        currentLossFreq = 16000.0f;
        baseGapLossFreq = 18000.0f;
        currentGapFreq = baseGapLossFreq;
        lastAppliedGapFreq = -999.0f;
        lastAppliedBias = 0.0f;
        lastAppliedBiasMix = -1.0f;
        biasFiltersActive = false;
        reset();
    }

    void reset()
    {
        preEmphasis.reset();
        airEmphasis.reset();
        deEmphasis.reset();
        headBumpPrimary.reset();
        headBumpDip.reset();
        headBumpSecondary.reset();
        gapLoss.reset();
        profileMidContour.reset();
        biasLowShelf.reset();
        biasMidPeak.reset();
        biasHighShelf.reset();
        archiveCompensationPeak.reset();
        archiveCompensationBody.reset();
        archiveCompensationDip.reset();
    }

    TapeCurve getTapeCurve(float speedNorm, int eqMode) const noexcept
    {
        return getTapeCurve(speedNorm, eqMode, TapesDSP::getBalancedProfile(0));
    }

    TapeCurve getTapeCurve(float speedNorm, int eqMode, const TapesDSP::TapeProfile& profile) const noexcept
    {
        TapeCurve curve;
        
        const float speedIps = TapesDSP::speedNormToIps(speedNorm);
        
        if (eqMode == 0) // CCIR
        {
            const float baseFreq = profile.preEmphasisFreq;
            curve.frequency = baseFreq * (1.0f + speedNorm * 0.18f);
            curve.gainDb = profile.preEmphasisGainDb * (1.0f + speedNorm * 0.25f);
            
            if (speedIps < 7.5f) {
                const float slowPenalty = (7.5f - speedIps) / 5.625f;
                curve.gainDb *= (1.0f - slowPenalty * 0.35f);
            }
        }
        else // NAB (eqMode==1)
        {
            const float baseFreq = profile.preEmphasisFreq;
            curve.frequency = baseFreq * 0.65f * (1.0f + speedNorm * 0.12f);
            curve.gainDb = profile.preEmphasisGainDb * 0.55f * (1.0f + speedNorm * 0.15f);
            
            if (speedIps < 7.5f) {
                const float slowBonus = (7.5f - speedIps) / 5.625f;
                curve.gainDb *= (1.0f + slowBonus * 0.15f);
            }
        }
        
        return curve;
    }

    void updateParameters(float speedNorm, int eqMode, float airGainDb, float decay, float age, const TapesDSP::TapeProfile& profile)
    {
        updateParameters(speedNorm, eqMode, airGainDb, decay, age, profile, 1.0f);
    }

    void updateParameters(float speedNorm, int eqMode, float airGainDb, float decay, float age, const TapesDSP::TapeProfile& profile, float mixAmount)
    {
        const bool mixChanged = std::abs(mixAmount - lastMix) > 0.001f;
        
        const bool coreChanged = (std::abs(speedNorm - lastSpeed) > 0.001f || eqMode != lastEq
                                  || lastModelName != profile.name || age != lastAge || mixChanged);
        const bool airChanged   = std::abs(currentAir - airGainDb) > 0.005f;
        const bool decayChanged = std::abs(currentDecay - decay) > 0.005f;

        currentAir = airGainDb;
        currentDecay = decay;
        lastSpeed = speedNorm;
        lastEq = eqMode;
        lastModelName = profile.name;
        lastAge = age;
        lastMix = mixAmount;

        // --- ЯДРО: pre/de-emphasis + Head Bump (Contour Ripple) ---
        if (coreChanged)
        {
            const auto curve = getTapeCurve(speedNorm, eqMode, profile);
            currentRecordFreq = curve.frequency;
            currentRecordGainDb = curve.gainDb * mixAmount;
            currentPlaybackFreq = curve.frequency;
            currentPlaybackGainDb = -curve.gainDb * mixAmount;
            preEmphasis.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
                sr, currentRecordFreq, shelfSlope, juce::Decibels::decibelsToGain((double)currentRecordGainDb));
            deEmphasis.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighShelf(
                sr, currentPlaybackFreq, shelfSlope, juce::Decibels::decibelsToGain((double)currentPlaybackGainDb));

            const float speedIps = TapesDSP::speedNormToIps(speedNorm);
            const float speedScale = juce::jlimit(0.125f, 1.0f, speedIps / 15.0f);

            const float bumpSpeedScale = std::pow(speedScale, 0.70f);

            const float eqStdFreqScale = (eqMode == 0) ? 1.15f : 0.85f;
            const float f0 = profile.headBumpFreq * bumpSpeedScale * eqStdFreqScale;

            const float baseGain = profile.headBumpGainDb
                * (1.0f + (1.0f - speedScale) * 0.55f);

            const float eqStdBumpScale = (eqMode == 0) ? 1.35f : 0.75f;

            const float ageNorm = juce::jlimit(0.0f, 1.0f, age / 50.0f);
            const float thermalShift = 1.0f - ageNorm * 0.08f;
            const float f0_adjusted = f0 * thermalShift;
            const float ageAttenuation = 1.0f - ageNorm * 0.15f;
            const float baseGain_adjusted = baseGain * ageAttenuation * eqStdBumpScale * mixAmount;

            const float hbfVar = tolerances ? tolerances->headBumpFrequency : 1.0f;
            const float hbqVar = tolerances ? tolerances->headBumpQ         : 1.0f;
            const float f0_tmt = f0_adjusted * hbfVar;
            const float baseQ = (eqMode == 0) ? 1.5f : 0.9f;
            const float Q_tmt  = (baseQ * (1.0f - ageNorm * 0.2f)) * hbqVar;

            headBumpPrimary.coefficients = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
                sr, (double)f0_tmt, (double)Q_tmt, juce::Decibels::decibelsToGain((double)baseGain_adjusted));

            const float f_dip = f0 * 1.85f;
            const float dipGain = -(baseGain * 0.75f) * mixAmount;
            headBumpDip.coefficients = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
                sr, (double)f_dip, 1.8, juce::Decibels::decibelsToGain((double)dipGain));

            const float f_sec = f0 * 2.7f;
            const float secGain = baseGain * 0.35f * mixAmount;
            headBumpSecondary.coefficients = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
                sr, (double)f_sec, 2.0, juce::Decibels::decibelsToGain((double)secGain));

            profileMidContour.setPeakFilter(profile.midContourFreq, profile.midContourQ,
                                            juce::Decibels::decibelsToGain(profile.midContourGainDb * mixAmount));

            outputTrim = juce::Decibels::decibelsToGain(tapeCurveTrimDb);
        }

        // --- AIR (обновляется независимо, без пересчёта Head Bump) ---
        if (airChanged || coreChanged)
        {
            const float airFrequency = 7500.0f + speedNorm * 1500.0f;
            const float airDb = juce::jlimit(0.0f, 15.0f, airGainDb * mixAmount);
            airEmphasis.setHighShelf(airFrequency, 0.707f, juce::Decibels::decibelsToGain(airDb));
        }

        // --- GAP LOSS (статическая физическая модель) ---
        if (decayChanged || coreChanged)
        {
            const float gapVar = tolerances ? tolerances->gapLossFrequency : 1.0f;
            const float ageNorm = juce::jlimit(0.0f, 1.0f, age / 50.0f);

            // === ФИЗИЧЕСКАЯ МОДЕЛЬ GAP LOSS ===

            // 1. Базовая частота зависит от скорости (геометрия контакта)
            const float speedIps = TapesDSP::speedNormToIps(speedNorm);
            const float speedScale = juce::jlimit(0.125f, 1.0f, speedIps / 15.0f);

            const float speedLossFreq = profile.gapLossBaseFreq * std::pow(speedScale, 0.82f);

            const float eqStdGapScale = (eqMode == 0) ? 1.10f : 0.90f;
            const float speedLossFreq_adjusted = speedLossFreq * eqStdGapScale;

            // 2. Износ головки: зазор увеличивается
            const float wearFactor = 1.0f - ageNorm * 0.22f; 

            // 3. Decay knob: дополнительная HF потеря (поглощение)
            // ИСПРАВЛЕНО: Теперь кривая поглощения дает до -8 кГц потерь,
            // что точно соответствует отображению на ручке в интерфейсе.
            const float decayCurve = std::pow(decay / 10.0f, 1.15f) * 10.0f; 
            const float magneticDecay = decayCurve * 800.0f; 

            float targetGapFreq = speedLossFreq_adjusted * wearFactor * gapVar - magneticDecay;

            // ИСПРАВЛЕНО: Сняли ограничитель в 6500 Гц, опустив его до 1500 Гц. 
            // Теперь ручка Decay может полноценно "заглушить" высокие частоты на низких скоростях пленки.
            baseGapLossFreq = juce::jlimit(1500.0f, 22050.0f, 
                targetGapFreq + (22050.0f - targetGapFreq) * (1.0f - mixAmount));

            currentGapFreq = baseGapLossFreq;
            currentLossFreq = currentGapFreq;

            // ПРИМЕНЯЕМ ВСЕГДА! Чтобы избежать скачков фазы и щелчков при старте
            gapLoss.setLowPass(currentGapFreq, 0.707f);
            lastAppliedGapFreq = currentGapFreq;

            // === ARCHIVE COMPENSATORY RIPPLE CASCADE (Mechlabor / Studer Style) ===
            const float wearThreshold = 0.15f;
            float activeWear = 0.0f;
            if (ageNorm > wearThreshold) {
                activeWear = (ageNorm - wearThreshold) / (1.0f - wearThreshold);
            }

            const float wearShape = activeWear * activeWear;

            // 1. Основной острый пик износа зазора
            const float peakFreq = 14000.0f - (activeWear * 8000.0f);
            const float peakQ = 1.2f + activeWear * 2.3f;
            const float peakGainDb = wearShape * 11.0f;

            // 2. Широкая "подушка" снизу (широкий белл большей площади)
            const float bodyFreq = peakFreq * 0.62f;
            const float bodyQ = 0.55f + activeWear * 0.25f;
            const float bodyGainDb = activeWear * 4.0f;

            // 3. Интерференционный микро-дип сверху (создает нелинейную волну АЧХ)
            const float dipFreq = juce::jmin(21000.0f, peakFreq * 1.45f);
            const float dipQ = 1.8f + activeWear * 1.2f;
            const float dipGainDb = -activeWear * 3.2f;

            archiveCompensationPeak.setPeakFilter(peakFreq, peakQ, juce::Decibels::decibelsToGain(peakGainDb * mixAmount));
            archiveCompensationBody.setPeakFilter(bodyFreq, bodyQ, juce::Decibels::decibelsToGain(bodyGainDb * mixAmount));
            archiveCompensationDip.setPeakFilter(dipFreq, dipQ, juce::Decibels::decibelsToGain(dipGainDb * mixAmount));
        }
    }

    forcedinline float processPre(float input) {
        float out = static_cast<float>(preEmphasis.processSample(static_cast<double>(input)));
        out = airEmphasis.processSample(out);
        return out;
    }

    forcedinline float processPost(float input) {
        float out = static_cast<float>(deEmphasis.processSample(static_cast<double>(input)));

        // Каскад Contour Ripple Effect (гребенка огибания головки)
        out = static_cast<float>(headBumpPrimary.processSample(static_cast<double>(out)));
        out = static_cast<float>(headBumpDip.processSample(static_cast<double>(out)));
        out = static_cast<float>(headBumpSecondary.processSample(static_cast<double>(out)));

        out = profileMidContour.processSample(out);

        out = archiveCompensationPeak.processSample(out);
        out = archiveCompensationBody.processSample(out);
        out = archiveCompensationDip.processSample(out);

        if (biasFiltersActive) {
            out = biasLowShelf.processSample(out);
            out = biasMidPeak.processSample(out);
            out = biasHighShelf.processSample(out);
        }

        out *= outputTrim;

        // === THERMAL MODULATION (очень медленная, 12-минутный цикл) ===
        ++thermalUpdateCounter;
        if (thermalUpdateCounter >= static_cast<int>(sr))  // раз в секунду
        {
            thermalUpdateCounter = 0;

            const float thermalFreq = 1.0f / (12.0f * 60.0f);  // 12 минут
            thermalPhase += thermalFreq;
            if (thermalPhase >= 1.0f) thermalPhase -= 1.0f;

            const float ageNorm = juce::jlimit(0.0f, 1.0f, lastAge / 50.0f);
            const float thermalAmount = 0.015f * (1.0f + ageNorm * 2.0f);  // старые машины нестабильнее
            thermalDrift = std::sin(thermalPhase * juce::MathConstants<float>::twoPi) * thermalAmount;

            currentGapFreq = baseGapLossFreq * (1.0f + thermalDrift);

            gapLoss.setLowPass(currentGapFreq, 0.707f);
            lastAppliedGapFreq = currentGapFreq;
        }

        // СТАЛО (фильтр работает постоянно, сглаживая фазу):
        out = gapLoss.processSample(out);

        return out;
    }

    double getMagnitudeForFrequency(double freq) const {
        const double pre = preEmphasis.coefficients->getMagnitudeForFrequency(freq, sr);
        const double air = airEmphasis.getMagnitudeForFrequency(freq);
        const double deEmphasisMagnitude = deEmphasis.coefficients->getMagnitudeForFrequency(freq, sr);
        const double bump = headBumpPrimary.coefficients->getMagnitudeForFrequency(freq, sr)
                          * headBumpDip.coefficients->getMagnitudeForFrequency(freq, sr)
                          * headBumpSecondary.coefficients->getMagnitudeForFrequency(freq, sr);
        const double gap = gapLoss.getMagnitudeForFrequency(freq);
        const double contour = profileMidContour.getMagnitudeForFrequency(freq);
        const double archiveComp = archiveCompensationPeak.getMagnitudeForFrequency(freq)
                                  * archiveCompensationBody.getMagnitudeForFrequency(freq)
                                  * archiveCompensationDip.getMagnitudeForFrequency(freq);
        double biasResponse = 1.0;
        if (biasFiltersActive) {
            biasResponse = biasLowShelf.getMagnitudeForFrequency(freq)
                         * biasMidPeak.getMagnitudeForFrequency(freq)
                         * biasHighShelf.getMagnitudeForFrequency(freq);
        }
        return pre * air * deEmphasisMagnitude * bump * biasResponse * gap * contour * archiveComp * outputTrim;
    }

    void updateBiasResponse(float tapeBias, float tapeSpeedNorm, float mixAmount = 1.0f) {
        if (std::abs(tapeBias - lastAppliedBias) < 0.01f && std::abs(mixAmount - lastAppliedBiasMix) < 0.01f)
            return;
        lastAppliedBias = tapeBias;
        lastAppliedBiasMix = mixAmount;

        const float overBias = juce::jmax(0.0f, tapeBias);
        const float underBias = juce::jmax(0.0f, -tapeBias);

        if (std::abs(tapeBias) < 0.02f || mixAmount < 0.001f) {
            biasFiltersActive = false;
            return;
        }
        biasFiltersActive = true;

        const float lowGainDb = (overBias * 1.8f - underBias * 1.2f) * mixAmount;
        biasLowShelf.setLowShelf(100.0, 0.5, juce::Decibels::decibelsToGain(lowGainDb));

        const float midFreq = 1500.0f + tapeSpeedNorm * 500.0f;
        const float midGainDb = (underBias * 4.0f - overBias * 2.5f) * mixAmount;
        const float midQ = 1.2f - underBias * 0.3f;
        biasMidPeak.setPeakFilter(midFreq, midQ, juce::Decibels::decibelsToGain(midGainDb));

        const float highFreq = 8000.0f + tapeSpeedNorm * 2000.0f;
        const float highGainDb = (underBias * 5.0f - overBias * 4.5f) * mixAmount;
        biasHighShelf.setHighShelf(highFreq, 0.8, juce::Decibels::decibelsToGain(highGainDb));
    }

private:
    double sr = 44100.0;
    static constexpr float shelfSlope = 0.707f;
    static constexpr float tapeCurveTrimDb = 0.0f;
    float currentRecordFreq = 16000.0f;
    float currentRecordGainDb = 0.0f;
    float currentPlaybackFreq = 16000.0f;
    float currentPlaybackGainDb = 0.0f;
    float currentDecay = 0.0f;
    float outputTrim = 1.0f;
    float currentAir = 0.0f;

    // Gap Loss: статическая модель + медленная тепловая модуляция
    float currentLossFreq = 16000.0f;
    float baseGapLossFreq = 18000.0f;
    float currentGapFreq = 18000.0f;
    float thermalDrift = 0.0f;
    float thermalPhase = 0.0f;
    int   thermalUpdateCounter = 0;
    float lastAppliedGapFreq = -999.0f;
    float lastAppliedBias = 0.0f;
    float lastAppliedBiasMix = -1.0f;
    bool biasFiltersActive = false;

    // Кэш для ленивого пересчёта
    float lastSpeed = -999.0f;
    int   lastEq = -1;
    float lastAge = -1.0f;
    float lastMix = -1.0f;
    const char* lastModelName = nullptr;

    juce::dsp::IIR::Filter<double> preEmphasis, deEmphasis;
    FastBiquad airEmphasis, gapLoss, profileMidContour;
    FastBiquad biasLowShelf, biasMidPeak, biasHighShelf;
    // Head Bump остаётся на IIR (редкий пересчёт)
    juce::dsp::IIR::Filter<double> headBumpPrimary;
    juce::dsp::IIR::Filter<double> headBumpDip;
    juce::dsp::IIR::Filter<double> headBumpSecondary;
    FastBiquad archiveCompensationPeak;
    FastBiquad archiveCompensationBody;
    FastBiquad archiveCompensationDip;
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
};

class EmphasisTone {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        preLow.prepare(sampleRate); preHigh.prepare(sampleRate);
        postLow.prepare(sampleRate); postHigh.prepare(sampleRate);
        updateCoefficients(0.0f, 0.0f, 60.0f, 10000.0f);
    }

    void updateCoefficients(float bassBoostDb, float trebleBoostDb, float bassFreq, float trebleFreq) {
        currentBassDb = bassBoostDb;
        currentTrebleDb = trebleBoostDb;

        bassFreq = juce::jlimit(30.0f, 300.0f, bassFreq);
        trebleFreq = juce::jlimit(1000.0f, 15000.0f, trebleFreq);

        float bassQ = 0.40f + std::abs(bassBoostDb) * 0.01f;
        float trebleQ = 0.50f + std::abs(trebleBoostDb) * 0.01f;

        preLow.setLowShelf(bassFreq, bassQ, juce::Decibels::decibelsToGain(bassBoostDb));
        preHigh.setHighShelf(trebleFreq, trebleQ, juce::Decibels::decibelsToGain(trebleBoostDb));

        constexpr float compensationRatio = 0.8f;
        postLow.setLowShelf(bassFreq, bassQ, juce::Decibels::decibelsToGain(-bassBoostDb * compensationRatio));
        postHigh.setHighShelf(trebleFreq, trebleQ, juce::Decibels::decibelsToGain(-trebleBoostDb * compensationRatio));
    }

    forcedinline float processPre(float input) {
        if (std::abs(currentBassDb) < 0.05f && std::abs(currentTrebleDb) < 0.05f) return input;
        return preHigh.processSample(preLow.processSample(input));
    }

    forcedinline float processPost(float input) {
        if (std::abs(currentBassDb) < 0.05f && std::abs(currentTrebleDb) < 0.05f) return input;
        return postHigh.processSample(postLow.processSample(input));
    }

    double getMagnitudeForFrequency(double freq) const {
        if (std::abs(currentBassDb) < 0.05f && std::abs(currentTrebleDb) < 0.05f) return 1.0;
        // Показываем только Pre-EQ: форму, которая реально влетает в сатуратор.
        // Post-EQ зеркально компенсирует её, поэтому в совместной АЧХ они взаимно
        // уничтожаются — но визуально важна именно "накачка" перед перегрузом.
        return preLow.getMagnitudeForFrequency(freq) * preHigh.getMagnitudeForFrequency(freq);
    }

private:
    double sr = 44100.0;
    float currentBassDb = 0.0f;
    float currentTrebleDb = 0.0f;
    FastBiquad preLow, preHigh, postLow, postHigh;
};

class PultecTone {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        lowShelf.prepare(sampleRate);
        highShelf.prepare(sampleRate);
        updateCoefficients(0.0f, 0.0f, 60.0f, 10000.0f);
    }

    void updateCoefficients(float bassBoostDb, float trebleBoostDb,
                            float bassFrequency, float trebleFrequency) {
        currentBassDb = bassBoostDb;
        currentTrebleDb = trebleBoostDb;

        bassFrequency = juce::jlimit(30.0f, 200.0f, bassFrequency);
        trebleFrequency = juce::jlimit(2000.0f, 15000.0f, trebleFrequency);

        float bassQ = 0.35f + std::abs(bassBoostDb) * 0.012f;
        float trebleQ = 0.45f + std::abs(trebleBoostDb) * 0.012f;

        lowShelf.setLowShelf(bassFrequency, bassQ, juce::Decibels::decibelsToGain(bassBoostDb));
        highShelf.setHighShelf(trebleFrequency, trebleQ, juce::Decibels::decibelsToGain(trebleBoostDb));
    }

    forcedinline float process(float input) {
        float out = input;
        if (std::abs(currentBassDb) > 0.05f)
            out = lowShelf.processSample(out);
        if (std::abs(currentTrebleDb) > 0.05f)
            out = highShelf.processSample(out);
        return out;
    }

    double getMagnitudeForFrequency(double freq) const {
        const double low = lowShelf.getMagnitudeForFrequency(freq);
        const double high = highShelf.getMagnitudeForFrequency(freq);
        return low * high;
    }

private:
    double sr = 44100.0;
    float currentBassDb = 0.0f;
    float currentTrebleDb = 0.0f;
    FastBiquad lowShelf, highShelf;
};

class TransformerClipper {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        transformerFlux = 0.0f;
        clippingEnvelope = 0.0f;
        outputShelf.prepare(sampleRate);
        applyToleranceFilterCoefficients();
    }

    // TMT: подставить допуски
    void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept {
        tolerances = t;
        applyToleranceFilterCoefficients();
    }

    forcedinline float process(float input, float mixAmount = 1.0f) {
        if (mixAmount <= 0.0001f)
            return input;

        const float absInput = std::abs(input);

        // TMT: разброс порога клиппинга
        const float thresholdShift = tolerances ? tolerances->clipperThreshold : 0.0f;
        
        const float ceiling = juce::jlimit(0.90f, 0.999f, 0.988f + (thresholdShift * 0.01f));
        const float knee = ceiling * 0.65f;

        float output = input;

        if (absInput > knee) {
            const float sign = input >= 0.0f ? 1.0f : -1.0f;
            const float excess = absInput - knee;
            const float range = ceiling - knee;
            
            const float compressed = range * fast_tanh((excess / range) * TapesDSP::harmonicDistortionTrim);
            output = sign * (knee + compressed);

            transformerFlux += (output - transformerFlux) * 0.15f;
            output += transformerFlux * 0.015f * TapesDSP::harmonicDistortionTrim * sign;
            clippingEnvelope += (1.0f - clippingEnvelope) * 0.05f;
        } else {
            transformerFlux += (0.0f - transformerFlux) * 0.05f;
            clippingEnvelope += (0.0f - clippingEnvelope) * 0.002f;
        }

        const float shelfOut = static_cast<float>(outputShelf.processSample(static_cast<double>(output)));
        
        if (clippingEnvelope > 0.0001f) {
            output = output + clippingEnvelope * (shelfOut - output);
        }
        
        const float processed = juce::jlimit(-ceiling, ceiling, output);

        return input + (processed - input) * mixAmount;
    }

private:
    void applyToleranceFilterCoefficients() {
        const float lpfVar = tolerances ? tolerances->outputTransformerLPF : 1.0f;
        // Сглаживание ВЧ при клиппинге (-1.5 дБ на 14 кГц)
        outputShelf.setHighShelf(14000.0 * static_cast<double>(lpfVar), 0.707,
                                 juce::Decibels::decibelsToGain(-1.5));
    }

    double sr = 44100.0;
    float transformerFlux = 0.0f;
    float clippingEnvelope = 0.0f;
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
    FastBiquad outputShelf;
};

struct WowFlutterModulation
{
    float wow = 0.0f;
    float flutter = 0.0f;
};

class TapeMechanics {
public:
    static constexpr float baseDelaySec = 0.025f; // 25 мс статической задержки для PDC

    void prepare(double sampleRate, int maximumBlockSize = 65536) 
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(maximumBlockSize), 1 };

        delayLineLows.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));
        delayLineLows.prepare(spec);

        delayLineHighs.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));
        delayLineHighs.prepare(spec);

        crossover.coefficients = juce::dsp::IIR::Coefficients<double>::makeFirstOrderHighPass(sampleRate, 250.0);
        crossover.prepare(spec);

        aziPhase = 0.0f;
        smoothWowDepth = 0.0f;
        smoothFlutterDepth = 0.0f;
        smoothAzi   = 0.0f;
        smoothCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.05));
        lastAzimuthDepth = -1.0f;
        lastAgeForAzimuth = -1.0f;
    }

    // TMT: подставить допуски
    void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept { tolerances = t; }

    forcedinline float process(float input, float speedNorm, float mixDepth, float azimuthDepth, float age,
                               bool isLeftChannel, const WowFlutterModulation& modulation) {
        float highPassSample = static_cast<float>(crossover.processSample(static_cast<double>(input)));
        float lowPassSample  = input - highPassSample;

        const float ageNorm = juce::jlimit(0.0f, 1.0f, age / 50.0f);

        // Отвязываем глубину Wow/Flutter от скорости ленты и её возраста.
        // Теперь амплитуда детонации полностью изолирована и зависит ТОЛЬКО
        // от ручек WOW и FLUTTER (их значения уже переданы внутри структуры modulation).
        // Базовый коэффициент 0.0004f эквивалентен эталонной студийной машине.
        const float wowDepth = 0.0075f * mixDepth;
        const float flutterDepth = 0.0006f * mixDepth;

        // Azimuth: убран дубль ageNorm
        const float azimuthNorm = juce::jlimit(0.0f, 1.0f, azimuthDepth / 10.0f);
        const float mixNorm = juce::jlimit(0.0f, 1.0f, mixDepth);
        
        // TMT: разброс azimuth амплитуды (+/-20%)
        const float aziVar = tolerances ? tolerances->azimuthAmplitude : 1.0f;
        const float targetAzi = 0.002f * azimuthNorm * ageNorm * mixNorm * aziVar;

        smoothWowDepth += smoothCoeff * (wowDepth - smoothWowDepth);
        smoothFlutterDepth += smoothCoeff * (flutterDepth - smoothFlutterDepth);
        smoothAzi   += smoothCoeff * (targetAzi   - smoothAzi);

        aziPhase += 0.1f / static_cast<float>(sr);
        if (aziPhase >= 1.0f) aziPhase -= 1.0f;
        float azi = isLeftChannel ? 0.0f
                      : std::sin(aziPhase * juce::MathConstants<float>::twoPi) * smoothAzi;

        // --- ИСПРАВЛЕНИЕ: Общая модуляция для предотвращения фазовой гребенки ---
        const float commonModDelay = baseDelaySec + (modulation.wow * smoothWowDepth * 0.10f) + (modulation.flutter * smoothFlutterDepth);

        delayLineLows.pushSample(0, lowPassSample);
        const float lowDelaySamples = juce::jmax(0.0f, commonModDelay) * static_cast<float>(sr);
        float processedLows = delayLineLows.popSample(0, lowDelaySamples);

        delayLineHighs.pushSample(0, highPassSample);
        // Азимут добавляется только к верхам (для моносовместимого стерео-расширения)
        const float highDelaySamples = juce::jmax(0.0f, commonModDelay + azi) * static_cast<float>(sr);
        float processedHighs = delayLineHighs.popSample(0, highDelaySamples);

        return processedLows + processedHighs;
    }

private:
    // ОБЕ линии должны быть Lagrange3rd, иначе быстрая модуляция флаттера дает цифровой хрип!
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLineLows, delayLineHighs;
    juce::dsp::IIR::Filter<double> crossover;
    double sr = 44100.0;
    float aziPhase = 0.0f;
    float smoothWowDepth = 0.0f, smoothFlutterDepth = 0.0f;
    float smoothAzi = 0.0f, smoothCoeff = 0.001f;
    float lastAzimuthDepth = -1.0f;
    float lastAgeForAzimuth = -1.0f;
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
};

class ScrapeFlutter
{
public:
    void prepare(double sampleRate, int maximumBlockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(maximumBlockSize);
        spec.numChannels = 1;
        scrapeFilter.prepare(spec);
        follower.prepare(sampleRate);
        scrapeDelay.setMaximumDelayInSamples(juce::jmax(8, static_cast<int>(sampleRate * 0.001)));
        scrapeDelay.prepare(spec);
        scrapeFilter.coefficients = juce::dsp::IIR::Coefficients<double>::makeBandPass(sampleRate, 4000.0, 0.8);
        reset();
    }
    void reset()
    {
        scrapeFilter.reset();
        scrapeDelay.reset();
        follower.reset();
    }
    forcedinline float process(float input, float age, float amount)
    {
        const float ageNorm = juce::jlimit(0.0f, 1.0f, age / 50.0f);
        
        const float effectAmount = juce::jlimit(0.0f, 1.0f, amount) * (0.2f + ageNorm * 0.8f);
        
        if (effectAmount <= 0.0001f)
            return input;
            
        const float white = rng.nextFloat() * 2.0f - 1.0f;
        const float bandNoise = static_cast<float>(scrapeFilter.processSample(static_cast<double>(white)));
        const float envelope = follower.processSample(std::abs(input));
        
        const float modulationMs = bandNoise * envelope * effectAmount * 0.035f;
        const float delayMs = juce::jlimit(0.001f, 0.25f, 0.02f + modulationMs);
        const float delaySamples = delayMs * static_cast<float>(sr) / 1000.0f;
        scrapeDelay.setDelay(delaySamples);
        scrapeDelay.pushSample(0, input);
        return scrapeDelay.popSample(0);
    }
    void setSeed(uint32_t seed) { rng.setSeed(seed); }
private:
    class EnvelopeFollower
    {
    public:
        void prepare(double sampleRate)
        {
            attack = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate)));
            release = 1.0f - std::exp(-1.0f / (0.080f * static_cast<float>(sampleRate)));
            reset();
        }
        void reset()
        {
            value = 0.0f;
        }
        forcedinline float processSample(float input)
        {
            const float coeff = input > value ? attack : release;
            value += coeff * (input - value);
            return value;
        }
    private:
        float value = 0.0f;
        float attack = 0.0f;
        float release = 0.0f;
    };
    double sr = 44100.0;
    FastRandom rng { 42 };
    EnvelopeFollower follower;
    juce::dsp::IIR::Filter<double> scrapeFilter;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> scrapeDelay;
};

class StereoCrosstalk
{
public:
    void prepare(double sampleRate, int maximumBlockSize = 65536)
    {
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(maximumBlockSize), 1 };
        highPassL.prepare(spec);
        highPassR.prepare(spec);
        highPassL.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighPass(sampleRate, 5000.0, 0.707);
        highPassR.coefficients = juce::dsp::IIR::Coefficients<double>::makeHighPass(sampleRate, 5000.0, 0.707);
        reset();
    }
    void reset()
    {
        highPassL.reset();
        highPassR.reset();
    }
     // TMT: подставить допуски
     void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept { tolerances = t; }

     forcedinline void process(float& left, float& right, float amount, float age)
     {
         const float ageNorm = juce::jlimit(0.0f, 1.0f, age / 50.0f);
         const float effectiveAmount = juce::jlimit(0.0f, 1.0f, amount) * ageNorm;
        if (effectiveAmount <= 0.0001f)
            return;
         const float highL = static_cast<float>(highPassL.processSample(static_cast<double>(left)));
         const float highR = static_cast<float>(highPassR.processSample(static_cast<double>(right)));

        // TMT: разброс crosstalk
        const float crossVar = tolerances ? tolerances->crosstalkVariation : 1.0f;
        const float crossGain = juce::Decibels::decibelsToGain(-45.0f) * effectiveAmount * crossVar;
        const float oldL = left;
        const float oldR = right;
        left  = oldL + highR * crossGain;
        right = oldR + highL * crossGain;
    }
private:
    juce::dsp::IIR::Filter<double> highPassL;
    juce::dsp::IIR::Filter<double> highPassR;
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
};

class WowFlutterGenerator {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        sqrtdelta = 1.0f / std::sqrt(static_cast<float>(sampleRate));
        T = 1.0f / static_cast<float>(sampleRate);

        driftY = 0.0f;
        wowMomentum = 0.0f;

        phaseW = 0.0f;
        phaseF1 = 0.0f; phaseF2 = 0.0f; phaseF3 = 0.0f;

        driftLpf.prepare(sampleRate);
        driftLpf.setLowPass(10.0, 0.707); // 10 Hz LPF
    }

    void setSeed(uint32_t seed) { rng.setSeed(seed); }

    void setTolerances(const ToleranceModel::ComponentTolerances* t) noexcept { tolerances = t; }

    forcedinline WowFlutterModulation generate(float speedNorm, float wowAmount, float flutterAmount) {
        const float wowScale = juce::jlimit(0.0f, 1.0f, wowAmount);
        const float flutterScale = juce::jlimit(0.0f, 1.0f, flutterAmount);

        // --- 1. WOW: Ornstein-Uhlenbeck Process + LFO ---
        float rawGaussian = (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 2.0f) * 1.732f;
        float filteredGaussian = driftLpf.processSample(rawGaussian);

        float damping = (wowScale * 20.0f) + 1.0f;
        driftY += sqrtdelta * filteredGaussian * (wowScale * 0.8f);
        driftY += damping * (wowScale - driftY) * T; 

        const float wowFreqVar = tolerances ? tolerances->wowFrequencyDrift : 1.0f;
        const float speedIps = TapesDSP::speedNormToIps(speedNorm);
        const float speedScale = juce::jlimit(0.125f, 1.0f, speedIps / 15.0f);
        const float speedRoot = std::sqrt(speedScale);

        const float wowFreq = (0.32f + speedRoot * 0.48f) * wowFreqVar;
        phaseW += wowFreq * T;
        if (phaseW >= 1.0f) phaseW -= 1.0f;
        const float wowPeriodic = std::sin(phaseW * juce::MathConstants<float>::twoPi) * 0.4f * wowScale;

        // [ИЗМЕНЕНИЕ]: Умножаем сумму периодического LFO и хаотичного дрейфа на 1.90f
        // Это сделает "плавание" питча значительно более выраженным на максимальных значениях ручки
        const float wowTarget = (wowPeriodic + juce::jlimit(-2.0f, 2.0f, driftY - wowScale)) * 1.90f;
        const float inertia = 0.82f + speedNorm * 0.10f;
        wowMomentum = inertia * wowMomentum + (1.0f - inertia) * wowTarget;

        // --- 2. FLUTTER: CHOW TAPE SIMPLIFIED ---
        const float flutterFreqVar = tolerances ? tolerances->flutterFrequencyDrift : 1.0f;
        // [ИЗМЕНЕНИЕ 1]: Уменьшаем частоту флаттера на 20% (множитель 0.80f)
        // Теперь флаттер звучит не как механическое "жужжание", а как быстрое, но читаемое дрожание ленты
        const float flutterBaseFreq = (7.0f + speedRoot * 20.0f) * flutterFreqVar * 0.80f;

        const float dTheta1 = juce::MathConstants<float>::twoPi * flutterBaseFreq * T;
        
        // Надежно кольцуем фазы, чтобы они не улетали в бесконечность
        phaseF1 = std::fmod(phaseF1 + dTheta1, juce::MathConstants<float>::twoPi);
        phaseF2 = std::fmod(phaseF2 + 2.0f * dTheta1, juce::MathConstants<float>::twoPi);
        phaseF3 = std::fmod(phaseF3 + 3.0f * dTheta1, juce::MathConstants<float>::twoPi);

        const float off2 = 13.0f * juce::MathConstants<float>::pi / 4.0f;
        const float off3 = -juce::MathConstants<float>::pi / 10.0f;

        // ИСПОЛЬЗУЕМ СТРОГО std::cos! FastMath падает от фаз > pi и взрывает DelayLine!
        const float complexFlutter = (-0.230f * std::cos(phaseF1)) +
                                     (-0.080f * std::cos(phaseF2 + off2)) +
                                     (-0.099f * std::cos(phaseF3 + off3));

        // [ИЗМЕНЕНИЕ 2]: Уменьшаем силу влияния на 50% (множитель снижен с 3.5f до 1.75f)
        // Теперь даже на 100% ручка Flutter не будет превращать звук в "кашу"
        const float finalFlutter = complexFlutter * flutterScale * 1.75f;

        return { wowMomentum, finalFlutter };
    }

private:
    double sr = 44100.0;
    float sqrtdelta = 1.0f;
    float T = 1.0f;
    float driftY = 0.0f;
    float wowMomentum = 0.0f; 
    float phaseW = 0.0f;
    float phaseF1 = 0.0f, phaseF2 = 0.0f, phaseF3 = 0.0f;
    FastBiquad driftLpf; 
    FastRandom rng { 1984 };
    const ToleranceModel::ComponentTolerances* tolerances = nullptr;
};

class OxideDropouts {
public:
    void prepare(double newSampleRate) {
        sampleRate = newSampleRate;
        currentGainL = currentGainR = targetGainL = targetGainR = 1.0f;
        samplesUntilNextDropout = 0;
        dropoutSamplesRemaining = 0;
        clusterRemaining = 0;
    }

    forcedinline void process(float& inputL, float& inputR, float oxideAmount, float age) {
        const float oxideNorm = juce::jlimit(0.0f, 1.0f, oxideAmount / 10.0f);
        const float ageNorm = juce::jlimit(0.0f, 1.0f, age / 50.0f);
        
        if (oxideNorm <= 0.0f) { 
            currentGainL += (1.0f - currentGainL) * 0.01f; 
            currentGainR += (1.0f - currentGainR) * 0.01f; 
            inputL *= currentGainL;
            inputR *= currentGainR;
            return; 
        }

        if (samplesUntilNextDropout <= 0 && dropoutSamplesRemaining <= 0) {
            if (clusterRemaining > 0) {
                samplesUntilNextDropout = static_cast<int>(randomFloat(rng, 0.02f, 0.1f) * sampleRate);
            } else {
                if (rng.nextFloat() < 0.35f)
                    clusterRemaining = static_cast<int>(randomFloat(rng, 1.0f, 3.0f));
                
                const float eventsPerMinute = 15.0f * oxideNorm + 45.0f * oxideNorm * oxideNorm * (1.0f + ageNorm * 1.5f);
                const float interval = eventsPerMinute > 0.0f
                    ? (60.0f * static_cast<float>(sampleRate) / eventsPerMinute) : 1.0e9f;
                
                samplesUntilNextDropout = static_cast<int>(randomFloat(rng, 0.2f, 1.2f) * interval);
            }
        }
        
        if (samplesUntilNextDropout > 0) --samplesUntilNextDropout;
        
        if (samplesUntilNextDropout == 0 && dropoutSamplesRemaining <= 0) {
            const int duration = static_cast<int>(randomFloat(rng, 0.01f, 0.15f) * sampleRate);
            dropoutSamplesRemaining = juce::jmax(1, duration);
            
            const float baseDepthDb = randomFloat(rng, 3.0f, 12.0f) * oxideNorm * (1.0f + 1.2f * ageNorm);
            const float lrBias = randomFloat(rng, -1.0f, 1.0f);
            
            targetGainL = juce::Decibels::decibelsToGain(-juce::jmax(0.0f, baseDepthDb + lrBias));
            targetGainR = juce::Decibels::decibelsToGain(-juce::jmax(0.0f, baseDepthDb - lrBias));
            
            if (clusterRemaining > 0) --clusterRemaining;
        }
        
        if (dropoutSamplesRemaining > 0) {
            --dropoutSamplesRemaining;
            if (dropoutSamplesRemaining == 0) {
                targetGainL = 1.0f;
                targetGainR = 1.0f;
            }
        } 
        
        const float coeffL = (targetGainL < currentGainL) ? 0.08f : 0.008f;
        const float coeffR = (targetGainR < currentGainR) ? 0.08f : 0.008f;
        
        currentGainL += (targetGainL - currentGainL) * coeffL;
        currentGainR += (targetGainR - currentGainR) * coeffR;
        
        inputL *= currentGainL;
        inputR *= currentGainR;
    }

    float getCurrentGainL() const noexcept { return currentGainL; }
    float getCurrentGainR() const noexcept { return currentGainR; }

    void setSeed(uint32_t seed) { rng.setSeed(seed); }

private:
    float currentGainL = 1.0f, currentGainR = 1.0f;
    float targetGainL = 1.0f, targetGainR = 1.0f;
    int samplesUntilNextDropout = 0, dropoutSamplesRemaining = 0;
    int clusterRemaining = 0;
    double sampleRate = 44100.0;
    FastRandom rng { 2026 };
};

class DCBlocker {
public:
    void prepare(double sampleRate) {
        R = 1.0 - (juce::MathConstants<double>::twoPi * 10.0 / sampleRate);
        x1 = 0.0;
        y1 = 0.0;
    }

    void reset() {
        x1 = 0.0;
        y1 = 0.0;
    }

    forcedinline float process(float in) {
        double din = static_cast<double>(in);
        double out = din - x1 + R * y1;
        x1 = din;
        y1 = out;
        return static_cast<float>(out);
    }

private:
    double R = 0.999, x1 = 0.0, y1 = 0.0;
};

class MainsHum {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        phase = 0.0f;
        driftPhase = 0.0f;
    }

    forcedinline float process(float humAmount) {
        const float amount = juce::jlimit(0.0f, 1.0f, humAmount);
        if (amount <= 0.0001f) return 0.0f;

        driftPhase += 0.05f / static_cast<float>(sr);
        if (driftPhase >= 1.0f) driftPhase -= 1.0f;
        
        const float mainsFreq = 50.0f + std::sin(driftPhase * juce::MathConstants<float>::twoPi) * 0.4f;

        phase += mainsFreq / static_cast<float>(sr);
        if (phase >= 1.0f) phase -= 1.0f;

        const float w = phase * juce::MathConstants<float>::twoPi;

        const float hum = std::sin(w) * 0.40f 
                        + std::sin(w * 2.0f) * 1.0f
                        + std::sin(w * 3.0f) * 0.30f 
                        + std::sin(w * 5.0f) * 0.15f;

        const float humGainDb = -85.0f + amount * 30.0f;
        return hum * juce::Decibels::decibelsToGain(humGainDb);
    }

private:
    double sr = 44100.0;
    float phase = 0.0f, driftPhase = 0.0f;
};

class ArchivalGrainPlayer {
public:
    void prepare(double sampleRate, const void* embeddedData, size_t dataSize) {
        sr = sampleRate;
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats(); 
        
        totalSamples = 0;
        
        if (embeddedData != nullptr && dataSize > 0) {
            auto stream = std::make_unique<juce::MemoryInputStream>(embeddedData, dataSize, false);
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(stream)));
            
            if (reader != nullptr && reader->lengthInSamples > 0) {
                sampleBuffer.setSize(1, static_cast<int>(reader->lengthInSamples));
                reader->read(&sampleBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);
                totalSamples = sampleBuffer.getNumSamples();
            }
        }

        if (totalSamples == 0) {
            totalSamples = static_cast<int>(sr * 2.0);
            sampleBuffer.setSize(1, totalSamples);
            float* w = sampleBuffer.getWritePointer(0);
            FastRandom noiseRng(1984);
            float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
            for (int i = 0; i < totalSamples; ++i) {
                float white = noiseRng.nextFloat() * 2.0f - 1.0f;
                b0 = 0.99765f * b0 + white * 0.0990460f;
                b1 = 0.96300f * b1 + white * 0.2965164f;
                b2 = 0.57000f * b2 + white * 1.0526913f;
                float pink = (b0 + b1 + b2 + white * 0.1848f) * 0.05f;
                w[i] = pink;
            }
        }
        
        dynAttack = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sr)));
        dynRelease = 1.0f - std::exp(-1.0f / (0.350f * static_cast<float>(sr)));
        
        reset();
    }

    void reset() {
        if (totalSamples > 0) playHead = static_cast<double>(rng.nextInt() % totalSamples);
        else playHead = 0.0;
        env = 0.0f;
    }

    int getNumLoadedSamples() const noexcept { return sampleBuffer.getNumSamples(); }

    forcedinline float process(float inputSample, NoiseMode mode, float speedNorm, float ageNorm) {
        if (totalSamples == 0 || mode == NoiseMode::off) 
            return 0.0f;

        const double pitchRatio = 0.85 + speedNorm * 0.25; 
        
        int posInt = static_cast<int>(playHead) % totalSamples;
        double alpha = playHead - static_cast<int>(playHead);
        int nextPos = (posInt + 1) % totalSamples;
        const float* rawData = sampleBuffer.getReadPointer(0);
        
        float rawGrain = static_cast<float>((1.0 - alpha) * rawData[posInt] + alpha * rawData[nextPos]);

        playHead += pitchRatio;
        if (playHead >= totalSamples) playHead -= totalSamples;

        float outGain = 1.0f;
        if (mode == NoiseMode::dynamicNoise) {
            float absIn = std::abs(inputSample); 
            env += (absIn > env ? dynAttack : dynRelease) * (absIn - env);
            outGain = juce::jlimit(0.0f, 1.0f, (env - 0.0005f) * 200.0f);
        } else {
            env = 1.0f;
            outGain = 1.0f;
        }

        return rawGrain * outGain;
    }

private:
    juce::AudioBuffer<float> sampleBuffer;
    int totalSamples = 0;
    double playHead = 0.0;
    double sr = 44100.0;
    float env = 0.0f;
    float dynAttack = 0.01f, dynRelease = 0.001f;
    FastRandom rng { static_cast<uint32_t>(juce::Time::currentTimeMillis()) };
};

class ContactNoise {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        env = 0.0f;
        crackleEnv = 0.0f;
        rng.setSeed(2026);
    }

    void reset() { env = 0.0f; crackleEnv = 0.0f; rng.setSeed(2026); }

    forcedinline float process(float input, float age, float humAmount, NoiseMode mode)
    {
        if (mode == NoiseMode::off) return 0.0f;

        const float ageNorm = juce::jlimit(0.0f, 1.0f, age / 50.0f);
        const float humFactor = juce::jlimit(0.0f, 1.0f, humAmount);

        const float absIn = std::abs(input);
        const float attackCoeff = 1.0f - std::exp(-1.0f / (0.006f * static_cast<float>(sr)));
        const float releaseCoeff = 1.0f - std::exp(-1.0f / (0.350f * static_cast<float>(sr)));
        env += (absIn > env ? attackCoeff : releaseCoeff) * (absIn - env);

        const float crackleProb = 0.00012f + ageNorm * 0.00020f + humFactor * 0.00008f;
        const float crackle = (rng.nextFloat() < crackleProb)
            ? (rng.nextFloat() * 2.0f - 1.0f) * (0.25f + ageNorm * 0.75f)
            : 0.0f;

        crackleEnv += (crackle != 0.0f ? 0.85f : 0.0f) * (0.0f - crackleEnv);
        crackleEnv += (std::abs(crackle) > crackleEnv ? 0.70f : 0.08f)
                    * (std::abs(crackle) - crackleEnv);

        const float rustle = (rng.nextFloat() * 2.0f - 1.0f) * 0.015f * (0.75f + ageNorm * 0.45f);
        const float modulated = rustle + crackle;
        const float cableGain = 0.14f + ageNorm * 0.10f + humFactor * 0.04f;

        float noiseOut = modulated * cableGain * (0.5f + env * 1.5f);

        if (mode == NoiseMode::dynamicNoise) {
            float gate = juce::jlimit(0.0f, 1.0f, (env - 0.0005f) * 200.0f);
            noiseOut *= gate;
        }

        return noiseOut;
    }

private:
    double sr = 44100.0;
    float env = 0.0f;
    float crackleEnv = 0.0f;
    FastRandom rng { 2026 };
};

} // namespace TroakarDSP
