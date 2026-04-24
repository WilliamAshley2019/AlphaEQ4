#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Api550bAudioProcessor();
}

// =============================================================================
// Static members
// =============================================================================
float Api550bAudioProcessor::satDriveValue = 2.0f;

float Api550bAudioProcessor::applySaturation(float x)
{
    // Soft-clip via tanh, normalised so unity-gain input stays at unity-gain
    // output when drive == 1. Drive == 0 is treated as bypass.
    return satDriveValue > 0.001f
        ? std::tanh(satDriveValue * x) / satDriveValue
        : x;
}

// =============================================================================
// Helper
// =============================================================================
float Api550bAudioProcessor::getEffectiveGainDb(const juce::String& gainParam,
                                                 const juce::String& muteParam) const
{
    // "Mute" in this plugin means: mute the band's EQ effect (flat response),
    // not silence the audio. This is consistent with the original design.
    if (apvts.getRawParameterValue(muteParam)->load() > 0.5f)
        return 0.0f;

    int idx = juce::jlimit(0, (int)std::size(EQTables::gainDb) - 1,
                           (int)apvts.getRawParameterValue(gainParam)->load());
    return EQTables::gainDb[idx];
}

// =============================================================================
// Constructor
// =============================================================================
Api550bAudioProcessor::Api550bAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo())
          .withOutput("Output", juce::AudioChannelSet::stereo())),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Register listeners for every parameter so parameterChanged fires.
    for (auto& id : { Params::LOW_FREQ,  Params::LOW_GAIN,  Params::LOW_SHELF,
                      Params::LOW_MUTE,  Params::LOW_BYPASS,
                      Params::LM_FREQ,   Params::LM_GAIN,
                      Params::LM_MUTE,   Params::LM_BYPASS,
                      Params::HM_FREQ,   Params::HM_GAIN,
                      Params::HM_MUTE,   Params::HM_BYPASS,
                      Params::HIGH_FREQ, Params::HIGH_GAIN, Params::HIGH_SHELF,
                      Params::HIGH_MUTE, Params::HIGH_BYPASS,
                      Params::SAT_DRIVE, Params::Q_MODE })
        apvts.addParameterListener(id, this);
}

// =============================================================================
// Parameter layout
// =============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
Api550bAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray lowFreqChoices { "40", "75", "150", "300", "600", "1.2k", "2.4k"       };
    juce::StringArray highFreqChoices{ "800", "1.5k", "3k", "5k", "7k", "10k", "12.5k"       };
    juce::StringArray gainChoices    { "-12", "-9", "-6", "-3", "0", "3", "6", "9", "12"      };

    auto addBand = [&](const juce::String& prefix, const juce::String& name,
                       const juce::StringArray& freqs, int defaultFreqIdx)
    {
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            prefix + "_FREQ",   name + " Freq",   freqs,      defaultFreqIdx));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            prefix + "_GAIN",   name + " Gain",   gainChoices, 4)); // default 0 dB (index 4)
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            prefix + "_MUTE",   name + " Mute",   false));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            prefix + "_BYPASS", name + " Bypass", false));
    };

    addBand("LOW",  "Low",      lowFreqChoices,  3); // 300 Hz
    params.push_back(std::make_unique<juce::AudioParameterBool>(Params::LOW_SHELF,  "Low Shelf",       false));
    addBand("LM",   "Low Mid",  lowFreqChoices,  4); // 600 Hz
    addBand("HM",   "High Mid", highFreqChoices, 2); // 3 kHz
    addBand("HIGH", "High",     highFreqChoices, 3); // 5 kHz
    params.push_back(std::make_unique<juce::AudioParameterBool>(Params::HIGH_SHELF, "High Shelf",      false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Params::SAT_DRIVE, "Saturation Drive",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(Params::Q_MODE,     "Proportional Q",  false));

    return { params.begin(), params.end() };
}

