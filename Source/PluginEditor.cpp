#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// =============================================================================
// Colour palette — centralised so every component reads from one place.
// Based on real API 550B hardware: dark gunmetal faceplate, cream legends,
// amber LEDs for active states, red LEDs for mute.
// =============================================================================
namespace Palette
{
    // Faceplate
    const juce::Colour faceplateTop{ 0xff3a3d42 }; // brushed steel highlight
    const juce::Colour faceplateBot{ 0xff1e2025 }; // dark gunmetal shadow
    const juce::Colour panelFill{ 0xff22252a }; // individual band recesses
    const juce::Colour panelEdgeLight{ 0xff4a4e55 }; // top/left bevel
    const juce::Colour panelEdgeDark{ 0xff0d0f12 }; // bottom/right bevel

    // Text
    const juce::Colour legend{ 0xffcccccc }; // silkscreened text
    const juce::Colour legendDim{ 0xff888888 };

    // LEDs
    const juce::Colour ledAmber{ 0xffff9900 }; // shelf / q-mode "on"
    const juce::Colour ledRed{ 0xffdd2200 }; // mute "on"
    const juce::Colour ledOff{ 0xff181a1d }; // any LED "off"

    // Spectrum
    const juce::Colour specLine{ 0xcc00aaff };
    const juce::Colour specFillTop{ 0x9900ccff };
    const juce::Colour specFillBot{ 0x220055aa };
}

// =============================================================================
// ApiLookAndFeel
// =============================================================================
class ApiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Two LED colour modes so shelf/qMode buttons look different from mute.
    enum class LedMode { Amber, Red };

    // Default mode for freshly-created buttons — overridden per-button below.
    static thread_local LedMode currentLedMode;

    ApiLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId, juce::Colours::whitesmoke);
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::black.withAlpha(0.7f));
        setColour(juce::Label::textColourId, Palette::legend);
    }

    // -------------------------------------------------------------------------
    // Rotary knob — brushed-metal face with a sharp pointer dot
    // -------------------------------------------------------------------------
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, const float rotaryStartAngle,
        const float rotaryEndAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>(x, y, width, height).reduced(10.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Outer ring — subtle bevel
        g.setColour(juce::Colour(0xff090a0c));
        g.fillEllipse(bounds.expanded(2.0f));

        // Knob body — radial gradient for a domed look
        juce::ColourGradient body(
            juce::Colour(0xff4e5258), centre.getX() - radius * 0.3f, centre.getY() - radius * 0.4f,
            juce::Colour(0xff1a1c1f), centre.getX() + radius * 0.2f, centre.getY() + radius * 0.5f,
            true);
        g.setGradientFill(body);
        g.fillEllipse(bounds);

        // Highlight arc (top-left)
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawEllipse(bounds.reduced(1.5f), 1.5f);

        // Pointer dot
        juce::Path p;
        p.addEllipse(-2.0f, -(radius * 0.78f) - 2.0f, 4.0f, 4.0f);
        p.applyTransform(juce::AffineTransform::rotation(toAngle).translated(centre));
        g.setColour(juce::Colours::whitesmoke);
        g.fillPath(p);
    }

    // -------------------------------------------------------------------------
    // LED toggle button — used for mute, bypass, shelf, and Q-mode.
    // Colour is determined by a per-component property stored in the component's
    // properties bag under the key "ledMode".  This avoids needing separate
    // subclasses for each LED colour.
    // -------------------------------------------------------------------------
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
        bool /*highlighted*/, bool /*down*/) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto circleBounds = bounds.withSizeKeepingCentre(
            bounds.getHeight(), bounds.getHeight()).reduced(2.5f);

        // Read the LED colour mode stored on the button itself
        bool isAmber = button.getProperties()["ledMode"].toString() == "amber";
        juce::Colour onColour = isAmber ? Palette::ledAmber : Palette::ledRed;

        if (button.getToggleState())
        {
            // Glow corona
            g.setColour(onColour.withAlpha(0.28f));
            g.fillEllipse(circleBounds.expanded(4.0f));

            // Bright cap gradient — white centre fading to the LED colour
            juce::ColourGradient cap(
                juce::Colours::white,
                circleBounds.getCentreX(),
                circleBounds.getCentreY() - circleBounds.getHeight() * 0.35f,
                onColour.darker(0.1f),
                circleBounds.getCentreX(),
                circleBounds.getCentreY() + circleBounds.getHeight() * 0.65f,
                true);
            g.setGradientFill(cap);
            g.fillEllipse(circleBounds);

            // Tiny specular dot
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            auto specSize = circleBounds.getHeight() * 0.25f;
            g.fillEllipse(circleBounds.getCentreX() - specSize * 0.5f,
                circleBounds.getY() + circleBounds.getHeight() * 0.18f,
                specSize, specSize);
        }
        else
        {
            // Dark recess with subtle inner gradient
            juce::ColourGradient recess(
                Palette::ledOff.brighter(0.12f),
                circleBounds.getCentreX(),
                circleBounds.getCentreY() - circleBounds.getHeight() * 0.35f,
                Palette::ledOff.darker(0.4f),
                circleBounds.getCentreX(),
                circleBounds.getCentreY() + circleBounds.getHeight() * 0.65f,
                true);
            g.setGradientFill(recess);
            g.fillEllipse(circleBounds);

            // Bezel ring
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawEllipse(circleBounds, 1.0f);
        }
    }
};

