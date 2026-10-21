#include "PluginEditor.h"

namespace riq
{

namespace
{
    constexpr double kSliceBeats[] = { 1.0, 0.5, 0.25, 0.125 };

    // Peak of what slice table `info` plays at loop phase `phase` of a stem.
    float stemPeakAt (StemEngine& st, const std::array<SliceDecision, StemEngine::kMaxSlices>& info, int n,
                      double phase, double L, double S, double ref)
    {
        const int j = juce::jlimit (0, n - 1, (int) (phase / S));
        const auto& d = info[(size_t) j];
        if (d.silent)
            return 0.0f;
        const double seg = S / d.ratchet;
        double o = std::fmod (phase - j * S, seg);
        if (d.rev)
            o = seg - o;
        const double src = srcBeatFor (d.src * S + o, ref, L);
        const int bin = (int) (wrapBeat (src, (double) StemEngine::kCaptureBeats) * StemEngine::kBinsPerBeat) % StemEngine::kBins;
        return juce::jmin (1.0f, st.peaks[(size_t) bin].load (std::memory_order_relaxed));
    }

    void readTable (StemEngine& st, std::array<SliceDecision, StemEngine::kMaxSlices>& info, int n)
    {
        for (int j = 0; j < n; ++j)
            info[(size_t) j] = SliceDecision::unpack (st.sliceInfo[(size_t) j].load (std::memory_order_relaxed));
    }

    void setParamValue (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
    {
        if (auto* p = state.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    setColour (juce::ComboBox::backgroundColourId, Colours::panelHi);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::line);
    setColour (juce::ComboBox::arrowColourId, Colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent);
    setColour (juce::TextButton::buttonColourId, Colours::panelHi);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::BubbleComponent::backgroundColourId, Colours::panel);
    setColour (juce::BubbleComponent::outlineColourId, Colours::line);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panel);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float r = radius - 4.0f;

    g.setColour (Colours::bg);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

    // Segmented ring, Rhythmiq style.
    constexpr int segs = 28;
    for (int i = 0; i < segs; ++i)
    {
        const float a0 = start + (end - start) * (float) i / segs;
        const float a1 = start + (end - start) * ((float) i + 0.7f) / segs;
        juce::Path seg;
        seg.addCentredArc (centre.x, centre.y, r, r, 0.0f, a0, a1, true);
        g.setColour (Colours::line);
        g.strokePath (seg, juce::PathStrokeType (4.0f));
    }

    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (colour);
    g.strokePath (value, juce::PathStrokeType (4.0f));

    // Ghost ring: the value the Beat Assistant is actually using right now.
    const float ghost = (float) s.getProperties().getWithDefault ("ghost", -1.0f);
    if (ghost >= 0.0f)
    {
        const float ga = start + ghost * (end - start);
        juce::Path gp;
        gp.addCentredArc (centre.x, centre.y, r - 7.0f, r - 7.0f, 0.0f, start, ga, true);
        g.setColour (Colours::accent);
        g.strokePath (gp, juce::PathStrokeType (3.0f));
    }

    g.setColour (Colours::panelHi);
    g.fillEllipse (juce::Rectangle<float> ((r - 11.0f) * 2.0f, (r - 11.0f) * 2.0f).withCentre (centre));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -(r - 12.0f), 3.0f, (r - 12.0f) * 0.5f, 1.5f);
    g.setColour (Colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (! b.isEnabled())
        c = c.withMultipliedAlpha (0.4f);
    if (down)
        c = c.brighter (0.25f);
    else if (over)
        c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Colours::dim);
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) h * 0.5f), juce::Font::bold));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.0f));
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& cap,
            juce::Colour colour, bool onTop)
    : caption (cap), captionOnTop (onTop)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    slider.setBounds (captionOnTop ? r.withTrimmedTop (15) : r.withTrimmedBottom (15));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (caption, captionOnTop ? r.removeFromTop (15) : r.removeFromBottom (15), juce::Justification::centred);
}

Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        addItemList (p->choices, 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, *this);
}

Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
    : attachment (*state.getParameter (paramId), [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        items = p->choices;
    attachment.sendInitialUpdate();
}

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colours::bg);
    g.fillRoundedRectangle (r, 4.0f);

    const float w = r.getWidth() / (float) juce::jmax (1, items.size());
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (int i = 0; i < items.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (1.0f);
        if (i == selected)
        {
            g.setColour (Colours::text);
            g.fillRoundedRectangle (cell, 3.0f);
        }
        g.setColour (i == selected ? Colours::bg : Colours::dim);
        g.drawText (items[i], cell, juce::Justification::centred);
    }
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (items.isEmpty())
        return;
    const int i = juce::jlimit (0, items.size() - 1, (int) (e.position.x / (float) getWidth() * (float) items.size()));
    attachment.setValueAsCompleteGesture ((float) i);
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text, juce::Colour onColour)
    : juce::TextButton (text)
{
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, onColour);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
}