// =============================================================================
// prepareToPlay
// =============================================================================
void Api550bAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // EQ filters run at native sample rate.
    juce::dsp::ProcessSpec nativeSpec{
        sampleRate,
        static_cast<juce::uint32>(samplesPerBlock),
        static_cast<juce::uint32>(getTotalNumInputChannels()) };

    lowFilter.prepare(nativeSpec);
    lowMidFilter.prepare(nativeSpec);
    highMidFilter.prepare(nativeSpec);
    highFilter.prepare(nativeSpec);

    // Oversampler allocates internal buffers based on max block size.
    oversampler.reset();
    oversampler.initProcessing(static_cast<size_t>(samplesPerBlock));

    // Waveshaper runs at 2× rate.
    juce::dsp::ProcessSpec oversampledSpec{
        sampleRate * 2.0,
        static_cast<juce::uint32>(samplesPerBlock * 2),
        static_cast<juce::uint32>(getTotalNumInputChannels()) };
    saturation.prepare(oversampledSpec);
    saturation.functionToUse = &applySaturation;

    // -------------------------------------------------------------------------
    // Initialise smoothers from current parameter state so there's no ramp
    // from zero on first playback start.
    // -------------------------------------------------------------------------
    constexpr double rampSecs = 0.10; // 100 ms — enough to be inaudible as a
                                       // click, fast enough to feel responsive.

    auto initSmoother = [&](juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>& sv,
                            float initialValue)
    {
        sv.reset(sampleRate, rampSecs);
        sv.setCurrentAndTargetValue(initialValue);
    };

    initSmoother(smoothedLowGain,  getEffectiveGainDb(Params::LOW_GAIN,  Params::LOW_MUTE));
    initSmoother(smoothedLmGain,   getEffectiveGainDb(Params::LM_GAIN,   Params::LM_MUTE));
    initSmoother(smoothedHmGain,   getEffectiveGainDb(Params::HM_GAIN,   Params::HM_MUTE));
    initSmoother(smoothedHighGain, getEffectiveGainDb(Params::HIGH_GAIN, Params::HIGH_MUTE));
    initSmoother(smoothedSatDrive, apvts.getRawParameterValue(Params::SAT_DRIVE)->load());

    parametersChanged = true; // Force initial coefficient calculation.
}

// =============================================================================
// parameterChanged  (called on the message thread by the APVTS)
// =============================================================================
void Api550bAudioProcessor::parameterChanged(const juce::String& paramID, float)
{
    parametersChanged = true;

    // For the parameters we smooth, update the smoother target immediately.
    // The audio thread will pick up the new target value within the next block.
    if      (paramID == Params::LOW_GAIN  || paramID == Params::LOW_MUTE)
        smoothedLowGain.setTargetValue( getEffectiveGainDb(Params::LOW_GAIN,  Params::LOW_MUTE));
    else if (paramID == Params::LM_GAIN   || paramID == Params::LM_MUTE)
        smoothedLmGain.setTargetValue(  getEffectiveGainDb(Params::LM_GAIN,   Params::LM_MUTE));
    else if (paramID == Params::HM_GAIN   || paramID == Params::HM_MUTE)
        smoothedHmGain.setTargetValue(  getEffectiveGainDb(Params::HM_GAIN,   Params::HM_MUTE));
    else if (paramID == Params::HIGH_GAIN || paramID == Params::HIGH_MUTE)
        smoothedHighGain.setTargetValue(getEffectiveGainDb(Params::HIGH_GAIN, Params::HIGH_MUTE));
    else if (paramID == Params::SAT_DRIVE)
        smoothedSatDrive.setTargetValue(apvts.getRawParameterValue(Params::SAT_DRIVE)->load());
}

// =============================================================================
// updateFilters — recalculates all four biquad coefficient sets.
//
// Called once per block when any parameter has changed or a smoother is still
// ramping. Per-block updates (vs per-sample) mean ~512 samples of coefficient
// interpolation latency, which is ~11 ms at 44.1 kHz — inaudible in practice
// and far cheaper than per-sample recalculation.
// =============================================================================
void Api550bAudioProcessor::updateFilters(float lowGainDb, float lmGainDb,
                                           float hmGainDb,  float highGainDb)
{
    double sr = getSampleRate();
    if (sr <= 0.0) return;

    bool propQ = apvts.getRawParameterValue(Params::Q_MODE)->load() > 0.5f;
    constexpr float fixedQ = 1.5f;

    auto makeQ = [&](float gainDb) -> float {
        return propQ ? juce::jlimit(0.7f, 2.2f, 1.0f + 0.2f * std::abs(gainDb)) : fixedQ;
    };

    auto getFreqIdx = [&](const juce::String& param, const float* table, size_t n) -> float {
        size_t idx = juce::jlimit<size_t>(0, n - 1,
            static_cast<size_t>(apvts.getRawParameterValue(param)->load()));
        return table[idx];
    };

    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    // --- Low band ---
    {
        float freq  = getFreqIdx(Params::LOW_FREQ, EQTables::lowFreq, std::size(EQTables::lowFreq));
        float q     = makeQ(lowGainDb);
        float gain  = juce::Decibels::decibelsToGain(lowGainDb);
        bool  shelf = apvts.getRawParameterValue(Params::LOW_SHELF)->load() > 0.5f;

        *lowFilter.state = shelf
            ? *Coeffs::makeLowShelf(sr, freq, q, gain)
            : *Coeffs::makePeakFilter(sr, freq, q, gain);
    }

    // --- Low-Mid band ---
    {
        float freq = getFreqIdx(Params::LM_FREQ, EQTables::lowFreq, std::size(EQTables::lowFreq));
        float q    = makeQ(lmGainDb);
        float gain = juce::Decibels::decibelsToGain(lmGainDb);

        *lowMidFilter.state = *Coeffs::makePeakFilter(sr, freq, q, gain);
    }

    // --- High-Mid band ---
    {
        float freq = getFreqIdx(Params::HM_FREQ, EQTables::hiFreq, std::size(EQTables::hiFreq));
        float q    = makeQ(hmGainDb);
        float gain = juce::Decibels::decibelsToGain(hmGainDb);

        *highMidFilter.state = *Coeffs::makePeakFilter(sr, freq, q, gain);
    }

    // --- High band ---
    {
        float freq  = getFreqIdx(Params::HIGH_FREQ, EQTables::hiFreq, std::size(EQTables::hiFreq));
        float q     = makeQ(highGainDb);
        float gain  = juce::Decibels::decibelsToGain(highGainDb);
        bool  shelf = apvts.getRawParameterValue(Params::HIGH_SHELF)->load() > 0.5f;

        // The 1.3× frequency offset for the high shelf matches the original
        // design intent (shifts the shelf knee slightly higher than the knob
        // value, approximating classic API 550B behaviour).
        *highFilter.state = shelf
            ? *Coeffs::makeHighShelf(sr, freq * 1.3f, q, gain)
            : *Coeffs::makePeakFilter(sr, freq, q, gain);
    }
}