// Required definition for the thread_local — not actually used for per-button
// dispatch but satisfies the linker if anything refers to it directly.
thread_local ApiLookAndFeel::LedMode ApiLookAndFeel::currentLedMode = ApiLookAndFeel::LedMode::Red;

// =============================================================================
// SpectrumAnalyzerComponent
//
// FFT spectrum display running on the GUI thread. The audio thread feeds a
// lock-free FIFO (SpectrumFifo); this component reads it at 30 Hz.
//
// SCALING FIX (previous version):
//   performFrequencyOnlyForwardTransform() returns raw bin magnitudes. For a
//   full-scale sine wave the peak bin is approximately fftSize/2 = 1024, so
//   gainToDecibels(1024) ≈ +60 dBFS — blowing out the top of the display.
//   Fix: divide each bin by (fftSize / 2) to normalise to a 0 dBFS ceiling,
//   then additionally correct for the Hann window's coherent amplitude gain
//   of 0.5 by dividing by 0.5 (i.e. multiply by 2.0). Combined factor = fftSize/4.
//   Display range -72..+6 dBFS then captures the entire useful dynamic range.
// =============================================================================
class SpectrumAnalyzerComponent : public juce::Component,
    public juce::Timer
{
public:
    explicit SpectrumAnalyzerComponent(SpectrumFifo& fifo)
        : spectrumFifo(fifo),
        forwardFFT(SpectrumFifo::fftOrder),
        window(SpectrumFifo::fftSize + 1, juce::dsp::WindowingFunction<float>::hann)
    {
        scopeData.fill(-80.0f);
        startTimerHz(30);
    }

    ~SpectrumAnalyzerComponent() override { stopTimer(); }

    void timerCallback() override
    {
        std::array<float, SpectrumFifo::fftSize> block{};
        if (!spectrumFifo.pullBlock(block))
            return;

        window.multiplyWithWindowingTable(block.data(), SpectrumFifo::fftSize);

        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::copy(block.begin(), block.end(), fftData.begin());
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

        // Normalisation factor: (fftSize/2) for FFT scaling, x2 for Hann coherent gain.
        // Net: divide by (fftSize / 4).
        const float normFactor = 1.0f / (float)(SpectrumFifo::fftSize / 4);

        const int   numBins = SpectrumFifo::fftSize / 2;
        const int   numCols = (int)scopeData.size();
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        const float assumedSr = 44100.0f;
        const float binWidth = assumedSr / (float)SpectrumFifo::fftSize;

        for (int col = 0; col < numCols; ++col)
        {
            float t = (float)col / (float)(numCols - 1);
            float freq = std::pow(10.0f, logMin + t * (logMax - logMin));
            int   bin = juce::jlimit(0, numBins - 1,
                juce::roundToInt(freq / binWidth));

            float normalised = fftData[bin] * normFactor;
            float dB = juce::Decibels::gainToDecibels(normalised, -80.0f);

            // Asymmetric smoothing: fast attack, slow release — mimics a meter
            float prev = scopeData[col];
            float alpha = (dB > prev) ? 0.5f : 0.88f;
            scopeData[col] = prev * alpha + dB * (1.0f - alpha);
        }

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        if (bounds.isEmpty()) return;

        // Background
        juce::ColourGradient bg(
            juce::Colour(0xff111316), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xff0a0c0e), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(bounds, 6.0f);

        // Border
        g.setColour(Palette::panelEdgeDark);
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

        // Grid lines at -12, -24, -48 dBFS with frequency labels
        g.setFont(juce::FontOptions(9.0f));
        for (float db : { 0.0f, -12.0f, -24.0f, -48.0f })
        {
            float y = dbToY(db, bounds);
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawHorizontalLine(juce::roundToInt(y), bounds.getX() + 1.0f, bounds.getRight() - 1.0f);
            g.setColour(Palette::legendDim);
            g.drawText(juce::String((int)db) + "dB",
                juce::Rectangle<float>(bounds.getX() + 2.0f, y - 8.0f, 30.0f, 10.0f),
                juce::Justification::left, false);
        }

        // Vertical frequency guides at 100 Hz, 1 kHz, 10 kHz
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            float t = (std::log10(hz) - logMin) / (logMax - logMin);
            float x = bounds.getX() + t * bounds.getWidth();
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawVerticalLine(juce::roundToInt(x), bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
            juce::String label = (hz >= 1000.0f)
                ? juce::String((int)(hz / 1000)) + "k"
                : juce::String((int)hz);
            g.setColour(Palette::legendDim);
            g.drawText(label,
                juce::Rectangle<float>(x + 2.0f, bounds.getBottom() - 14.0f, 24.0f, 12.0f),
                juce::Justification::left, false);
        }

        // Spectrum path
        const int numCols = (int)scopeData.size();
        juce::Path specPath;
        specPath.startNewSubPath(bounds.getX(), bounds.getBottom());

        for (int col = 0; col < numCols; ++col)
        {
            float x = bounds.getX() + (float)col / (float)(numCols - 1) * bounds.getWidth();
            float y = dbToY(scopeData[col], bounds);
            if (col == 0) specPath.lineTo(x, y);
            else          specPath.lineTo(x, y);
        }
        specPath.lineTo(bounds.getRight(), bounds.getBottom());
        specPath.closeSubPath();

        // Gradient fill
        juce::ColourGradient fill(
            Palette::specFillTop, bounds.getCentreX(), bounds.getY(),
            Palette::specFillBot, bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(specPath);

        // Bright line
        g.setColour(Palette::specLine);
        g.strokePath(specPath, juce::PathStrokeType(1.3f));
    }

private:
    float dbToY(float dB, const juce::Rectangle<float>& b) const
    {
        constexpr float minDb = -72.0f;
        constexpr float maxDb = 6.0f;
        float norm = juce::jlimit(0.0f, 1.0f, (dB - maxDb) / (minDb - maxDb));
        return b.getY() + norm * b.getHeight();
    }

    SpectrumFifo& spectrumFifo;
    juce::dsp::FFT                              forwardFFT;
    juce::dsp::WindowingFunction<float>         window;
    std::array<float, SpectrumFifo::fftSize * 2> fftData{};
    std::array<float, 512>                      scopeData{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerComponent)
};

// =============================================================================
// Helper — tag a ToggleButton with its LED colour mode so the LAF can read it.
// =============================================================================
static void setLedMode(juce::ToggleButton& b, const juce::String& mode)
{
    b.getProperties().set("ledMode", mode);
}

// =============================================================================
// Api550bAudioProcessorEditor
// =============================================================================
Api550bAudioProcessorEditor::Api550bAudioProcessorEditor(Api550bAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    laf = std::make_unique<ApiLookAndFeel>();
    setLookAndFeel(laf.get());

    // Sliders
    setupSlider(lowFreqSlider);
    setupSlider(lowMidFreqSlider);
    setupSlider(highMidFreqSlider);
    setupSlider(highFreqSlider);
    setupSlider(lowGainSlider);
    setupSlider(lowMidGainSlider);
    setupSlider(highMidGainSlider);
    setupSlider(highGainSlider);
    setupSlider(satDriveSlider);

    // Labels
    auto setupLabel = [&](juce::Label& label, const juce::String& text,
        float fontSize = 12.0f)
        {
            label.setText(text, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setFont(juce::FontOptions(fontSize));
            label.setColour(juce::Label::textColourId, Palette::legend);
            addAndMakeVisible(label);
        };

    setupLabel(lowBandLabel, "LOW", 15.0f);
    setupLabel(lowMidBandLabel, "LOW-MID", 15.0f);
    setupLabel(highMidBandLabel, "HIGH-MID", 15.0f);
    setupLabel(highBandLabel, "HIGH", 15.0f);
    setupLabel(lowMuteLabel, "MUTE", 10.0f);
    setupLabel(lowBypassLabel, "BYPASS", 10.0f);
    setupLabel(lmMuteLabel, "MUTE", 10.0f);
    setupLabel(lmBypassLabel, "BYPASS", 10.0f);
    setupLabel(hmMuteLabel, "MUTE", 10.0f);
    setupLabel(hmBypassLabel, "BYPASS", 10.0f);
    setupLabel(highMuteLabel, "MUTE", 10.0f);
    setupLabel(highBypassLabel, "BYPASS", 10.0f);
    setupLabel(satDriveLabel, "DRIVE", 13.0f);
    setupLabel(lowShelfLabel, "SHELF", 10.0f);
    setupLabel(highShelfLabel, "SHELF", 10.0f);
    setupLabel(qModeLabel, "PROP Q", 10.0f);

    // Toggle buttons
    // Mute buttons: red LED.  Shelf / Q-Mode buttons: amber LED.
    auto setupToggle = [&](juce::ToggleButton& button, const juce::String& ledMode)
        {
            setLedMode(button, ledMode);
            button.setToggleState(false, juce::dontSendNotification);
            addAndMakeVisible(button);
        };

    setupToggle(lowMuteButton, "red");
    setupToggle(lowBypassButton, "red");
    setupToggle(lmMuteButton, "red");
    setupToggle(lmBypassButton, "red");
    setupToggle(hmMuteButton, "red");
    setupToggle(hmBypassButton, "red");
    setupToggle(highMuteButton, "red");
    setupToggle(highBypassButton, "red");
    setupToggle(lowShelfButton, "amber"); // shelf active = amber, like vintage API hardware
    setupToggle(highShelfButton, "amber");
    setupToggle(qModeButton, "amber");

    // APVTS attachments
    lowFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LOW_FREQ, lowFreqSlider);
    lowGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LOW_GAIN, lowGainSlider);
    lowShelfAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LOW_SHELF, lowShelfButton);
    lowMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LOW_MUTE, lowMuteButton);
    lowBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LOW_BYPASS, lowBypassButton);

    lowMidFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LM_FREQ, lowMidFreqSlider);
    lowMidGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LM_GAIN, lowMidGainSlider);
    lmMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LM_MUTE, lmMuteButton);
    lmBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LM_BYPASS, lmBypassButton);

    highMidFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HM_FREQ, highMidFreqSlider);
    highMidGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HM_GAIN, highMidGainSlider);
    hmMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HM_MUTE, hmMuteButton);
    hmBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HM_BYPASS, hmBypassButton);

    highFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HIGH_FREQ, highFreqSlider);
    highGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HIGH_GAIN, highGainSlider);
    highShelfAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HIGH_SHELF, highShelfButton);
    highMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HIGH_MUTE, highMuteButton);
    highBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HIGH_BYPASS, highBypassButton);

    satDriveAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::SAT_DRIVE, satDriveSlider);
    qModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::Q_MODE, qModeButton);

    // Spectrum
    spectrumAnalyzer = std::make_unique<SpectrumAnalyzerComponent>(audioProcessor.spectrumFifo);
    addAndMakeVisible(spectrumAnalyzer.get());

    setSize(700, 580);
    setResizable(true, true);
    setResizeLimits(600, 500, 1200, 960);
}