Momentary::Momentary (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text)
    : juce::TextButton (text), param (state.getParameter (paramId))
{
}

void Momentary::buttonStateChanged()
{
    const bool isDown = getState() == juce::Button::buttonDown;
    if (isDown == down || param == nullptr)
        return;
    down = isDown;
    param->beginChangeGesture();
    param->setValueNotifyingHost (down ? 1.0f : 0.0f);
    param->endChangeGesture();
}

//==============================================================================
void WaveView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101216));
    g.fillRoundedRectangle (bounds, 4.0f);

    const bool playing = proc.transportPlaying.load();
    const float w = bounds.getWidth(), h = bounds.getHeight(), mid = h * 0.5f;

    std::array<std::array<SliceDecision, StemEngine::kMaxSlices>, kNumStems> info;
    std::array<int, kNumStems> n {};
    std::array<double, kNumStems> L {}, S {}, ref {};
    const int first = stem < 0 ? 0 : stem, last = stem < 0 ? kNumStems - 1 : stem;
    for (int s = first; s <= last; ++s)
    {
        auto& st = proc.getStem (s);
        n[(size_t) s] = juce::jmax (1, st.dispSlices.load());
        L[(size_t) s] = st.dispLoop.load();
        S[(size_t) s] = st.dispSlice.load();
        ref[(size_t) s] = st.dispRef.load();
        readTable (st, info[(size_t) s], n[(size_t) s]);
    }

    auto& base = proc.getStem (first);
    const double loop = L[(size_t) first], slice = S[(size_t) first], bpb = base.dispBeatsPerBar.load();
    const int nSl = n[(size_t) first];
    const int playingSlice = base.dispPlaying.load();

    if (stem >= 0 && playing && playingSlice >= 0 && playingSlice < nSl)
    {
        g.setColour (Colours::panelHi);
        g.fillRect ((float) (playingSlice * slice / loop) * w, 0.0f, (float) (slice / loop) * w, h);
    }

    const int cols = juce::jmax (1, (int) w);
    columnPeak.resize ((size_t) cols);
    columnAltered.resize ((size_t) cols);
    float maxPk = 0.0f;
    for (int x = 0; x < cols; ++x)
    {
        const double phase = (x + 0.5) / w * loop;
        float pk = 0.0f;
        bool altered = false;
        for (int s = first; s <= last; ++s)
        {
            if (stem < 0 && proc.getStem (s).dispLevel.load() <= 0.0f && proc.sourceOk[s].load() == 0)
                continue;
            pk += stemPeakAt (proc.getStem (s), info[(size_t) s], n[(size_t) s], phase, L[(size_t) s], S[(size_t) s], ref[(size_t) s]);
            const int j = juce::jlimit (0, n[(size_t) s] - 1, (int) (phase / S[(size_t) s]));
            altered = altered || info[(size_t) s][(size_t) j].altered;
        }
        columnPeak[(size_t) x] = pk;
        columnAltered[(size_t) x] = altered;
        maxPk = juce::jmax (maxPk, pk);
    }

    // Normalised to the loudest point so hot material (kicks at 0 dBFS) still shows its shape.
    const float target = 1.0f / juce::jmax (0.02f, maxPk);
    displayGain = displayGain <= 0.0f ? target : displayGain + (target - displayGain) * 0.25f;

    for (int x = 0; x < cols; ++x)
    {
        const float amp = juce::jmin (1.0f, columnPeak[(size_t) x] * displayGain) * (mid - 3.0f);
        g.setColour (columnAltered[(size_t) x] ? Colours::accent : (stem >= 0 ? Colours::stem[stem] : Colours::text).withAlpha (0.85f));
        g.drawVerticalLine (x, mid - amp, mid + amp + 1.0f);
    }

    for (int j = 1; j < nSl; ++j)
    {
        const double b = j * slice;
        const bool bar = wrapBeat (b, bpb) < 1.0e-6;
        const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
        if (stem < 0 && ! beat)
            continue;
        g.setColour (bar ? Colours::text.withAlpha (0.5f) : beat ? Colours::dim.withAlpha (0.5f) : Colours::line.withAlpha (0.6f));
        g.drawVerticalLine ((int) ((float) (b / loop) * w), 0.0f, h);
    }

    if (stem >= 0)
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int j = 0; j < nSl; ++j)
        {
            const auto& d = info[(size_t) stem][(size_t) j];
            if (! d.altered)
                continue;
            const float x0 = (float) (j * slice / loop) * w, sw = (float) (slice / loop) * w;
            g.setColour (Colours::accent);
            g.fillRect (x0 + 1.0f, h - 3.0f, sw - 2.0f, 3.0f);
            if (sw > 13.0f && (d.ratchet > 1 || d.rev))
                g.drawText (d.ratchet > 1 ? "x" + juce::String (d.ratchet) : "R",
                            juce::Rectangle<float> (x0, 1.0f, sw, 11.0f), juce::Justification::centred);
        }
    }

    if (playing)
    {
        g.setColour (Colours::accent);
        g.drawVerticalLine ((int) ((float) (base.dispPhase.load() / loop) * w), 0.0f, h);
    }

    if (stem >= 0)
    {
        const int slot = (int) proc.apvts.getRawParameterValue (ID::stem (stem, "src"))->load();
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + slot));
        juce::String msg;
        if (proc.dropped[stem].load() != 0)
            msg = "DROPPED (AUTOPILOT)";
        else if (! RiqLiveProcessor::isSlotFed (slot) && base.dispState.load() != (int) CaptureState::frozen)
            msg = "NO RiQ SEND ON SLOT " + letter;
        else if (proc.sourceOk[stem].load() == 0 && base.dispState.load() != (int) CaptureState::frozen)
            msg = playing ? "SLOT " + letter + " SILENT" : "TRANSPORT STOPPED";

        if (msg.isNotEmpty())
        {
            g.setColour (Colours::dim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (msg, getLocalBounds().reduced (6, 3), juce::Justification::centredRight);
        }
    }
}

