#include "PluginProcessor.h"

DomRadioDriveAudioProcessor::DomRadioDriveAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    inGainParam    = apvts.getRawParameterValue("IN_GAIN");
    driveTypeParam = apvts.getRawParameterValue("DRIVE_TYPE");
    driveParam     = apvts.getRawParameterValue("DRIVE");
    ironCoreParam  = apvts.getRawParameterValue("IRON_CORE");
    mixParam       = apvts.getRawParameterValue("MIX");
    outLvlParam    = apvts.getRawParameterValue("OUT_LVL");

    toleranceModel.setMode(TroakarDSP::ToleranceModel::UnitMode::Calibrated);
}

DomRadioDriveAudioProcessor::~DomRadioDriveAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout DomRadioDriveAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto dbFormat = [](float value, int) { return juce::String(value, 1) + " dB"; };
    auto pctFormat = [](float value, int) { return juce::String(static_cast<int>(std::round(value))) + " %"; };

    using FloatAttr = juce::AudioParameterFloatAttributes;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IN_GAIN", "Input Gain", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat)));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "DRIVE_TYPE", "Drive Type", juce::StringArray { "Silicon (Modern)", "Germanium (Vintage)" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DRIVE", "Drive", juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IRON_CORE", "Iron Core (Transformer)", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float v, int){ return juce::String(v * 100.0f, 0) + " %"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OUT_LVL", "Output Level", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat)));

    return { params.begin(), params.end() };
}

void DomRadioDriveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const size_t maxBlockSize = (size_t) juce::jmax(2048, samplesPerBlock);
    
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false
    );
    oversampler->initProcessing(maxBlockSize);
    oversampler->reset();

    const double osRate = sampleRate * 2.0;
    const int osMaxBlock = (int)maxBlockSize * 2;

    osWorkBuffer.setSize(2, (int)maxBlockSize);

    transL.prepare(osRate, osMaxBlock);
    transR.prepare(osRate, osMaxBlock);
    transL.setTolerances(&toleranceModel.getTolerancesL());
    transR.setTolerances(&toleranceModel.getTolerancesR());

    spiralL.prepare(); spiralR.prepare();
    spiralL.setTolerances(&toleranceModel.getTolerancesL());
    spiralR.setTolerances(&toleranceModel.getTolerancesR());

    clipL.prepare(sampleRate); clipR.prepare(sampleRate);
    clipL.setTolerances(&toleranceModel.getTolerancesL());
    clipR.setTolerances(&toleranceModel.getTolerancesR());

    dcBlockL.prepare(sampleRate); dcBlockR.prepare(sampleRate);
    dcBlockL.reset(); dcBlockR.reset();

    const float latency = oversampler->getLatencyInSamples();
    setLatencySamples(static_cast<int>(latency));
    
    dryDelayL.prepare({ sampleRate, (juce::uint32)samplesPerBlock, 1 });
    dryDelayR.prepare({ sampleRate, (juce::uint32)samplesPerBlock, 1 });
    dryDelayL.reset(); dryDelayR.reset();
    
    dryBuffer.setSize(2, samplesPerBlock + 1024);

    inGainSmoothed.reset(sampleRate, 0.010);
    driveSmoothed.reset(sampleRate, 0.020);
    ironCoreSmoothed.reset(sampleRate, 0.050);
    mixSmoothed.reset(sampleRate, 0.010);
    outLvlSmoothed.reset(sampleRate, 0.010);

    inGainSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inGainParam->load()));
    driveSmoothed.setCurrentAndTargetValue(driveParam->load());
    ironCoreSmoothed.setCurrentAndTargetValue(ironCoreParam->load());
    mixSmoothed.setCurrentAndTargetValue(mixParam->load() / 100.0f);
    outLvlSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outLvlParam->load()));
}

void DomRadioDriveAudioProcessor::releaseResources() {}

void DomRadioDriveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || oversampler == nullptr) return;

    if (dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize(2, numSamples, true, true, true);
    if (osWorkBuffer.getNumSamples() < numSamples)
        osWorkBuffer.setSize(2, numSamples, false, false, true);

    inGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(inGainParam->load()));
    driveSmoothed.setTargetValue(driveParam->load());
    ironCoreSmoothed.setTargetValue(ironCoreParam->load());
    mixSmoothed.setTargetValue(mixParam->load() / 100.0f);
    outLvlSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outLvlParam->load()));

    const int type = static_cast<int>(driveTypeParam->load());
    const float hfeMultiplier = (type == 1) ? 1.35f : 1.0f; 

    if (inGainSmoothed.isSmoothing()) {
        for (int i = 0; i < numSamples; ++i) {
            float g = inGainSmoothed.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * g);
        }
    } else {
        buffer.applyGain(inGainSmoothed.getCurrentValue());
    }

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    osWorkBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1) osWorkBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);
    else osWorkBuffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    juce::dsp::AudioBlock<float> fullBlock(osWorkBuffer);
    auto block = fullBlock.getSubBlock(0, (size_t)numSamples);
    auto osBlock = oversampler->processSamplesUp(block);

    const int osNumSamples = (int)osBlock.getNumSamples();
    auto* osL = osBlock.getChannelPointer(0);
    auto* osR = osBlock.getChannelPointer(1);

    const float targetDrive = driveSmoothed.getCurrentValue();
    const float targetIron = ironCoreSmoothed.getCurrentValue();

    driveSmoothed.skip(numSamples);
    ironCoreSmoothed.skip(numSamples);

    for (int i = 0; i < osNumSamples; ++i)
    {
        float wetL = osL[i];
        wetL = transL.process(wetL, targetIron);
        wetL = spiralL.process(wetL, targetDrive * hfeMultiplier, 0.1f);
        osL[i] = wetL;

        float wetR = osR[i];
        wetR = transR.process(wetR, targetIron);
        wetR = spiralR.process(wetR, targetDrive * hfeMultiplier, 0.1f);
        osR[i] = wetR;
    }

    oversampler->processSamplesDown(block);

    buffer.copyFrom(0, 0, osWorkBuffer, 0, 0, numSamples);
    if (numChannels > 1) buffer.copyFrom(1, 0, osWorkBuffer, 1, 0, numSamples);

    auto* wetL_ptr = buffer.getWritePointer(0);
    auto* wetR_ptr = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    auto* dryL_ptr = dryBuffer.getReadPointer(0);
    auto* dryR_ptr = numChannels > 1 ? dryBuffer.getReadPointer(1) : nullptr;

    const float latency = oversampler->getLatencyInSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const float currentMix = mixSmoothed.getNextValue();
        const float currentOut = outLvlSmoothed.getNextValue();

        float dL = dryDelayL.popSample(0, latency);
        dryDelayL.pushSample(0, dryL_ptr[i]);
        float mixedL = dL * (1.0f - currentMix) + wetL_ptr[i] * currentMix;
        wetL_ptr[i] = dcBlockL.process(clipL.process(mixedL * currentOut));

        if (wetR_ptr && dryR_ptr) {
            float dR = dryDelayR.popSample(0, latency);
            dryDelayR.pushSample(0, dryR_ptr[i]);
            float mixedR = dR * (1.0f - currentMix) + wetR_ptr[i] * currentMix;
            wetR_ptr[i] = dcBlockR.process(clipR.process(mixedR * currentOut));
        }
    }
}

juce::AudioProcessorEditor* DomRadioDriveAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void DomRadioDriveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DomRadioDriveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DomRadioDriveAudioProcessor();
}