Api550bAudioProcessorEditor::~Api550bAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// =============================================================================
// paint — metallic faceplate look
// =============================================================================
void Api550bAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Main faceplate gradient — dark gunmetal, top-lit
    juce::ColourGradient faceplate(
        Palette::faceplateTop, bounds.getCentreX(), bounds.getY(),
        Palette::faceplateBot, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(faceplate);
    g.fillAll();

    // Hairline top highlight — simulates angled light catching a bevelled edge
    g.setColour(juce::Colour(0xff606570));
    g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, bounds.getWidth(), 1.5f));

    // Title bar strip
    auto titleStrip = juce::Rectangle<float>(0.0f, 0.0f, bounds.getWidth(), 46.0f);
    juce::ColourGradient titleGrad(
        juce::Colour(0xff2e3138), bounds.getCentreX(), titleStrip.getY(),
        juce::Colour(0xff1c1e22), bounds.getCentreX(), titleStrip.getBottom(), false);
    g.setGradientFill(titleGrad);
    g.fillRect(titleStrip);

    // Title text — white silkscreen style
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("EQ ALPHA 3", titleStrip, juce::Justification::centred, true);

    // Manufacturer sub-text
    g.setColour(Palette::legendDim);
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("PROGRAMME EQUALISER", titleStrip.translated(0.0f, 13.0f),
        juce::Justification::centred, false);

    // Divider line below title
    g.setColour(juce::Colour(0xff0d0f12));
    g.fillRect(juce::Rectangle<float>(0.0f, 46.0f, bounds.getWidth(), 1.5f));

    // Band panel recesses — drawn here so sliders paint over them
    auto panelArea = getLocalBounds().toFloat();
    panelArea.removeFromTop(50.0f);
    panelArea.removeFromBottom(100.0f);
    panelArea.reduce(10.0f, 10.0f);

    const int numBands = 4;
    const float panelW = panelArea.getWidth() / numBands;

    for (int i = 0; i < numBands; ++i)
    {
        auto panel = panelArea.withX(panelArea.getX() + (float)i * panelW)
            .withWidth(panelW - 5.0f);

        // Inset shadow (top-left darker, simulates a pressed recess)
        juce::ColourGradient recess(
            Palette::panelFill.darker(0.15f), panel.getX(), panel.getY(),
            Palette::panelFill.brighter(0.05f), panel.getX(), panel.getBottom(), false);
        g.setGradientFill(recess);
        g.fillRoundedRectangle(panel, 8.0f);

        // Inner bevel — dark top/left edge
        g.setColour(Palette::panelEdgeDark);
        g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);
        // Light bottom/right edge
        g.setColour(Palette::panelEdgeLight.withAlpha(0.35f));
        g.drawRoundedRectangle(panel.translated(1.0f, 1.0f).reduced(1.5f), 8.0f, 0.75f);
    }

    // Spectrum area border
    auto specBorder = getLocalBounds().toFloat();
    specBorder.removeFromTop((float)getHeight() - 100.0f);
    specBorder.reduce(10.0f, 5.0f);
    g.setColour(Palette::panelEdgeDark);
    g.drawRoundedRectangle(specBorder, 6.0f, 1.0f);
}