//==============================================================================
MasterDisplay::MasterDisplay (RiqLiveProcessor& p)
    : proc (p), fillSize (p.apvts, ID::fillSize), loopLen (p.apvts, ID::loopLen), wave (p, -1)
{
    addAndMakeVisible (fillSize);
    addAndMakeVisible (loopLen);
    addAndMakeVisible (wave);
    fillSize.setTooltip ("Keyboard fill slice size");
    loopLen.setTooltip ("Loop length");
}

void MasterDisplay::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (22);
    fillSize.setBounds (top.removeFromLeft (200));
    loopLen.setBounds (top.withSizeKeepingCentre (240, 22));
    r.removeFromTop (6);
    r.removeFromBottom (22);
    wave.setBounds (r);
}

void MasterDisplay::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    const auto waveR = wave.getBounds();
    const auto& st = proc.getStem (0);
    const double L = st.dispLoop.load(), bpb = st.dispBeatsPerBar.load();

    // Bar numbers
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (Colours::text);
    for (int b = 0; b < (int) std::round (L / bpb); ++b)
        g.drawText (juce::String (b + 1), waveR.getX() + (int) (b * bpb / L * waveR.getWidth()) + 3, waveR.getY() + 2, 20, 12,
                    juce::Justification::centredLeft);

    // Keyboard fill strip: one cell per fill slice, labelled with its MIDI key.
    const double fs = kSliceBeats[juce::jlimit (0, 3, (int) proc.apvts.getRawParameterValue (ID::fillSize)->load())];
    const int cells = juce::jmax (1, (int) std::round (L / fs));
    const int baseNote = (int) proc.apvts.getRawParameterValue (ID::fillNote)->load();
    const auto mask = proc.fillMask.load();
    const auto strip = juce::Rectangle<int> (waveR.getX(), waveR.getBottom() + 3, waveR.getWidth(), 18);
    const float cw = (float) strip.getWidth() / (float) cells;

    for (int c = 0; c < cells; ++c)
    {
        const auto cell = juce::Rectangle<float> (strip.getX() + c * cw, (float) strip.getY(), cw, (float) strip.getHeight()).reduced (0.5f);
        const bool mapped = c < 16;
        const bool held = mapped && ((mask >> c) & 1u);
        const bool black = juce::MidiMessage::isMidiNoteBlack (baseNote + c);
        g.setColour (held ? Colours::accent : black ? Colours::bg : Colours::panelHi);
        g.fillRect (cell);
        if (mapped && cw > 22.0f)
        {
            g.setColour (held ? juce::Colours::black : Colours::dim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::MidiMessage::getMidiNoteName (baseNote + c, true, true, 3), cell, juce::Justification::centred);
        }
    }

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("BPM " + juce::String (proc.hostBpm.load(), 1), getLocalBounds().reduced (10, 6).removeFromTop (22),
                juce::Justification::centredRight);
}

