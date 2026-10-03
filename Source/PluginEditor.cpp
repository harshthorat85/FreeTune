/*
  ==============================================================================
    FreeTune v3.3 - Editor
  ==============================================================================
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Note toggle parameters are stored starting at A; the UI works in pitch classes (C = 0).
    const char* const kNoteParamIds[12] = { "noteA", "noteBb", "noteB", "noteC", "noteDb", "noteD",
                                            "noteEb", "noteE", "noteF", "noteGb", "noteG", "noteAb" };
    int paramIndexFor (int pitchClass) { return (pitchClass + 3) % 12; }

    const int kWhitePcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int kBlackPcs[5] = { 1, 3, 6, 8, 10 };

    juce::String noteName (int pitchClass)
    {
        // UTF-8 sharps and flats
        static const char* const names[12] = { "C", "C\xe2\x99\xaf", "D", "E\xe2\x99\xad", "E", "F",
                                               "F\xe2\x99\xaf", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
        return juce::String (juce::CharPointer_UTF8 (names[((pitchClass % 12) + 12) % 12]));
    }

    juce::String minusSign() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }

    // type: 0 = major, 1 = minor, 2 = chromatic
    void scaleNotes (int key, int type, bool out[12])
    {
        static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
        for (int i = 0; i < 12; ++i)
            out[i] = (type == 2);
        if (type == 0 || type == 1)
        {
            const int* intervals = (type == 0) ? major : minor;
            for (int i = 0; i < 7; ++i)
                out[(key + intervals[i]) % 12] = true;
        }
    }

    juce::Path strokeOf (const juce::Path& source, float width)
    {
        juce::Path stroked;
        juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, source);
        return stroked;
    }

    // Fill a path with a soft glow of the same colour around it
    void glowFill (juce::Graphics& g, const juce::Path& path, juce::Colour colour, int radius)
    {
        juce::DropShadow (colour.withAlpha (0.85f), radius, {}).drawForPath (g, path);
        g.setColour (colour);
        g.fillPath (path);
    }
}

juce::Font ftui::font (float size, bool bold)
{
    return juce::Font (juce::FontOptions ("Bahnschrift", size, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
// Look and feel
//==============================================================================
FreeTuneLookAndFeel::FreeTuneLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, ftui::ink);
    setColour (juce::ComboBox::arrowColourId, ftui::dim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff222832));
    setColour (juce::PopupMenu::textColourId, ftui::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff33405A));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, ftui::ink);
}

void FreeTuneLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    // The knob is drawn 20 px smaller than its bounds so the drop shadow has room.
    const float size   = (float) juce::jmin (width, height) - 20.0f;
    const auto  centre = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).getCentre();
    const float r      = size * 0.5f - 9.0f;                 // value-arc radius
    const float track  = size > 100.0f ? 6.0f : 4.0f;
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);
    const bool  bipolar = (bool) slider.getProperties()["bipolar"];
    const float from   = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    // Groove
    juce::Path groove;
    groove.addCentredArc (centre.x, centre.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff0E1116));
    g.strokePath (groove, juce::PathStrokeType (track + 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Glowing value arc
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        glowFill (g, strokeOf (arc, track), ftui::amber, 6);
    }

    // Bevelled rim with drop shadow
    const float rimR = r - 5.0f, faceR = r - 9.0f;
    juce::Path rim;
    rim.addEllipse (centre.x - rimR, centre.y - rimR, rimR * 2.0f, rimR * 2.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.65f), juce::roundToInt (size * 0.09f),
                      { 0, juce::roundToInt (size * 0.045f) }).drawForPath (g, rim);
    juce::ColourGradient rimGrad (juce::Colour (0xff666F82), centre.x, centre.y - rimR,
                                  juce::Colour (0xff0F1216), centre.x, centre.y + rimR, false);
    rimGrad.addColour (0.5, juce::Colour (0xff2A303A));
    g.setGradientFill (rimGrad);
    g.fillPath (rim);

    // Face, lit from the top left
    const juce::Point<float> light (centre.x - faceR * 0.24f, centre.y - faceR * 0.4f);
    juce::ColourGradient faceGrad (juce::Colour (0xff4B5466), light,
                                   juce::Colour (0xff1B2028), light + juce::Point<float> (faceR * 1.6f, 0.0f), true);
    faceGrad.addColour (0.55, juce::Colour (0xff2C333F));
    g.setGradientFill (faceGrad);
    g.fillEllipse (centre.x - faceR, centre.y - faceR, faceR * 2.0f, faceR * 2.0f);

    // Soft highlight
    const juce::Point<float> shineAt (centre.x - faceR * 0.3f, centre.y - faceR * 0.56f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.2f), shineAt,
                                             juce::Colours::transparentWhite, shineAt + juce::Point<float> (faceR * 0.9f, 0.0f), true));
    g.fillEllipse (centre.x - faceR, centre.y - faceR, faceR * 2.0f, faceR * 2.0f);

    // Pointer
    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (r * 0.42f, angle));
    pointer.lineTo (centre.getPointOnCircumference (r * 0.78f, angle));
    g.setColour (juce::Colour (0xffF2F5F9));
    g.strokePath (pointer, juce::PathStrokeType (size > 100.0f ? 4.0f : 3.0f,
                                                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void FreeTuneLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                        int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    juce::ignoreUnused (isButtonDown, buttonX, buttonY, buttonW, buttonH);
    const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2C3340), 0.0f, 0.0f,
                                             juce::Colour (0xff1F252E), 0.0f, (float) height, false));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (b.getX() + 6.0f, b.getY() + 1.0f, b.getWidth() - 12.0f, 1.0f);
    g.setColour (box.hasKeyboardFocus (true) ? ftui::ice : ftui::edge);
    g.drawRoundedRectangle (b, 6.0f, 1.0f);

    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (ftui::dim);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void FreeTuneLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font FreeTuneLookAndFeel::getComboBoxFont (juce::ComboBox&) { return ftui::font (15.0f); }
juce::Font FreeTuneLookAndFeel::getPopupMenuFont()                { return ftui::font (15.0f); }

//==============================================================================
// LED toggle
//==============================================================================
LedToggle::LedToggle (const juce::String& name) : juce::Button (name)
{
    setClickingTogglesState (true);
    setTitle (name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LedToggle::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    juce::ignoreUnused (isDown);
    const bool  on  = getToggleState();
    const auto  b   = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = b.getHeight() * 0.5f;
    juce::Path pill;
    pill.addRoundedRectangle (b, rad);

    if (on)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1A1F27), 0.0f, b.getY(),
                                                 juce::Colour (0xff283040), 0.0f, b.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2F3644), 0.0f, b.getY(),
                                                 juce::Colour (0xff1F242C), 0.0f, b.getBottom(), false));
    g.fillPath (pill);

    if (on)
    {
        // Pressed in: inner shadow along the top edge
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (pill);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, b.getY(),
                                                 juce::Colours::transparentBlack, 0.0f, b.getY() + 8.0f, false));
        g.fillRect (b);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (b.getX() + rad, b.getY() + 1.0f, b.getWidth() - 2.0f * rad, 1.0f);
    }

    g.setColour (on ? ftui::amber : ftui::edge.brighter (isHighlighted ? 0.35f : 0.0f));
    g.drawRoundedRectangle (b, rad, 1.0f);

    const auto led = juce::Rectangle<float> (b.getX() + 16.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
    juce::Path ledPath;
    ledPath.addEllipse (led);
    if (on)
        glowFill (g, ledPath, ftui::amber, 8);
    else
    {
        g.setColour (ftui::edge);
        g.fillPath (ledPath);
    }

    g.setColour (ftui::ink);
    g.setFont (ftui::font (15.0f));
    g.drawText (getName(), b.withLeft (led.getRight() + 8.0f).withTrimmedRight (12.0f),
                juce::Justification::centredLeft, false);
}

//==============================================================================
// Tuning meter
//==============================================================================
void TuningMeter::setReading (float inputCents, float outputCents, int midi, bool isVoiced)
{
    if (std::abs (inputCents - inCents) < 0.05f && std::abs (outputCents - outCents) < 0.05f
        && midi == noteMidi && isVoiced == voiced)
        return;
    inCents = inputCents;
    outCents = outputCents;
    noteMidi = midi;
    voiced = isVoiced;
    repaint();
}

void TuningMeter::paint (juce::Graphics& g)
{
    const auto  b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();

    juce::Path screen;
    screen.addRoundedRectangle (b.reduced (0.5f), 14.0f);

    // Recessed screen
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff171C24), w * 0.5f, h,
                                             juce::Colour (0xff0B0E12), w * 0.5f, -h * 0.1f, true));
    g.fillPath (screen);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (screen);

        // Inner shadow along the top edge
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), 0.0f, 0.0f,
                                                 juce::Colours::transparentBlack, 0.0f, 22.0f, false));
        g.fillRect (b);

        // Scale (geometry from the mockup's 560 x 300 drawing space)
        const float s = w / 560.0f, oy = 8.0f;
        const float cx = 280.0f * s, cy = 285.0f * s + oy, R = 224.0f;
        auto ang = [] (float cents) { return juce::degreesToRadians (juce::jlimit (-50.0f, 50.0f, cents) / 50.0f * 70.0f); };
        auto pt  = [&] (float radius, float a) { return juce::Point<float> (cx + radius * s * std::sin (a), cy - radius * s * std::cos (a)); };
        auto arc = [&] (float radius, float a0, float a1)
        {
            juce::Path p;
            p.addCentredArc (cx, cy, radius * s, radius * s, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
            return p;
        };

        g.setColour (juce::Colour (0xff2E3542));
        g.strokePath (arc (R, ang (-50.0f), ang (50.0f)), juce::PathStrokeType (2.0f));
        g.setColour (ftui::ink.withAlpha (0.14f));
        g.strokePath (arc (R, ang (-5.0f), ang (5.0f)), juce::PathStrokeType (14.0f * s));

        juce::Path ticks;
        for (int c = -50; c <= 50; c += 5)
        {
            const float len = (c % 25 == 0) ? 16.0f : 7.0f;
            ticks.startNewSubPath (pt (R + 8.0f, ang ((float) c)));
            ticks.lineTo (pt (R + 8.0f + len, ang ((float) c)));
        }
        g.setColour (juce::Colour (0xff5A6372));
        g.strokePath (ticks, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        struct TickLabel { float x, y; juce::String text; };
        const TickLabel labels[] = { { 30.0f, 194.0f, minusSign() + "50" }, { 127.0f, 67.0f, minusSign() + "25" },
                                     { 280.0f, 19.0f, "0" }, { 433.0f, 67.0f, "+25" }, { 530.0f, 194.0f, "+50" } };
        g.setColour (ftui::tickText);
        g.setFont (ftui::font (13.0f));
        for (const auto& l : labels)
            g.drawText (l.text, juce::Rectangle<float> (l.x * s - 20.0f, l.y * s + oy - 8.0f, 40.0f, 16.0f),
                        juce::Justification::centred, false);

        // Correction arc and output dot (amber), singer's pitch needle (ice)
        const float ea = ang (inCents), oa = ang (outCents);
        if (voiced && std::abs (ea - oa) > juce::degreesToRadians (0.3f))
            glowFill (g, strokeOf (arc (R - 18.0f, ea, oa), 6.0f * s), ftui::amber, 6);

        const auto dp = pt (R - 18.0f, oa);
        juce::Path dot;
        dot.addEllipse (dp.x - 7.0f * s, dp.y - 7.0f * s, 14.0f * s, 14.0f * s);
        juce::Path needle;
        needle.startNewSubPath (pt (150.0f, ea));
        needle.lineTo (pt (R + 2.0f, ea));
        const auto needleShape = strokeOf (needle, 3.0f);

        if (voiced)
        {
            glowFill (g, dot, ftui::amber, 8);
            glowFill (g, needleShape, ftui::ice, 6);
        }
        else
        {
            g.setColour (ftui::amber.withAlpha (0.3f));
            g.fillPath (dot);
            g.setColour (ftui::ice.withAlpha (0.3f));
            g.fillPath (needleShape);
        }

        // Readout
        const juce::String noteText = (voiced && noteMidi >= 0)
            ? noteName (noteMidi % 12) + juce::String (noteMidi / 12 - 1)
            : juce::String ("--");
        const auto noteArea = juce::Rectangle<float> (0.0f, h - 129.0f, w, 83.0f);
        g.setFont (ftui::font (92.0f, true));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawText (noteText, noteArea.translated (0.0f, 3.0f), juce::Justification::centred, false);
        g.setColour (ftui::ink);
        g.drawText (noteText, noteArea, juce::Justification::centred, false);

        auto cents = [] (float c)
        {
            const int r = juce::roundToInt (c);
            if (r == 0) return juce::String ("0");
            return (r > 0 ? juce::String ("+") : minusSign()) + juce::String (std::abs (r));
        };
        const auto row = juce::Rectangle<float> (0.0f, h - 38.0f, w, 18.0f);
        g.setFont (ftui::font (15.0f));
        g.setColour (ftui::ice);
        g.drawText (voiced ? "Input " + cents (inCents) + " cents" : juce::String ("Input --"),
                    row.withRight (w * 0.5f - 12.0f), juce::Justification::centredRight, false);
        g.setColour (ftui::amber);
        g.drawText (voiced ? "Output " + cents (outCents) + " cents" : juce::String ("Output --"),
                    row.withLeft (w * 0.5f + 12.0f), juce::Justification::centredLeft, false);

        // Glass highlight
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, 0.0f,
                                                 juce::Colours::transparentWhite, 0.0f, h * 0.36f, false));
        g.fillRect (b);
    }

    g.setColour (juce::Colour (0xff2A303B));
    g.strokePath (screen, juce::PathStrokeType (1.0f));
}

//==============================================================================
// Piano keys
//==============================================================================
juce::Rectangle<float> PianoKeys::whiteRect (int index) { return { (float) index * 40.0f, 0.0f, 39.0f, 124.0f }; }

juce::Rectangle<float> PianoKeys::blackRect (int index)
{
    static const float lefts[5] = { 28.0f, 68.0f, 148.0f, 188.0f, 228.0f };
    return { lefts[index], 0.0f, 24.0f, 76.0f };
}

void PianoKeys::setTarget (int pitchClass)
{
    if (pitchClass != target)
    {
        target = pitchClass;
        repaint();
    }
}

int PianoKeys::keyAt (juce::Point<float> p) const
{
    for (int i = 0; i < 5; ++i)
        if (blackRect (i).contains (p)) return kBlackPcs[i];
    for (int i = 0; i < 7; ++i)
        if (whiteRect (i).contains (p)) return kWhitePcs[i];
    return -1;
}

void PianoKeys::mouseDown (const juce::MouseEvent& e)
{
    const int pc = keyAt (e.position);
    if (pc >= 0 && onToggle)
        onToggle (pc);
}

void PianoKeys::paint (juce::Graphics& g)
{
    // White keys
    for (int i = 0; i < 7; ++i)
    {
        const int  pc = kWhitePcs[i];
        const bool on = isOn ? isOn (pc) : true;
        const auto r  = whiteRect (i);
        juce::Path key;
        key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 6.0f, 6.0f, false, false, true, true);

        juce::ColourGradient grad (on ? juce::Colour (0xffBFC7D2) : juce::Colour (0xff262C35), 0.0f, r.getY(),
                                   on ? juce::Colour (0xffC6CDD7) : juce::Colour (0xff343B46), 0.0f, r.getBottom(), false);
        if (on) { grad.addColour (0.22, juce::Colour (0xffE9EDF2)); grad.addColour (0.85, juce::Colour (0xffDCE2EA)); }
        else    { grad.addColour (0.30, juce::Colour (0xff3A414C)); }
        g.setGradientFill (grad);
        g.fillPath (key);

        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (key);
            g.setColour (juce::Colours::black.withAlpha (on ? 0.14f : 0.3f));    // key lip
            g.fillRect (r.withTop (r.getBottom() - 6.0f));
            g.setColour (juce::Colours::white.withAlpha (on ? 0.7f : 0.06f));
            g.fillRect (r.withHeight (1.0f));
        }

        g.setColour (juce::Colour (0xff12161C));
        g.strokePath (key, juce::PathStrokeType (1.0f));

        if (pc == target)
        {
            g.setColour (ftui::amber);
            g.fillRoundedRectangle (r.getCentreX() - 10.0f, r.getBottom() - 32.0f, 20.0f, 4.0f, 2.0f);
        }
        g.setColour (on ? juce::Colour (0xff1A1E25) : juce::Colour (0xffA9B2BF));
        g.setFont (ftui::font (12.0f, true));
        g.drawText (noteName (pc), r.withTop (r.getBottom() - 22.0f).withHeight (14.0f), juce::Justification::centred, false);
    }

    // Black keys (with shadows onto the white keys)
    for (int i = 0; i < 5; ++i)
    {
        const int  pc = kBlackPcs[i];
        const bool on = isOn ? isOn (pc) : true;
        const auto r  = blackRect (i);
        juce::Path key;
        key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4.0f, 4.0f, false, false, true, true);

        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 8, { 0, 5 }).drawForPath (g, key);
        juce::ColourGradient grad (on ? juce::Colour (0xff46536A) : juce::Colour (0xff0C0F13), 0.0f, r.getY(),
                                   on ? juce::Colour (0xff536078) : juce::Colour (0xff171B21), 0.0f, r.getBottom(), false);
        grad.addColour (0.8, on ? juce::Colour (0xff6A7891) : juce::Colour (0xff262B34));
        g.setGradientFill (grad);
        g.fillPath (key);

        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (key);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRect (r.withTop (r.getBottom() - 4.0f));
        }
        g.setColour (juce::Colour (0xff0B0E12));
        g.strokePath (key, juce::PathStrokeType (1.0f));

        if (pc == target)
        {
            g.setColour (ftui::amber);
            g.fillRoundedRectangle (r.getCentreX() - 6.0f, r.getBottom() - 11.0f, 12.0f, 4.0f, 2.0f);
        }
    }
}

//==============================================================================
// Editor
//==============================================================================
FreeTuneAudioProcessorEditor::FreeTuneAudioProcessorEditor (FreeTuneAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lnf);

    auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };

    setupKnob (speed, "speed", "Retune speed", true, false, [this] (double v)
    {
        if (audioProcessor.apvts.getRawParameterValue ("classic")->load() > 0.5f)
            return juce::String ("Instant");
        const double ms = std::pow (v / 100.0, 2.0) * 200.0;      // same curve as the DSP
        return ms < 3.0 ? juce::String ("Instant") : juce::String (juce::roundToInt (ms)) + " ms";
    });
    setupKnob (amount,   "amount",   "Correction",      true,  false, percent);
    setupKnob (flex,     "flex",     "Flex-tune",       false, false, percent);
    setupKnob (humanize, "humanize", "Humanize",        false, false, [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; });
    setupKnob (vibrato,  "vibrato",  "Natural vibrato", false, false, percent);
    setupKnob (shift,    "shift",    "Shift",           false, true,  [] (double v)
    {
        const int st = juce::roundToInt (v);
        if (st == 0) return juce::String ("0 st");
        return (st > 0 ? juce::String ("+") : minusSign()) + juce::String (std::abs (st)) + " st";
    });
    setupKnob (mix,      "mix",      "Mix",             false, false, percent);

    // Header selectors
    juce::StringArray keys;
    for (int pc = 0; pc < 12; ++pc)
        keys.add (noteName (pc));
    auto setupCombo = [this] (juce::ComboBox& box, const juce::String& title, const juce::StringArray& items)
    {
        box.addItemList (items, 1);
        box.setTitle (title);
        addAndMakeVisible (box);
    };
    setupCombo (keyBox,   "Key",   keys);
    setupCombo (scaleBox, "Scale", { "Major", "Minor", "Chromatic", "Custom" });
    setupCombo (voiceBox, "Voice", { "Soprano", "Alto", "Tenor", "Baritone", "Bass" });
    keyAtt   = std::make_unique<ComboAtt> (audioProcessor.apvts, "key",       keyBox);
    scaleAtt = std::make_unique<ComboAtt> (audioProcessor.apvts, "scaleType", scaleBox);
    voiceAtt = std::make_unique<ComboAtt> (audioProcessor.apvts, "range",     voiceBox);

    addAndMakeVisible (classicBtn);
    addAndMakeVisible (formantBtn);
    classicAtt = std::make_unique<ButtonAtt> (audioProcessor.apvts, "classic", classicBtn);
    formantAtt = std::make_unique<ButtonAtt> (audioProcessor.apvts, "fcorr",   formantBtn);

    addAndMakeVisible (meter);

    piano.isOn = [this] (int pc) { return noteOn (pc); };
    piano.onToggle = [this] (int pc)
    {
        setNote (pc, ! noteOn (pc));
        if (! notesMatch (keyBox.getSelectedItemIndex(), scaleBox.getSelectedItemIndex()))
            scaleBox.setSelectedItemIndex (3, juce::sendNotificationSync);       // "Custom"
        lastNoteMask = noteMask();
        piano.repaint();
    };
    addAndMakeVisible (piano);

    // Projects saved before the Key/Scale selectors existed: make the labels match the notes.
    {
        const int k = keyBox.getSelectedItemIndex(), t = scaleBox.getSelectedItemIndex();
        if (t >= 0 && t <= 2 && ! notesMatch (k, t))
        {
            int foundKey = -1, foundType = -1;
            for (int type = 0; type <= 2 && foundKey < 0; ++type)
                for (int key = 0; key < 12; ++key)
                    if (notesMatch (key, type)) { foundKey = key; foundType = type; break; }

            if (foundKey >= 0)
            {
                keyBox.setSelectedItemIndex (foundKey, juce::sendNotificationSync);
                scaleBox.setSelectedItemIndex (foundType, juce::sendNotificationSync);
            }
            else
                scaleBox.setSelectedItemIndex (3, juce::sendNotificationSync);
        }
    }

    // Wired up last, so the attachments' initial sync can't overwrite the saved notes.
    keyBox.onChange   = [this] { applyScale(); };
    scaleBox.onChange = [this] { applyScale(); };

    lastNoteMask = noteMask();
    updateValueLabels();
    setSize (960, 600);
    startTimerHz (30);
}

FreeTuneAudioProcessorEditor::~FreeTuneAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void FreeTuneAudioProcessorEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& title,
                                              bool big, bool bipolar, std::function<juce::String (double)> format)
{
    auto& s = k.slider;
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (200);
    s.setTitle (title);
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    s.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (s);

    k.attachment = std::make_unique<SliderAtt> (audioProcessor.apvts, paramId, s);
    if (auto* param = audioProcessor.apvts.getParameter (paramId))
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    k.format = std::move (format);
    k.value.setFont (ftui::font (big ? 26.0f : 16.0f, true));
    k.value.setJustificationType (juce::Justification::centred);
    k.value.setColour (juce::Label::textColourId, ftui::ink);
    k.value.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (k.value);
}

void FreeTuneAudioProcessorEditor::updateValueLabels()
{
    for (auto* k : { &speed, &amount, &flex, &humanize, &vibrato, &shift, &mix })
    {
        const auto text = k->format ? k->format (k->slider.getValue()) : juce::String();
        if (k->value.getText() != text)
            k->value.setText (text, juce::dontSendNotification);
    }
}

bool FreeTuneAudioProcessorEditor::noteOn (int pitchClass) const
{
    return audioProcessor.apvts.getRawParameterValue (kNoteParamIds[paramIndexFor (pitchClass)])->load() > 0.5f;
}

void FreeTuneAudioProcessorEditor::setNote (int pitchClass, bool on)
{
    if (auto* param = audioProcessor.apvts.getParameter (kNoteParamIds[paramIndexFor (pitchClass)]))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

bool FreeTuneAudioProcessorEditor::notesMatch (int key, int scaleType) const
{
    if (scaleType < 0 || scaleType > 2)
        return false;
    bool want[12];
    scaleNotes (juce::jmax (0, key), scaleType, want);
    for (int pc = 0; pc < 12; ++pc)
        if (noteOn (pc) != want[pc])
            return false;
    return true;
}

void FreeTuneAudioProcessorEditor::applyScale()
{
    const int type = scaleBox.getSelectedItemIndex();
    if (type < 0 || type > 2)
        return;                                   // "Custom": leave the notes as they are
    bool want[12];
    scaleNotes (juce::jmax (0, keyBox.getSelectedItemIndex()), type, want);
    for (int pc = 0; pc < 12; ++pc)
        if (noteOn (pc) != want[pc])
            setNote (pc, want[pc]);
    lastNoteMask = noteMask();
    piano.repaint();
}

int FreeTuneAudioProcessorEditor::noteMask() const
{
    int mask = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (noteOn (pc)) mask |= (1 << pc);
    return mask;
}

void FreeTuneAudioProcessorEditor::timerCallback()
{
    auto& p = audioProcessor;
    const int  target  = p.meterTargetMidi.load();
    const bool voiced  = p.meterVoiced.load() && target >= 0;
    const int  shiftSt = juce::roundToInt (p.apvts.getRawParameterValue ("shift")->load());

    meter.setReading (p.meterInputCents.load(), p.meterOutputCents.load(),
                      target >= 0 ? target + shiftSt : -1, voiced);
    piano.setTarget (voiced ? target % 12 : -1);

    const int mask = noteMask();
    if (mask != lastNoteMask)                     // notes changed by automation or a preset
    {
        lastNoteMask = mask;
        piano.repaint();
    }
    updateValueLabels();
}

//==============================================================================
void FreeTuneAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();

    // Window background: slate blue fading to deep navy
    {
        const float a = juce::degreesToRadians (160.0f);
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        const float len = std::abs (w * dir.x) + std::abs (h * dir.y);
        const juce::Point<float> c (w * 0.5f, h * 0.5f);
        juce::ColourGradient bg (juce::Colour (0xff33476A), c - dir * (len * 0.5f),
                                 juce::Colour (0xff111827), c + dir * (len * 0.5f), false);
        bg.addColour (0.42, juce::Colour (0xff1F2B40));
        g.setGradientFill (bg);
        g.fillAll();
    }

    // Soft blue glow behind the meter row
    {
        const juce::Point<float> c (w * 0.5f, 64.0f + 336.0f * 0.55f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x295A96E6), c,
                                                 juce::Colour (0x005A96E6), c + juce::Point<float> (330.0f, 0.0f), true));
        g.fillRect (0.0f, 64.0f, w, 336.0f);
    }

    // Header bar with a shadow onto the main area
    g.setColour (juce::Colour (0xff05050E));
    g.fillRect (0.0f, 0.0f, w, 64.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, 64.0f,
                                             juce::Colours::transparentBlack, 0.0f, 82.0f, false));
    g.fillRect (0.0f, 64.0f, w, 18.0f);
    g.setColour (ftui::line);
    g.fillRect (0.0f, 63.0f, w, 1.0f);

    // Bottom panel, lit top edge
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, 376.0f,
                                             juce::Colours::black.withAlpha (0.3f), 0.0f, 400.0f, false));
    g.fillRect (0.0f, 376.0f, w, 24.0f);
    g.setColour (juce::Colour (0x59090C10));
    g.fillRect (0.0f, 400.0f, w, h - 400.0f);
    g.setColour (ftui::line);
    g.fillRect (0.0f, 400.0f, w, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (0.0f, 401.0f, w, 1.0f);

    // Wordmark and build stamp
    g.setColour (ftui::ink);
    g.setFont (ftui::font (30.0f, true));
    g.drawText ("FreeTune", 24, 12, 130, 40, juce::Justification::centredLeft, false);
    g.setColour (ftui::dim);
    g.setFont (ftui::font (13.0f));
    g.drawText ("v3.3", 158, 17, 80, 16, juce::Justification::centredLeft, false);
    g.setFont (ftui::font (11.0f));
    g.drawText (juce::String ("built ") + __DATE__, 158, 33, 84, 14, juce::Justification::centredLeft, false);

    // Header captions
    g.setFont (ftui::font (14.0f));
    g.drawText ("Key",   246, 14, 30, 36, juce::Justification::centredLeft, false);
    g.drawText ("Scale", 366, 14, 40, 36, juce::Justification::centredLeft, false);
    g.drawText ("Voice", 542, 14, 42, 36, juce::Justification::centredLeft, false);

    // Big knob captions
    g.setFont (ftui::font (15.0f));
    g.drawText ("Retune speed", 24,  312, 196, 20, juce::Justification::centred, false);
    g.drawText ("Correction",   740, 312, 196, 20, juce::Justification::centred, false);

    // Notes panel
    g.setColour (ftui::ink);
    g.setFont (ftui::font (15.0f, true));
    g.drawText ("Notes", 24, 416, 140, 22, juce::Justification::centredLeft, false);
    g.setColour (ftui::dim);
    g.setFont (ftui::font (13.0f));
    g.drawText ("Click to add or remove", 24, 416, 280, 22, juce::Justification::centredRight, false);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 16, { 0, 8 })
        .drawForRectangle (g, juce::Rectangle<int> (24, 448, 280, 124));

    // Small knob captions (colour set again: the shadow above leaves its own colour on g)
    g.setColour (ftui::dim);
    g.setFont (ftui::font (13.0f));
    const char* const captions[5] = { "Flex-tune", "Humanize", "Natural vibrato", "Shift", "Mix" };
    const float colW = (596.0f - 32.0f) / 5.0f;
    for (int i = 0; i < 5; ++i)
        g.drawText (captions[i], juce::Rectangle<float> (340.0f + (float) i * (colW + 8.0f), 514.0f, colW, 18.0f),
                    juce::Justification::centred, false);
}

void FreeTuneAudioProcessorEditor::resized()
{
    // Header
    keyBox.setBounds   (278, 14,  72, 36);
    scaleBox.setBounds (406, 14, 120, 36);
    voiceBox.setBounds (584, 14, 110, 36);
    classicBtn.setBounds (724, 14,  96, 36);
    formantBtn.setBounds (830, 14, 106, 36);

    // Main row: big knobs either side of the meter (knob bounds include room for shadows)
    speed.slider.setBounds  (42,  120, 160, 160);
    amount.slider.setBounds (758, 120, 160, 160);
    speed.value.setBounds   (24,  276, 196, 32);
    amount.value.setBounds  (740, 276, 196, 32);
    meter.setBounds (240, 76, 480, 312);

    // Bottom panel
    piano.setBounds (24, 448, 280, 124);
    const float colW = (596.0f - 32.0f) / 5.0f;
    Knob* smalls[5] = { &flex, &humanize, &vibrato, &shift, &mix };
    for (int i = 0; i < 5; ++i)
    {
        const float colX = 340.0f + (float) i * (colW + 8.0f);
        const int   cx   = juce::roundToInt (colX + colW * 0.5f);
        smalls[i]->slider.setBounds (cx - 44, 408, 88, 88);
        smalls[i]->value.setBounds (juce::roundToInt (colX), 490, juce::roundToInt (colW), 22);
    }
}
