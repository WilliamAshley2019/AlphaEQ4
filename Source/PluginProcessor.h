#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>

// =============================================================================
// Parameter IDs
// =============================================================================
namespace Params
{
    inline const juce::String LOW_FREQ   { "LOW_FREQ"   };
    inline const juce::String LOW_GAIN   { "LOW_GAIN"   };
    inline const juce::String LOW_SHELF  { "LOW_SHELF"  };
    inline const juce::String LOW_MUTE   { "LOW_MUTE"   };
    inline const juce::String LOW_BYPASS { "LOW_BYPASS" };
    inline const juce::String LM_FREQ    { "LM_FREQ"    };
    inline const juce::String LM_GAIN    { "LM_GAIN"    };
    inline const juce::String LM_MUTE    { "LM_MUTE"    };
    inline const juce::String LM_BYPASS  { "LM_BYPASS"  };
    inline const juce::String HM_FREQ    { "HM_FREQ"    };
    inline const juce::String HM_GAIN    { "HM_GAIN"    };
    inline const juce::String HM_MUTE    { "HM_MUTE"    };
    inline const juce::String HM_BYPASS  { "HM_BYPASS"  };
    inline const juce::String HIGH_FREQ  { "HIGH_FREQ"  };
    inline const juce::String HIGH_GAIN  { "HIGH_GAIN"  };
    inline const juce::String HIGH_SHELF { "HIGH_SHELF" };
    inline const juce::String HIGH_MUTE  { "HIGH_MUTE"  };
    inline const juce::String HIGH_BYPASS{ "HIGH_BYPASS"};
    inline const juce::String SAT_DRIVE  { "SAT_DRIVE"  };
    inline const juce::String Q_MODE     { "Q_MODE"     };
}

// Lookup tables shared between processor code
namespace EQTables
{
    constexpr float gainDb[]  = { -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 9.f, 12.f };
    constexpr float lowFreq[] = { 40.f, 75.f, 150.f, 300.f, 600.f, 1200.f, 2400.f };
    constexpr float hiFreq[]  = { 800.f, 1500.f, 3000.f, 5000.f, 7000.f, 10000.f, 12500.f };
}

// =============================================================================
// SpectrumFifo
//
// Lock-free double-buffer for handing post-processed audio to the GUI thread.
// Audio thread pushes individual samples; GUI thread pulls a full 2048-sample
// block at ~30 Hz for FFT analysis. One missed update = one skipped repaint,
// never a corruption.
// =============================================================================
struct SpectrumFifo
{
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder; // 2048 samples

    // Called from audio thread.
    void pushSample(float s) noexcept
    {
        fifo[writeIdx++] = s;
        if (writeIdx >= fftSize)
        {
            writeIdx = 0;
            // Only overwrite the pending block if the GUI has already consumed
            // the previous one — this is intentional frame-drop, not data loss.
            if (!blockReady.load(std::memory_order_relaxed))
            {
                pendingBlock = fifo;
                blockReady.store(true, std::memory_order_release);
            }
        }
    }

    // Called from GUI thread.
    bool pullBlock(std::array<float, fftSize>& dest) noexcept
    {
        if (!blockReady.load(std::memory_order_acquire))
            return false;
        dest = pendingBlock;
        blockReady.store(false, std::memory_order_release);
        return true;
    }

private:
    std::array<float, fftSize> fifo{};
    std::array<float, fftSize> pendingBlock{};
    int writeIdx = 0;
    std::atomic<bool> blockReady{ false };
};

// =============================================================================
// Api550bAudioProcessor
// =============================================================================
class Api550bAudioProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    Api550bAudioProcessor();
    ~Api550bAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { oversampler.reset(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "EQAlpha3"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Post-processed audio FIFO — read by SpectrumAnalyzerComponent in the editor.
    SpectrumFifo spectrumFifo;

private:
    // -------------------------------------------------------------------------
    // FIX: The original code used juce::dsp::IIR::Filter<float> directly.
    // That class processes exactly ONE channel. On a stereo AudioBlock it only
    // touches channel 0, leaving channel 1 completely unprocessed — a silent
    // bug. ProcessorDuplicator wraps a mono filter and calls it once per
    // channel, sharing the same coefficient object.
    // -------------------------------------------------------------------------
    using StereoFilter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    StereoFilter lowFilter, lowMidFilter, highMidFilter, highFilter;

    // -------------------------------------------------------------------------
    // Oversampling — applied ONLY around the waveshaper, not the linear EQ.
    // tanh() at full rate aliases back into the audible band above ~5 kHz at
    // high drive settings. 2× (factor = 1 → 2^1) is enough to push the alias
    // products above 40 kHz where they are filtered by the decimation LP.
    // Cost: ~2× CPU for the saturation stage alone, not the whole signal chain.
    // -------------------------------------------------------------------------
    juce::dsp::Oversampling<float> oversampler{
        2u, 1u,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        false };  // false = lower latency IIR anti-aliasing filter

    // Waveshaper (runs at 2× rate inside the oversampled block)
    static float satDriveValue; // written and read exclusively on the audio thread
    static float applySaturation(float x);
    juce::dsp::WaveShaper<float> saturation;

    // -------------------------------------------------------------------------
    // Parameter smoothing
    //
    // Why: juce::AudioParameterChoice uses integer indices. Every time the user
    // clicks a step, the new gain immediately snaps to the target, causing a
    // discontinuity that sounds like a click or zipper noise. Smoothing the
    // effective gain value in dB over 100 ms eliminates this at the cost of
    // one per-block coefficient recalculation (cheap vs. the filter itself).
    //
    // NOTE: We smooth the effective gain AFTER applying mute so that muting
    // also ramps smoothly to 0 dB (flat) instead of stepping.
    // -------------------------------------------------------------------------
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLowGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLmGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedHmGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedHighGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSatDrive;

    // Returns the effective gain in dB for a band, accounting for its mute state.
    float getEffectiveGainDb(const juce::String& gainParam,
                             const juce::String& muteParam) const;

    std::atomic<bool> parametersChanged{ true };
    void parameterChanged(const juce::String& paramID, float newValue) override;

    // Recalculates all biquad coefficients using the provided smoothed gain values.
    void updateFilters(float lowGainDb, float lmGainDb,
                       float hmGainDb,  float highGainDb);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Api550bAudioProcessor)
};