//==============================================================================
StemColumn::StemColumn (RiqLiveProcessor& p, int s)
    : proc (p), stem (s),
      source (p.apvts, ID::stem (s, "src")),
      slice (p.apvts, ID::stem (s, "slice")),
      wave (p, s),
      hold (p.apvts, ID::stem (s, "roll"), "FREEZE"),
      capture (p.apvts, ID::stem (s, "freeze"), "IN", Colours::frozen),
      arrange (p.apvts, ID::stem (s, "arrange"), "ARRANGE", Colours::stem[s]),
      reverse (p.apvts, ID::stem (s, "reverse"), "REVERSE", Colours::stem[s]),
      silence (p.apvts, ID::stem (s, "silence"), "SILENCE", Colours::stem[s]),
      ratchet (p.apvts, ID::stem (s, "ratchet"), "RATCHET", Colours::stem[s]),
      pan (p.apvts, ID::stem (s, "pan"), "PAN", Colours::dim),
      gain (p.apvts, ID::stem (s, "gain"), "GAIN", Colours::dim),
      evolveArr (p.apvts, ID::stem (s, "evolveArr"), "EVOLVE"),
      evolveRev (p.apvts, ID::stem (s, "evolveRev"), "EVOLVE"),
      on (p.apvts, ID::stem (s, "on"), "ON", Colours::text),
      solo (p.apvts, ID::stem (s, "solo"), "SOLO", Colours::armed),
      out (p.apvts, ID::stem (s, "out"))
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &source, &slice, &wave, &hold, &capture, &arrange, &reverse, &silence, &ratchet,
             &pan, &gain, &evolveArr, &evolveRev, &on, &solo, &out })
        addAndMakeVisible (c);

    hold.setTooltip ("Hold: repeats the currently playing slice (Rhythmiq Freeze).");
    capture.setTooltip ("IN: the track streams live into the buffer. Click to FREEZE the buffer contents (quantised by Freeze Q).");
    ratchet.slider.setTooltip ("Subdivides weak-beat slices into 2/3/4 retriggers.");
    out.setTooltip ("Mix: stem plays on the main output. Own: only on its own output (Stem " + juce::String (s + 1) + ").");

    // Like Rhythmiq: touching a stem knob hands control back from the Beat Assistant.
    arrange.slider.onDragStart = [this]
    {
        if (proc.apvts.getRawParameterValue (ID::stem (stem, "link"))->load() > 0.5f)
            setParamValue (proc.apvts, ID::iqArrange, 0.0f);
    };
    reverse.slider.onDragStart = [this]
    {
        if (proc.apvts.getRawParameterValue (ID::stem (stem, "link"))->load() > 0.5f)
            setParamValue (proc.apvts, ID::iqReverse, 0.0f);
    };
}

void StemColumn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 6.0f);

    auto header = r.removeFromTop (30.0f);
    g.setColour (juce::Colour (0xff0d0e11));
    g.fillRoundedRectangle (header, 6.0f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("STEM " + juce::String (stem + 1), header.reduced (10.0f, 0.0f), juce::Justification::centredLeft);

    // Which track feeds this stem.
    const int slot = (int) proc.apvts.getRawParameterValue (ID::stem (stem, "src"))->load();
    const bool fed = RiqLiveProcessor::isSlotFed (slot);
    const auto trackName = RiqLiveProcessor::getSlotTrackName (slot);
    g.setColour (fed ? Colours::live : Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (fed ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) + (trackName.isNotEmpty() ? trackName : "connected")
                    : juce::String ("no send"),
                juce::Rectangle<int> (134, 0, source.getX() - 140, 30), juce::Justification::centredLeft);

    const float lvl = juce::jmin (1.0f, proc.getStem (stem).dispLevel.load());
    g.setColour (Colours::line);
    g.fillRect (76, 12, 50, 6);
    g.setColour (Colours::stem[stem]);
    g.fillRect (76, 12, (int) (50 * std::sqrt (lvl)), 6);

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("OUT", out.getX() - 30, out.getY(), 28, out.getHeight(), juce::Justification::centredRight);
}

