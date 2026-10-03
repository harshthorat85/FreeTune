/*
  ==============================================================================
    FreeTune v3.3 - Editor
    Sleek dark design: 3D knobs, recessed tuning meter, one-octave note keyboard.
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Palette and type, matching the approved mockup
namespace ftui
{
    const juce::Colour ink      { 0xffE6EAF0 };   // primary text
    const juce::Colour dim      { 0xff98A2B0 };   // captions
    const juce::Colour tickText { 0xff8A93A1 };   // meter scale numbers
    const juce::Colour line     { 0xff323947 };   // panel dividers
    const juce::Colour edge     { 0xff3A4250 };   // control outlines
    const juce::Colour amber    { 0xffF5A524 };   // what FreeTune does (correction)
    const juce::Colour ice      { 0xff6CC7E8 };   // what the singer does (input pitch)

    // Bahnschrift ships with Windows 10/11; other systems fall back to the default sans.
    juce::Font font (float size, bool bold = false);
}

//==============================================================================
class FreeTuneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FreeTuneLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

//==============================================================================
// Pill-shaped on/off button with an LED (Classic, Formant)
class LedToggle : public juce::Button
{
public:
    explicit LedToggle (const juce::String& name);
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

//==============================================================================
// Arc meter: ice needle = singer's pitch, amber arc/dot = correction and result
class TuningMeter : public juce::Component
{
public:
    void setReading (float inputCents, float outputCents, int noteMidi, bool voiced);
    void paint (juce::Graphics&) override;

private:
    float inCents = 0.0f, outCents = 0.0f;
    int   noteMidi = -1;
    bool  voiced = false;
};

//==============================================================================
// One-octave keyboard: lit keys are in the scale, click to add/remove a note
class PianoKeys : public juce::Component
{
public:
    std::function<bool (int)> isOn;       // pitch class (C = 0) -> in scale?
    std::function<void (int)> onToggle;   // a key was clicked

    void setTarget (int pitchClass);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int target = -1;
    static juce::Rectangle<float> whiteRect (int index);   // index 0..6
    static juce::Rectangle<float> blackRect (int index);   // index 0..4
    int keyAt (juce::Point<float>) const;
};

//==============================================================================
class FreeTuneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit FreeTuneAudioProcessorEditor (FreeTuneAudioProcessor&);
    ~FreeTuneAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  value;
        std::unique_ptr<SliderAtt> attachment;
        std::function<juce::String (double)> format;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& title,
                    bool big, bool bipolar, std::function<juce::String (double)> format);
    void updateValueLabels();

    // Notes / scales
    bool noteOn (int pitchClass) const;
    void setNote (int pitchClass, bool on);
    bool notesMatch (int key, int scaleType) const;
    void applyScale();
    int  noteMask() const;

    FreeTuneAudioProcessor& audioProcessor;
    FreeTuneLookAndFeel lnf;

    Knob speed, amount, flex, humanize, vibrato, shift, mix;

    juce::ComboBox keyBox, scaleBox, voiceBox;
    std::unique_ptr<ComboAtt> keyAtt, scaleAtt, voiceAtt;

    LedToggle classicBtn { "Classic" }, formantBtn { "Formant" };
    std::unique_ptr<ButtonAtt> classicAtt, formantAtt;

    TuningMeter meter;
    PianoKeys   piano;

    bool applyingPreset = false;
    int  lastNoteMask = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeTuneAudioProcessorEditor)
};