// =============================================================================
// processBlock
// =============================================================================
void Api550bAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() == 0
        || getTotalNumInputChannels() != getTotalNumOutputChannels())
        return;

    const int numSamples = buffer.getNumSamples();

    // ------------------------------------------------------------------
    // 1. Advance all smoothers by one block.
    //    SmoothedValue::skip(n) advances n steps and returns the final
    //    value, but we call getCurrentValue() after to be explicit.
    // ------------------------------------------------------------------
    smoothedLowGain .skip(numSamples);
    smoothedLmGain  .skip(numSamples);
    smoothedHmGain  .skip(numSamples);
    smoothedHighGain.skip(numSamples);
    smoothedSatDrive.skip(numSamples);

    // Drive is read per-sample inside the static applySaturation function.
    satDriveValue = smoothedSatDrive.getCurrentValue();

    // ------------------------------------------------------------------
    // 2. Recalculate filter coefficients if anything changed.
    // ------------------------------------------------------------------
    const bool needsUpdate = parametersChanged.exchange(false)
                           || smoothedLowGain .isSmoothing()
                           || smoothedLmGain  .isSmoothing()
                           || smoothedHmGain  .isSmoothing()
                           || smoothedHighGain.isSmoothing();

    if (needsUpdate)
        updateFilters(smoothedLowGain .getCurrentValue(),
                      smoothedLmGain  .getCurrentValue(),
                      smoothedHmGain  .getCurrentValue(),
                      smoothedHighGain.getCurrentValue());

    // ------------------------------------------------------------------
    // 3. EQ stage — four stereo biquads at native sample rate.
    //    Bypassed bands are skipped entirely (no coefficient-set-to-flat
    //    trick needed; skipping is cheaper and phase-neutral).
    // ------------------------------------------------------------------
    juce::dsp::AudioBlock<float>           block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);

    auto isBypassed = [&](const juce::String& param) {
        return apvts.getRawParameterValue(param)->load() > 0.5f;
    };

    if (!isBypassed(Params::LOW_BYPASS))   lowFilter   .process(ctx);
    if (!isBypassed(Params::LM_BYPASS))    lowMidFilter.process(ctx);
    if (!isBypassed(Params::HM_BYPASS))    highMidFilter.process(ctx);
    if (!isBypassed(Params::HIGH_BYPASS))  highFilter  .process(ctx);

    // ------------------------------------------------------------------
    // 4. Saturation stage — 2× oversampled to suppress aliasing.
    //    processSamplesUp() upsamples and returns an AudioBlock referencing
    //    internal memory at 2× the block size. We process that block in
    //    place, then processSamplesDown() decimates back to native rate and
    //    writes the result into the original 'buffer'.
    // ------------------------------------------------------------------
    auto oversampledBlock = oversampler.processSamplesUp(block);
    juce::dsp::ProcessContextReplacing<float> ovCtx(oversampledBlock);
    saturation.process(ovCtx);
    oversampler.processSamplesDown(block);

    // ------------------------------------------------------------------
    // 5. Push post-processed audio to spectrum FIFO (mono sum).
    //    The editor's timer pulls this at ~30 Hz for FFT display.
    // ------------------------------------------------------------------
    const int numChannels = buffer.getNumChannels();
    for (int s = 0; s < numSamples; ++s)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getSample(ch, s);
        spectrumFifo.pushSample(mono / (float)numChannels);
    }
}

// =============================================================================
// State persistence
// =============================================================================
void Api550bAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void Api550bAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorEditor* Api550bAudioProcessor::createEditor()
{
    return new Api550bAudioProcessorEditor(*this);
}