void StemColumn::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (30).reduced (8, 4);
    source.setBounds (header.removeFromRight (100));

    r.reduce (10, 8);
    slice.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    wave.setBounds (r.removeFromTop (78));
    r.removeFromTop (4);
    hold.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    capture.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (58);
    auto evo = r.removeFromBottom (24);
    const int kw = r.getWidth() / 4;
    int i = 0;
    for (auto* k : { &arrange, &reverse, &silence, &ratchet })
        k->setBounds (r.withX (r.getX() + kw * i++).withWidth (kw).withSizeKeepingCentre (kw, juce::jmin (r.getHeight(), kw + 14)));
    evolveArr.setBounds (evo.withX (evo.getX()).withWidth (kw).reduced (8, 0));
    evolveRev.setBounds (evo.withX (evo.getX() + kw).withWidth (kw).reduced (8, 0));

    bottom.removeFromTop (4);
    auto btns = bottom.removeFromLeft (120);
    on.setBounds (btns.removeFromLeft (56).withSizeKeepingCentre (56, 28));
    btns.removeFromLeft (6);
    solo.setBounds (btns.withSizeKeepingCentre (btns.getWidth(), 28));
    auto knobs = bottom.removeFromRight (110);
    pan.setBounds (knobs.removeFromLeft (55));
    gain.setBounds (knobs);
    bottom.removeFromLeft (36);
    out.setBounds (bottom.withSizeKeepingCentre (juce::jmin (bottom.getWidth() - 4, 90), 24));
}

void StemColumn::refresh()
{
    const auto state = (CaptureState) proc.getStem (stem).dispState.load();
    const auto now = juce::Time::getMillisecondCounter();
    const bool blink = (now / 250) % 2 == 0;

    if (! capture.getToggleState())
    {
        capture.setButtonText ("IN  -  LIVE TO BUFFER");
        capture.setColour (juce::TextButton::buttonColourId, Colours::live.withAlpha (0.3f));
    }
    else if (state == CaptureState::armed)
    {
        capture.setButtonText ("FREEZING ON NEXT " + juce::String (proc.apvts.getRawParameterValue (ID::freezeQ)->load() >= 3.0f ? "LOOP" : "STEP"));
        capture.setColour (juce::TextButton::buttonOnColourId, blink ? Colours::armed : Colours::armed.darker (0.6f));
    }
    else
    {
        capture.setButtonText ("FROZEN  -  CLICK FOR IN");
        capture.setColour (juce::TextButton::buttonOnColourId, Colours::frozen);
    }

    // Beat Assistant feedback: live value rings, locked Evolve buttons, IQ Evolve flashes.
    const bool linked = proc.apvts.getRawParameterValue (ID::stem (stem, "link"))->load() > 0.5f;
    const bool iqArr = proc.apvts.getRawParameterValue (ID::iqArrange)->load() > 0.0f;
    const bool iqRev = proc.apvts.getRawParameterValue (ID::iqReverse)->load() > 0.5f;
    const bool iqEvo = proc.apvts.getRawParameterValue (ID::iqEvolve)->load() > 0.5f;
    auto setGhost = [] (Knob& k, bool show, float v)
    {
        const float g = show ? v : -1.0f;
        if ((float) k.slider.getProperties().getWithDefault ("ghost", -1.0f) != g)
        {
            k.slider.getProperties().set ("ghost", g);
            k.slider.repaint();
        }
    };
    setGhost (arrange, linked && iqArr, proc.effArrange[stem].load());
    setGhost (ratchet, linked && iqArr, proc.effRatchet[stem].load());
    setGhost (reverse, linked && iqRev, proc.effReverse[stem].load());

    evolveArr.setEnabled (! (linked && iqEvo));
    evolveRev.setEnabled (! (linked && iqEvo));
    const int ec = proc.evolveCount[stem].load();
    if (ec != lastEvolveCount)
    {
        lastEvolveCount = ec;
        flashUntil = now + 300;
    }
    const bool flash = now < flashUntil;
    evolveArr.setToggleState (flash, juce::dontSendNotification);
    evolveRev.setToggleState (flash, juce::dontSendNotification);

    wave.repaint();
    repaint (70, 0, juce::jmax (0, getWidth() - 180), 30);
}

//==============================================================================
SendPanel::SendPanel (RiqLiveProcessor& p)
    : proc (p), slot (p.apvts, ID::slot), thru (p.apvts, ID::sendThru, "THRU WHILE PLAYING", Colours::accent)
{
    addAndMakeVisible (slot);
    addAndMakeVisible (thru);
}

void SendPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    auto r = getLocalBounds().reduced (24);

    const int s = juce::jlimit (0, 2, proc.getSendSlot());
    const auto colour = Colours::stem[s];

    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("RiQ LIVE SEND", r.removeFromTop (28), juce::Justification::centredLeft);

    r.removeFromTop (44);
    g.setColour (colour);
    g.setFont (juce::FontOptions (54.0f, juce::Font::bold));
    g.drawText ("SLOT " + juce::String::charToString ((juce::juce_wchar) ('A' + s)), r.removeFromTop (64), juce::Justification::centred);

    auto meter = r.removeFromTop (22).withSizeKeepingCentre (360, 12);
    g.setColour (Colours::panelHi);
    g.fillRoundedRectangle (meter.toFloat(), 4.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (meter.withWidth ((int) (meter.getWidth() * juce::jmin (1.0f, std::sqrt (proc.sendLevel.load())))).toFloat(), 4.0f);

    r.removeFromTop (12);
    const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + s));
    if (proc.poolFull.load())
    {
        auto box = r.removeFromTop (60).reduced (10, 4);
        g.setColour (juce::Colour (0xffe03b3b));
        g.fillRoundedRectangle (box.toFloat(), 6.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawFittedText ("TOO MANY RiQ SENDS (MAX " + juce::String (SharedBusData::kMaxSends)
                          + ")\nThis track is NOT being sent", box, juce::Justification::centred, 2);
        return;
    }

    const int count = RiqLiveProcessor::getSlotSendCount (s);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("Streams this track into RiQ Live (stem source \"Send " + letter + "\")."
                      + (count > 1 ? "\nMixed on this slot: " + RiqLiveProcessor::getSlotTrackName (s) : juce::String())
                      + "\nWhile playing the track is muted here (RiQ Live plays it); stopped, it passes through.",
                      r.removeFromTop (70), juce::Justification::centred, 4);
}

void SendPanel::resized()
{
    auto r = getLocalBounds().reduced (24);
    slot.setBounds (r.removeFromTop (28).removeFromRight (150));
    thru.setBounds (getLocalBounds().removeFromBottom (60).withSizeKeepingCentre (200, 30));
}

} // namespace riq

//==============================================================================
using namespace riq;

RiqLiveEditor::RiqLiveEditor (RiqLiveProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      protect (p.apvts, ID::protect), freezeQ (p.apvts, ID::freezeQ), sceneQ (p.apvts, ID::sceneQ),
      masterGain (p.apvts, ID::masterGain, "GAIN", Colours::dim),
      limiter (p.apvts, ID::limiter, "LIMIT", Colours::accent),
      master (p),
      iqArrange (p.apvts, ID::iqArrange, "IQ ARRANGE", Colours::accent),
      iqEvolve (p.apvts, ID::iqEvolve, "IQ EVOLVE", Colours::accent),
      iqReverse (p.apvts, ID::iqReverse, "IQ REVERSE", Colours::accent),
      apPhrase (p.apvts, ID::apPhrase), apEvolveEvery (p.apvts, ID::apEvolveEvery),
      apFill (p.apvts, ID::apFill, "FILL", Colours::accent),
      apMotion (p.apvts, ID::apMotion, "MOTION", Colours::accent),
      apRatchet (p.apvts, ID::apRatchet, "FILL RATCHET", Colours::accent),
      apEvolveChance (p.apvts, ID::apEvolveChance, "EVOLVE CHANCE", Colours::accent),
      apRevDepth (p.apvts, ID::apRevDepth, "REV DEPTH", Colours::accent),
      apDrop (p.apvts, ID::apDrop, "DROP", Colours::accent)
{
    setLookAndFeel (&lnf);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &protect, &freezeQ, &sceneQ, &masterGain, &limiter, &master, &iqArrange, &iqEvolve, &iqReverse,
             &apPhrase, &apEvolveEvery, &apFill, &apMotion, &apRatchet, &apEvolveChance, &apRevDepth, &apDrop })
        addAndMakeVisible (c);

    iqArrange.slider.setTooltip ("Takes over the Arrange knobs of linked stems: motion every bar, fills at phrase ends.");
    iqEvolve.setTooltip ("Presses the stem Evolve buttons by itself (see Autopilot: Evolve Every / Chance).");
    iqReverse.setTooltip ("Reverses some slices of linked stems, alternating bars.");
    apDrop.slider.setTooltip ("Chance that a linked stem sits out a bar (one always keeps playing).");

    const char* letters[] = { "A", "B", "C" };
    for (int s = 0; s < kNumStems; ++s)
    {
        links[(size_t) s] = std::make_unique<Toggle> (p.apvts, ID::stem (s, "link"), letters[s], Colours::stem[s]);
        links[(size_t) s]->setTooltip ("Link stem " + juce::String (s + 1) + " to the Beat Assistant");
        addAndMakeVisible (*links[(size_t) s]);
        columns[(size_t) s] = std::make_unique<StemColumn> (p, s);
        addAndMakeVisible (*columns[(size_t) s]);
    }

    for (int i = 0; i < kNumScenes; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        b.setButtonText (i == 0 ? "RESET" : juce::String (i));
        b.onClick = [this, i]
        {
            if (i > 0 && (saveButton.getToggleState() || juce::ModifierKeys::currentModifiers.isShiftDown()))
            {
                proc.saveScene (i);
                saveButton.setToggleState (false, juce::dontSendNotification);
            }
            else
            {
                proc.requestScene (i);
            }
            updateSceneButtons();
        };
        addAndMakeVisible (b);
    }
    saveButton.setClickingTogglesState (true);
    saveButton.setColour (juce::TextButton::buttonOnColourId, Colours::armed);
    saveButton.setTooltip ("Click SAVE then a scene (or Shift-click a scene) to store the stem settings.");
    addAndMakeVisible (saveButton);

    setSize (1240, 924);
    startTimerHz (30);
}