// =============================================================================
// resized
// =============================================================================
void Api550bAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50); // title

    auto spectrumArea = bounds.removeFromBottom(100);
    spectrumArea.reduce(10, 5);
    spectrumAnalyzer->setBounds(spectrumArea);

    bounds.reduce(15, 15);

    const int numBands = 4;
    const int bandWidth = (bounds.getWidth() - (numBands - 1) * 10) / numBands;

    // layoutBand places all controls for one EQ band into the provided column.
    // The extraControl (shelf button, drive knob, q-mode) sits at the bottom
    // below the mute/bypass row, with its own label beneath it.
    auto layoutBand = [&](juce::Rectangle<int> area,
        juce::Label& bandLabel,
        juce::Slider& freqSlider,
        juce::Slider& gainSlider,
        juce::Button* muteButton, juce::Label* muteLabel,
        juce::Button* bypassButton, juce::Label* bypassLabel,
        juce::Component* extraControl,
        juce::Label* extraLabel)
        {
            const int labelH = 22;
            const int knobSize = 68;
            const int textBoxH = 18;
            const int sliderH = knobSize + textBoxH;
            const int buttonSize = 18;
            const int btnLabelH = 14;
            const int spacing = 8;

            // Band name
            bandLabel.setBounds(area.removeFromTop(labelH));
            area.removeFromTop(spacing);

            // Frequency knob
            freqSlider.setBounds(area.removeFromTop(sliderH)
                .withSizeKeepingCentre(knobSize, sliderH));
            area.removeFromTop(spacing);

            // Gain knob
            gainSlider.setBounds(area.removeFromTop(sliderH)
                .withSizeKeepingCentre(knobSize, sliderH));
            area.removeFromTop(spacing);

            // Mute + Bypass LED row
            if (muteButton && bypassButton)
            {
                auto buttonRow = area.removeFromTop(buttonSize + btnLabelH);
                int halfW = buttonRow.getWidth() / 2;

                auto muteArea = buttonRow.removeFromLeft(halfW);
                muteButton->setBounds(muteArea.removeFromTop(buttonSize)
                    .withSizeKeepingCentre(buttonSize, buttonSize));
                if (muteLabel) muteLabel->setBounds(muteArea);

                auto bypassArea = buttonRow;
                bypassButton->setBounds(bypassArea.removeFromTop(buttonSize)
                    .withSizeKeepingCentre(buttonSize, buttonSize));
                if (bypassLabel) bypassLabel->setBounds(bypassArea);

                area.removeFromTop(spacing);
            }

            // Extra control (shelf toggle, drive knob, or q-mode toggle)
            if (extraControl)
            {
                bool isSlider = (dynamic_cast<juce::Slider*>(extraControl) != nullptr);
                if (isSlider)
                {
                    // Drive knob with label above
                    if (extraLabel) extraLabel->setBounds(area.removeFromTop(labelH));
                    extraControl->setBounds(area.removeFromTop(sliderH)
                        .withSizeKeepingCentre(knobSize, sliderH));
                }
                else
                {
                    // LED toggle button — centred, with label below
                    auto toggleRow = area.removeFromTop(buttonSize);
                    extraControl->setBounds(toggleRow.withSizeKeepingCentre(buttonSize, buttonSize));
                    if (extraLabel)
                        extraLabel->setBounds(area.removeFromTop(btnLabelH));
                }
            }
        };

    for (int i = 0; i < numBands; ++i)
    {
        auto column = bounds.withX(bounds.getX() + i * (bandWidth + 10))
            .withWidth(bandWidth);
        if (i == 0)
            layoutBand(column, lowBandLabel, lowFreqSlider, lowGainSlider,
                &lowMuteButton, &lowMuteLabel, &lowBypassButton, &lowBypassLabel,
                &lowShelfButton, &lowShelfLabel);
        else if (i == 1)
            layoutBand(column, lowMidBandLabel, lowMidFreqSlider, lowMidGainSlider,
                &lmMuteButton, &lmMuteLabel, &lmBypassButton, &lmBypassLabel,
                &satDriveSlider, &satDriveLabel);
        else if (i == 2)
            layoutBand(column, highMidBandLabel, highMidFreqSlider, highMidGainSlider,
                &hmMuteButton, &hmMuteLabel, &hmBypassButton, &hmBypassLabel,
                &qModeButton, &qModeLabel);
        else
            layoutBand(column, highBandLabel, highFreqSlider, highGainSlider,
                &highMuteButton, &highMuteLabel, &highBypassButton, &highBypassLabel,
                &highShelfButton, &highShelfLabel);
    }
}

// =============================================================================
void Api550bAudioProcessorEditor::setupSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    addAndMakeVisible(slider);
}