RiqLiveEditor::~RiqLiveEditor()
{
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> RiqLiveEditor::assistantArea() const
{
    return { 10, 210, getWidth() - 20, 192 };
}

juce::Rectangle<int> RiqLiveEditor::autopilotArea() const
{
    auto a = assistantArea();
    return a.removeFromRight (640).reduced (10).withTrimmedBottom (24);
}

void RiqLiveEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // Top bar
    g.setColour (Colours::panel);
    g.fillRect (0, 0, getWidth(), 54);
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("RiQ", 16, 12, 56, 30, juce::Justification::centredLeft);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("LIVE RETRIGGER", 66, 19, 120, 18, juce::Justification::centredLeft);

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (auto* c : std::initializer_list<juce::Component*> { &protect, &freezeQ, &sceneQ })
    {
        const char* cap = c == &protect ? "KEEP STRONG BEATS" : c == &freezeQ ? "BUFFER FREEZE Q" : "SCENE QUANTIZATION";
        g.drawText (cap, c->getX(), 5, c->getWidth() + 40, 12, juce::Justification::centredLeft);
    }

    const bool playing = proc.transportPlaying.load();
    g.setColour (playing ? Colours::live : Colours::dim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (playing ? "HOST PLAYING" : "HOST STOPPED", getWidth() - 330, 18, 130, 18, juce::Justification::centredRight);

    // Beat Assistant banner
    const auto a = assistantArea();
    g.setColour (juce::Colour (0xff0d0e11));
    g.fillRoundedRectangle (a.toFloat(), 8.0f);
    g.setColour (Colours::line);
    g.drawRoundedRectangle (a.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("BEAT ASSISTANT", a.getX() + 14, a.getY() + 10, 200, 18, juce::Justification::centredLeft);

    // Link lines from IQ Arrange down to each stem
    const auto iqC = iqArrange.getBounds().getCentre().toFloat();
    for (int s = 0; s < kNumStems; ++s)
    {
        const auto lc = links[(size_t) s]->getBounds().getCentre().toFloat();
        const bool on = links[(size_t) s]->getToggleState();
        // Down from IQ Arrange, along a bus under the autopilot panel, then down into the stem.
        const float busY = (float) a.getBottom() - 20.0f + (float) s * 0.0f;
        const float dir = lc.x >= iqC.x ? 1.0f : -1.0f;
        const float rad = juce::jmin (12.0f, std::abs (lc.x - iqC.x) * 0.5f);
        juce::Path path;
        path.startNewSubPath (iqC.x, iqC.y + 50.0f);
        path.lineTo (iqC.x, busY - rad);
        path.quadraticTo (iqC.x, busY, iqC.x + dir * rad, busY);
        path.lineTo (lc.x - dir * rad, busY);
        path.quadraticTo (lc.x, busY, lc.x, busY + rad);
        path.lineTo (lc.x, lc.y);
        g.setColour (on ? Colours::stem[s].withAlpha (0.8f) : Colours::line);
        g.strokePath (path, juce::PathStrokeType (on ? 2.5f : 1.5f));
    }

    // Autopilot panel
    const auto ap = autopilotArea();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (ap.toFloat(), 6.0f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("AUTOPILOT", ap.getX() + 10, ap.getY() + 6, 100, 16, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PHRASE", apPhrase.getX(), apPhrase.getY() - 13, 80, 12, juce::Justification::centredLeft);
    g.drawText ("EVOLVE EVERY (BARS)", apEvolveEvery.getX(), apEvolveEvery.getY() - 13, 160, 12, juce::Justification::centredLeft);

    // Phrase progress: bar cells, the last one is the fill bar.
    const int len = juce::jmax (1, proc.phraseLen.load()), pos = proc.phrasePos.load();
    auto prog = juce::Rectangle<int> (apPhrase.getX(), apPhrase.getBottom() + 4, apPhrase.getWidth(), 6);
    const float cw = (float) prog.getWidth() / (float) len;
    for (int b = 0; b < len; ++b)
    {
        const bool fillBar = b == len - 1 || (len >= 8 && b == len / 2 - 1);
        g.setColour (playing && b == pos ? Colours::accent : fillBar ? Colours::accent.withAlpha (0.35f) : Colours::line);
        g.fillRect (juce::Rectangle<float> (prog.getX() + b * cw, (float) prog.getY(), cw - 1.0f, (float) prog.getHeight()));
    }

    // Scenes bar
    auto sc = getLocalBounds().removeFromBottom (56).reduced (10, 8);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (sc.toFloat(), 6.0f);
}

void RiqLiveEditor::resized()
{
    auto top = juce::Rectangle<int> (0, 0, getWidth(), 54).reduced (10, 0);
    top.removeFromLeft (190);
    auto place = [&] (juce::Component& c, int w)
    {
        c.setBounds (top.removeFromLeft (w).withTrimmedTop (19).withHeight (26));
        top.removeFromLeft (16);
    };
    place (protect, 110);
    place (freezeQ, 100);
    place (sceneQ, 100);

    auto right = juce::Rectangle<int> (0, 0, getWidth(), 54).reduced (10, 4);
    masterGain.setBounds (right.removeFromRight (56));
    right.removeFromRight (6);
    limiter.setBounds (right.removeFromRight (60).withSizeKeepingCentre (60, 26));

    master.setBounds (10, 62, getWidth() - 20, 140);

    // Beat Assistant: IQ section on the left, autopilot on the right.
    auto a = assistantArea();
    auto iq = a.withTrimmedRight (650);
    const int cx = iq.getCentreX();
    iqArrange.setBounds (cx - 55, a.getY() + 10, 110, 110);
    iqEvolve.setBounds (cx - 200, a.getY() + 50, 110, 30);
    iqReverse.setBounds (cx + 90, a.getY() + 50, 110, 30);

    auto ap = autopilotArea().reduced (10);
    ap.removeFromTop (22);
    auto row1 = ap.removeFromTop (34);
    apPhrase.setBounds (row1.removeFromLeft (220).withTrimmedTop (12));
    row1.removeFromLeft (30);
    apEvolveEvery.setBounds (row1.removeFromLeft (240).withTrimmedTop (12));
    ap.removeFromTop (14);
    const int kw = ap.getWidth() / 6;
    for (auto* k : { &apFill, &apMotion, &apRatchet, &apEvolveChance, &apRevDepth, &apDrop })
        k->setBounds (ap.removeFromLeft (kw).withSizeKeepingCentre (kw, juce::jmin (ap.getHeight(), 78)));

    // Stem columns
    auto colsArea = juce::Rectangle<int> (10, a.getBottom() + 26, getWidth() - 20, getHeight() - a.getBottom() - 26 - 60);
    const int cw = (colsArea.getWidth() - 2 * 10) / 3;
    for (int s = 0; s < kNumStems; ++s)
    {
        auto col = colsArea.removeFromLeft (cw);
        colsArea.removeFromLeft (10);
        columns[(size_t) s]->setBounds (col);
        links[(size_t) s]->setBounds (col.getCentreX() - 16, a.getBottom() - 10, 32, 28);
    }

    auto sc = getLocalBounds().removeFromBottom (56).reduced (10, 8).reduced (8, 6);
    const int bw = (sc.getWidth() - 90) / kNumScenes;
    for (auto& b : sceneButtons)
        b.setBounds (sc.removeFromLeft (bw).reduced (2, 0));
    sc.removeFromLeft (10);
    saveButton.setBounds (sc);
}

void RiqLiveEditor::updateSceneButtons()
{
    const int active = proc.getActiveScene();
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        const bool valid = proc.isSceneValid (i);
        b.setColour (juce::TextButton::buttonColourId, i == active ? Colours::accent.withAlpha (0.85f)
                                                       : valid ? Colours::text.withAlpha (0.25f) : Colours::panelHi);
        b.setColour (juce::TextButton::textColourOffId, valid ? Colours::text : Colours::dim);
    }
}

void RiqLiveEditor::timerCallback()
{
    for (auto& col : columns)
        col->refresh();
    master.repaint();
    updateSceneButtons();
    repaint (assistantArea());
    repaint (getWidth() - 340, 0, 140, 54);
}

//==============================================================================
RiqSendEditor::RiqSendEditor (RiqLiveProcessor& p)
    : AudioProcessorEditor (&p), panel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    setSize (520, 320);
    startTimerHz (24);
}

RiqSendEditor::~RiqSendEditor()
{
    setLookAndFeel (nullptr);
}
