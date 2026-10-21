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

    // Neon stroke: two soft halos under a crisp line.
    void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
    {
        g.setColour (c.withAlpha (0.12f));
        g.strokePath (p, juce::PathStrokeType (width * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.28f));
        g.strokePath (p, juce::PathStrokeType (width * 1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void caption (juce::Graphics& g, const juce::String& text, int x, int y, int w = 160)
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (text, x, y, w, 12, juce::Justification::centredLeft);
    }

    juce::String slotLetter (int s) { return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 2, s))); }
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Consolas");
    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    setColour (juce::ComboBox::backgroundColourId, Colours::panelHi);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::line);
    setColour (juce::ComboBox::arrowColourId, Colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, Colours::panelHi);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::BubbleComponent::backgroundColourId, Colours::panel);
    setColour (juce::BubbleComponent::outlineColourId, Colours::accent);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panel);
    setColour (juce::TooltipWindow::outlineColourId, Colours::accent);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::accent);
    setColour (juce::TextEditor::backgroundColourId, Colours::bg);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (Colours::line);
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        glowStroke (g, value, colour, 2.5f);
    }

    // Live value the autopilot is actually using (magenta, inner ring)
    const float ghost = (float) s.getProperties().getWithDefault ("ghost", -1.0f);
    if (ghost >= 0.0f)
    {
        juce::Path gp;
        gp.addCentredArc (c.x, c.y, r - 6.0f, r - 6.0f, 0.0f, start, start + ghost * (end - start), true);
        glowStroke (g, gp, Colours::edit, 2.0f);
    }

    // value dot + needle
    const auto tip = c.getPointOnCircumference (r, angle);
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (tip));
    g.setColour (Colours::text.withAlpha (0.8f));
    g.drawLine (juce::Line<float> (c.getPointOnCircumference (r * 0.25f, angle), c.getPointOnCircumference (r * 0.62f, angle)), 1.6f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto onCol = b.findColour (juce::TextButton::buttonOnColourId);
    auto c = on ? onCol : base;
    if (! b.isEnabled())
        c = c.withMultipliedAlpha (0.35f);
    if (down)
        c = c.brighter (0.3f);
    else if (over)
        c = c.brighter (0.1f);

    if (on)
    {
        g.setColour (onCol.withAlpha (0.18f));
        g.fillRoundedRectangle (r.expanded (2.0f), 3.0f);
    }
    g.setColour (c);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (on ? onCol.brighter (0.3f) : (over ? Colours::dim : Colours::line));
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool on = b.getToggleState();
    g.setColour ((on ? b.findColour (juce::TextButton::textColourOnId) : b.findColour (juce::TextButton::textColourOffId))
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 2.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Colours::accent);
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) h * 0.52f), juce::Font::bold));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.0f, juce::Font::bold));
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (14.0f));
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& cap, juce::Colour colour)
    : caption (cap)
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
    slider.setBounds (getLocalBounds().withTrimmedTop (13));
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (caption, getLocalBounds().removeFromTop (13), juce::Justification::centred);
}

Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        addItemList (p->choices, 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, *this);
}

Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::Colour c)
    : colour (c), attachment (*state.getParameter (paramId), [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        items = p->choices;
    attachment.sendInitialUpdate();
}

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colours::bg);
    g.fillRoundedRectangle (r, 2.0f);

    const float w = r.getWidth() / (float) juce::jmax (1, items.size());
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (int i = 0; i < items.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (1.0f);
        if (i == selected)
        {
            g.setColour (colour.withAlpha (0.2f));
            g.fillRect (cell.expanded (1.0f));
            g.setColour (colour);
            g.fillRect (cell);
        }
        else if (i > 0)
        {
            g.setColour (Colours::line);
            g.drawVerticalLine ((int) cell.getX(), cell.getY() + 4.0f, cell.getBottom() - 4.0f);
        }
        g.setColour (i == selected ? juce::Colours::black : Colours::dim);
        g.drawText (items[i], cell, juce::Justification::centred);
    }
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
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
    g.setColour (Colours::wave);
    g.fillRect (bounds);

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

    // centre line
    g.setColour (Colours::line.withAlpha (0.6f));
    g.drawHorizontalLine ((int) mid, 0.0f, w);

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

    const auto baseCol = stem >= 0 ? Colours::stem[stem] : Colours::accent;
    for (int x = 0; x < cols; ++x)
    {
        const float amp = juce::jmin (1.0f, columnPeak[(size_t) x] * displayGain) * (mid - 3.0f);
        g.setColour (columnAltered[(size_t) x] ? Colours::edit : baseCol.withAlpha (0.9f));
        g.drawVerticalLine (x, mid - amp, mid + amp + 1.0f);
    }

    for (int j = 1; j < nSl; ++j)
    {
        const double b = j * slice;
        const bool bar = wrapBeat (b, bpb) < 1.0e-6;
        const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
        if (stem < 0 && ! beat)
            continue;
        g.setColour (bar ? Colours::text.withAlpha (0.45f) : beat ? Colours::dim.withAlpha (0.45f) : Colours::line.withAlpha (0.5f));
        g.drawVerticalLine ((int) ((float) (b / loop) * w), 0.0f, h);
    }

    if (stem >= 0)
    {
        // What the engine does to each slice, drawn on the slice itself:
        //  arc = Arrange moved/repeated material from the source slice into this one
        //  ticks = Ratchet retriggers, arrow = Reverse, bottom bar = slice is edited
        const float sw = (float) (slice / loop) * w;
        for (int j = 0; j < nSl; ++j)
        {
            const auto& d = info[(size_t) stem][(size_t) j];
            if (! d.altered)
                continue;
            const float x0 = (float) (j * slice / loop) * w;
            g.setColour (Colours::edit);
            g.fillRect (x0 + 1.0f, h - 3.0f, sw - 2.0f, 3.0f);

            if (d.ratchet > 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                for (int k = 1; k < d.ratchet; ++k)
                    g.drawVerticalLine ((int) (x0 + sw * (float) k / (float) d.ratchet), 4.0f, h - 4.0f);
            }

            if (d.rev)
            {
                juce::Path tri;
                const float tx = x0 + juce::jmin (sw * 0.5f, 9.0f), ty = h - 10.0f;
                tri.addTriangle (tx - 4.0f, ty, tx + 2.0f, ty - 4.0f, tx + 2.0f, ty + 4.0f);
                g.setColour (Colours::text);
                g.fillPath (tri);
            }

            if (d.src != j)
            {
                const float xs = (float) ((d.src + 0.5) * slice / loop) * w, xd = x0 + sw * 0.5f;
                const float depth = juce::jlimit (6.0f, h * 0.42f, std::abs (xd - xs) * 0.3f);
                juce::Path arc;
                arc.startNewSubPath (xs, 1.0f);
                arc.quadraticTo ((xs + xd) * 0.5f, 1.0f + depth * 2.0f, xd, 1.0f);
                g.setColour (Colours::edit.withAlpha (0.8f));
                g.strokePath (arc, juce::PathStrokeType (1.3f));
                g.fillEllipse (xs - 2.0f, 0.0f, 4.0f, 4.0f);
                juce::Path head;
                head.addTriangle (xd - 3.5f, 6.0f, xd + 3.5f, 6.0f, xd, 0.0f);
                g.fillPath (head);
            }
        }
    }

    if (playing)
    {
        juce::Path ph;
        const float px = (float) (base.dispPhase.load() / loop) * w;
        ph.startNewSubPath (px, 0.0f);
        ph.lineTo (px, h);
        glowStroke (g, ph, Colours::accent, 1.2f);
    }

    if (stem >= 0)
    {
        const int slot = (int) proc.apvts.getRawParameterValue (ID::stem (stem, "src"))->load();
        juce::String msg;
        if (proc.dropped[stem].load() != 0)
            msg = "DROPPED BY AUTOPILOT";
        else if (! RiqLiveProcessor::isSlotFed (slot) && base.dispState.load() != (int) CaptureState::frozen)
            msg = "NO SEND ON SLOT " + slotLetter (slot);
        else if (proc.sourceOk[stem].load() == 0 && base.dispState.load() != (int) CaptureState::frozen)
            msg = playing ? "SLOT " + slotLetter (slot) + " SILENT" : "TRANSPORT STOPPED";

        if (msg.isNotEmpty())
        {
            g.setColour (Colours::dim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (msg, getLocalBounds().reduced (8, 4), juce::Justification::centredRight);
        }
    }

    g.setColour (Colours::line);
    g.drawRect (bounds, 1.0f);
}

//==============================================================================
void PhraseMap::paint (juce::Graphics& g)
{
    const auto in = proc.makeAutopilotInput();
    const int pb = juce::jmax (1, in.phraseBars);
    const auto cur = proc.currentBar.load();
    const int pos = (int) (((cur % pb) + pb) % pb);
    const auto start = cur - pos;
    const bool playing = proc.transportPlaying.load();

    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::wave);
    g.fillRect (r);

    auto labels = r.removeFromLeft (22.0f);
    auto header = r.removeFromTop (14.0f);
    labels.removeFromTop (14.0f);
    const float cw = r.getWidth() / (float) pb, rh = r.getHeight() / 3.0f;

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int s = 0; s < 3; ++s)
    {
        g.setColour (Colours::stem[s]);
        g.drawText (slotLetter (s), juce::Rectangle<float> (labels.getX(), labels.getY() + s * rh, labels.getWidth(), rh),
                    juce::Justification::centred);
    }

    for (int b = 0; b < pb; ++b)
    {
        const auto plan = planAutopilotBar (in, start + b);
        const float x = r.getX() + b * cw;

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (plan.fillBar || plan.halfFillBar ? Colours::edit : Colours::dim);
        g.drawText (plan.fillBar ? "FILL" : plan.halfFillBar ? "fill" : juce::String (b + 1),
                    juce::Rectangle<float> (x, header.getY(), cw, header.getHeight()), juce::Justification::centred);

        for (int s = 0; s < 3; ++s)
        {
            const auto& st = plan.stems[(size_t) s];
            const auto cell = juce::Rectangle<float> (x, r.getY() + s * rh, cw, rh).reduced (1.5f);
            if (st.drop)
            {
                g.setColour (Colours::bg);
                g.fillRect (cell);
                g.setColour (Colours::warn.withAlpha (0.6f));
                g.drawLine (cell.getX(), cell.getBottom(), cell.getRight(), cell.getY(), 1.2f);
                continue;
            }
            g.setColour (Colours::stem[s].withAlpha (0.08f + 0.82f * st.arrange));
            g.fillRect (cell);
            if (st.ratchet > 0.02f)
            {
                g.setColour (Colours::edit);
                g.fillRect (cell.withTop (cell.getBottom() - juce::jmax (2.0f, cell.getHeight() * 0.5f * st.ratchet)));
            }
            if (st.reverse > 0.15f)
            {
                juce::Path tri;
                const float ty = cell.getCentreY();
                tri.addTriangle (cell.getX() + 3.0f, ty, cell.getX() + 8.0f, ty - 4.0f, cell.getX() + 8.0f, ty + 4.0f);
                g.setColour (Colours::text.withAlpha (juce::jmin (1.0f, st.reverse * 1.5f)));
                g.fillPath (tri);
            }
            if (st.evolve)
            {
                g.setColour (juce::Colours::white);
                g.fillEllipse (cell.getRight() - 7.0f, cell.getY() + 2.0f, 5.0f, 5.0f);
            }
        }

        if (playing && b == pos)
        {
            juce::Path outline;
            outline.addRectangle (juce::Rectangle<float> (x + 0.5f, r.getY(), cw - 1.0f, r.getHeight()));
            glowStroke (g, outline, Colours::accent, 1.5f);
        }
    }
}

//==============================================================================
AutopilotDeck::AutopilotDeck (RiqLiveProcessor& p)
    : iqArrange (p.apvts, ID::iqArrange, "IQ ARRANGE", Colours::accent),
      proc (p),
      iqEvolve (p.apvts, ID::iqEvolve, "IQ EVOLVE", Colours::accent),
      iqReverse (p.apvts, ID::iqReverse, "IQ REVERSE", Colours::accent),
      phraseMap (p),
      apPhrase (p.apvts, ID::apPhrase), apEvolveEvery (p.apvts, ID::apEvolveEvery),
      apFill (p.apvts, ID::apFill, "FILL", Colours::accent),
      apMotion (p.apvts, ID::apMotion, "MOTION", Colours::accent),
      apRatchet (p.apvts, ID::apRatchet, "FILL RATCHET", Colours::edit),
      apEvolveChance (p.apvts, ID::apEvolveChance, "EVOLVE %", Colours::accent),
      apRevDepth (p.apvts, ID::apRevDepth, "REV DEPTH", Colours::accent),
      apDrop (p.apvts, ID::apDrop, "DROP", Colours::warn)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &iqArrange, &iqEvolve, &iqReverse, &phraseMap, &apPhrase, &apEvolveEvery,
             &apFill, &apMotion, &apRatchet, &apEvolveChance, &apRevDepth, &apDrop })
        addAndMakeVisible (c);

    for (int s = 0; s < kNumStems; ++s)
    {
        links[(size_t) s] = std::make_unique<Toggle> (p.apvts, ID::stem (s, "link"), slotLetter (s), Colours::stem[s]);
        links[(size_t) s]->setTooltip ("Stem " + slotLetter (s) + " follows the autopilot");
        addAndMakeVisible (*links[(size_t) s]);
    }

    iqArrange.slider.setTooltip ("Autopilot drives the Arrange/Ratchet of linked stems: motion every bar, fills at phrase ends");
    iqEvolve.setTooltip ("Autopilot presses the stem Evolve buttons at the bars set by EVERY / EVOLVE %");
    iqReverse.setTooltip ("Autopilot reverses slices of linked stems on alternating bars");
    apDrop.slider.setTooltip ("Chance that a linked stem sits out a bar (one always keeps playing)");
    phraseMap.setTooltip ("Plan for this phrase: brightness = arrange, magenta = ratchet, arrow = reverse, "
                          "dot = evolve, red cross = stem dropped. Lime frame = now.");
}

void AutopilotDeck::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (22);

    auto left = r.removeFromLeft (250);
    iqArrange.setBounds (left.getX(), left.getY(), 116, 124);
    iqEvolve.setBounds (left.getX() + 126, left.getY() + 16, 120, 30);
    iqReverse.setBounds (left.getX() + 126, left.getY() + 54, 120, 30);
    for (int s = 0; s < kNumStems; ++s)
        links[(size_t) s]->setBounds (left.getX() + 56 + s * 44, left.getBottom() - 30, 38, 26);

    r.removeFromLeft (14);
    auto right = r.removeFromRight (380);
    auto selRow = right.removeFromTop (38);
    apPhrase.setBounds (selRow.removeFromLeft (176).withTrimmedTop (14));
    selRow.removeFromLeft (14);
    apEvolveEvery.setBounds (selRow.withTrimmedTop (14));
    right.removeFromTop (6);
    const int kw = right.getWidth() / 3, kh = right.getHeight() / 2;
    int i = 0;
    for (auto* k : { &apFill, &apMotion, &apRatchet, &apEvolveChance, &apRevDepth, &apDrop })
    {
        k->setBounds (right.getX() + (i % 3) * kw, right.getY() + (i / 3) * kh, kw, kh);
        ++i;
    }

    r.removeFromRight (14);
    metersArea = r.removeFromBottom (44);
    r.removeFromBottom (6);
    r.removeFromTop (14);
    phraseMap.setBounds (r);
}

void AutopilotDeck::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRect (b);
    g.setColour (Colours::accent.withAlpha (0.5f));
    g.drawRect (b, 1.0f);
    g.setColour (Colours::accent);
    g.fillRect (b.removeFromTop (2.0f));

    const auto in = proc.makeAutopilotInput();
    const int pb = juce::jmax (1, in.phraseBars), pos = proc.phrasePos.load();
    const bool engaged = in.iq > 0.0f || in.iqEvolve || in.iqReverse;

    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.setColour (engaged ? Colours::accent : Colours::dim);
    g.drawText ("AUTOPILOT", 12, 6, 160, 22, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (engaged ? Colours::live : Colours::dim);
    g.drawText (engaged ? "ENGAGED" : "STANDBY", 132, 8, 90, 18, juce::Justification::centredLeft);

    const int toFill = (pb - 1 - pos + pb) % pb;
    g.setColour (Colours::text);
    g.drawText ("BAR " + juce::String (pos + 1) + "/" + juce::String (pb)
                    + (toFill == 0 ? "   >> FILL NOW" : "   FILL IN " + juce::String (toFill)),
                phraseMap.getX(), 8, 300, 18, juce::Justification::centredLeft);

    caption (g, "LINK", links[0]->getX() - 40, links[0]->getY() + 7, 36);
    caption (g, "PHRASE", apPhrase.getX(), apPhrase.getY() - 13);
    caption (g, "EVOLVE EVERY (BARS)", apEvolveEvery.getX(), apEvolveEvery.getY() - 13);
    caption (g, "PHRASE MAP", phraseMap.getX(), phraseMap.getY() - 13);

    // live meters: what the stems are using right now
    auto m = metersArea;
    caption (g, "NOW:  ARRANGE  |  RATCHET  |  REVERSE", m.getX(), m.getY() - 1, 400);
    m.removeFromTop (12);
    const int rowH = m.getHeight() / 3;
    for (int s = 0; s < kNumStems; ++s)
    {
        auto row = m.removeFromTop (rowH).reduced (0, 2);
        g.setColour (Colours::stem[s]);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (slotLetter (s), row.removeFromLeft (16), juce::Justification::centredLeft);
        const float vals[] = { proc.effArrange[s].load(), proc.effRatchet[s].load(), proc.effReverse[s].load() };
        const juce::Colour cols[] = { Colours::stem[s], Colours::edit, Colours::text };
        const int bw = row.getWidth() / 3;
        for (int k = 0; k < 3; ++k)
        {
            auto bar = row.removeFromLeft (bw).reduced (3, 0);
            g.setColour (Colours::panelHi);
            g.fillRect (bar);
            g.setColour (proc.dropped[s].load() ? Colours::line : cols[k]);
            g.fillRect (bar.withWidth ((int) (bar.getWidth() * juce::jlimit (0.0f, 1.0f, vals[k]))));
        }
    }
}

//==============================================================================
MasterDisplay::MasterDisplay (RiqLiveProcessor& p)
    : proc (p), fillSize (p.apvts, ID::fillSize), loopLen (p.apvts, ID::loopLen),
      freezeQ (p.apvts, ID::freezeQ), sceneQ (p.apvts, ID::sceneQ), wave (p, -1)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &fillSize, &loopLen, &freezeQ, &sceneQ, &wave })
        addAndMakeVisible (c);
    fillSize.setTooltip ("Keyboard fill slice size");
    loopLen.setTooltip ("Loop length");
    freezeQ.setTooltip ("Buffer freeze quantisation: when IN -> FROZEN takes effect");
    sceneQ.setTooltip ("Scene recall quantisation (2/4/8/16 bars switch on phrase boundaries)");
}

void MasterDisplay::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto top = r.removeFromTop (22);
    loopLen.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (76);
    fillSize.setBounds (top.removeFromLeft (190));
    top.removeFromLeft (76);
    freezeQ.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (70);
    sceneQ.setBounds (top.removeFromLeft (90));
    r.removeFromTop (5);
    r.removeFromBottom (20);
    wave.setBounds (r);
}

void MasterDisplay::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRect (getLocalBounds());

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("FILL KEYS", fillSize.getX() - 72, fillSize.getY(), 68, fillSize.getHeight(), juce::Justification::centredRight);
    g.drawText ("FREEZE Q", freezeQ.getX() - 72, freezeQ.getY(), 68, freezeQ.getHeight(), juce::Justification::centredRight);
    g.drawText ("SCENE Q", sceneQ.getX() - 66, sceneQ.getY(), 62, sceneQ.getHeight(), juce::Justification::centredRight);

    const auto waveR = wave.getBounds();
    const auto& st = proc.getStem (0);
    const double L = st.dispLoop.load(), bpb = st.dispBeatsPerBar.load();

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (Colours::text);
    for (int b = 0; b < (int) std::round (L / bpb); ++b)
        g.drawText (juce::String (b + 1), waveR.getX() + (int) (b * bpb / L * waveR.getWidth()) + 4, waveR.getY() + 2, 20, 12,
                    juce::Justification::centredLeft);

    // keyboard fill strip: one cell per fill slice, labelled with its MIDI key
    const double fs = kSliceBeats[juce::jlimit (0, 3, (int) proc.apvts.getRawParameterValue (ID::fillSize)->load())];
    const int cells = juce::jmax (1, (int) std::round (L / fs));
    const int baseNote = (int) proc.apvts.getRawParameterValue (ID::fillNote)->load();
    const auto mask = proc.fillMask.load();
    const auto strip = juce::Rectangle<int> (waveR.getX(), waveR.getBottom() + 3, waveR.getWidth(), 16);
    const float cw = (float) strip.getWidth() / (float) cells;
    for (int c = 0; c < cells; ++c)
    {
        const auto cell = juce::Rectangle<float> (strip.getX() + c * cw, (float) strip.getY(), cw, (float) strip.getHeight()).reduced (0.5f);
        const bool mapped = c < 16;
        const bool held = mapped && ((mask >> c) & 1u);
        g.setColour (held ? Colours::accent : juce::MidiMessage::isMidiNoteBlack (baseNote + c) ? Colours::bg : Colours::panelHi);
        g.fillRect (cell);
        if (mapped && cw > 22.0f)
        {
            g.setColour (held ? juce::Colours::black : Colours::dim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::MidiMessage::getMidiNoteName (baseNote + c, true, true, 3), cell, juce::Justification::centred);
        }
    }

    const bool playing = proc.transportPlaying.load();
    g.setColour (playing ? Colours::live : Colours::dim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (juce::String (proc.hostBpm.load(), 1) + " BPM  " + (playing ? "> PLAY" : "|| STOP"),
                getLocalBounds().reduced (12, 6).removeFromTop (22), juce::Justification::centredRight);
}

//==============================================================================
PresetBar::PresetBar (RiqLiveProcessor& p) : proc (p)
{
    for (auto* b : { &prev, &next, &name, &save, &del })
        addAndMakeVisible (b);
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    name.onClick = [this] { showMenu(); };
    save.onClick = [this]
    {
        const auto cur = proc.getCurrentPresetName();
        askName ("Save preset", cur.isNotEmpty() ? cur : juce::String ("My preset"),
                 [this] (juce::String n) { proc.savePreset (n); refresh(); });
    };
    del.onClick = [this] { confirmDelete(); };
    name.setTooltip ("Presets: whole plugin state incl. scenes. Stored in Documents/AUTOPILOT/Presets");
    save.setTooltip ("Save the current state as a preset (same name overwrites)");
    del.setTooltip ("Delete the current preset file");
    refresh();
}

void PresetBar::resized()
{
    auto r = getLocalBounds().withTrimmedTop (17).withHeight (26);
    prev.setBounds (r.removeFromLeft (26));
    r.removeFromLeft (2);
    del.setBounds (r.removeFromRight (44));
    r.removeFromRight (4);
    save.setBounds (r.removeFromRight (54));
    r.removeFromRight (4);
    next.setBounds (r.removeFromRight (26));
    r.removeFromRight (2);
    name.setBounds (r);
}

void PresetBar::paint (juce::Graphics& g)
{
    caption (g, "PRESET", 0, 3);
}

void PresetBar::refresh()
{
    const auto cur = proc.getCurrentPresetName();
    if (cur != shown || name.getButtonText().isEmpty())
    {
        shown = cur;
        name.setButtonText (cur.isNotEmpty() ? cur : juce::String ("- init -"));
    }
    del.setEnabled (cur.isNotEmpty() && RiqLiveProcessor::getPresetNames().contains (cur));
}

void PresetBar::step (int delta)
{
    const auto names = RiqLiveProcessor::getPresetNames();
    if (names.isEmpty())
        return;
    const int idx = names.indexOf (proc.getCurrentPresetName());
    const int nextIdx = idx < 0 ? (delta > 0 ? 0 : names.size() - 1) : (idx + delta + names.size()) % names.size();
    proc.loadPreset (names[nextIdx]);
    refresh();
}

void PresetBar::showMenu()
{
    const auto names = RiqLiveProcessor::getPresetNames();
    const auto cur = proc.getCurrentPresetName();
    juce::PopupMenu m;
    m.addItem (1, "Init (defaults)");
    m.addSeparator();
    for (int i = 0; i < names.size(); ++i)
        m.addItem (100 + i, names[i], true, names[i] == cur);
    if (names.isEmpty())
        m.addItem (-1, "(no presets yet - press SAVE)", false);
    m.addSeparator();
    m.addItem (2, "Save as...");
    m.addItem (3, "Rename...", cur.isNotEmpty() && names.contains (cur));
    m.addItem (4, "Delete", cur.isNotEmpty() && names.contains (cur));
    m.addItem (5, "Open presets folder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name), [this, names, cur] (int r)
    {
        if (r >= 100 && r - 100 < names.size())
            proc.loadPreset (names[r - 100]);
        else if (r == 1)
            proc.loadInitPreset();
        else if (r == 2)
            askName ("Save preset as", cur.isNotEmpty() ? cur + " 2" : juce::String ("My preset"),
                     [this] (juce::String n) { proc.savePreset (n); refresh(); });
        else if (r == 3)
            askName ("Rename preset", cur, [this, cur] (juce::String n) { proc.renamePreset (cur, n); refresh(); });
        else if (r == 4)
            confirmDelete();
        else if (r == 5)
            RiqLiveProcessor::getPresetFolder().startAsProcess();
        refresh();
    });
}

void PresetBar::askName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk)
{
    auto* w = new juce::AlertWindow (title, "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk] (int result)
    {
        const auto text = w->getTextEditorContents ("name").trim();
        if (result == 1 && text.isNotEmpty())
            onOk (text);
    }), true);
}

void PresetBar::confirmDelete()
{
    const auto cur = proc.getCurrentPresetName();
    if (cur.isEmpty())
        return;
    auto* w = new juce::AlertWindow ("Delete preset", "Delete \"" + cur + "\" for good?", juce::MessageBoxIconType::WarningIcon, this);
    w->addButton ("Delete", 1);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, cur] (int result)
    {
        if (result == 1)
            proc.deletePreset (cur);
        refresh();
    }), true);
}

//==============================================================================
StemLane::StemLane (RiqLiveProcessor& p, int s)
    : proc (p), stem (s),
      source (p.apvts, ID::stem (s, "src")),
      keep (p.apvts, ID::stem (s, "keep")),
      slice (p.apvts, ID::stem (s, "slice"), Colours::stem[s]),
      wave (p, s),
      hold (p.apvts, ID::stem (s, "roll"), "FREEZE"),
      capture (p.apvts, ID::stem (s, "freeze"), "IN", Colours::frozen),
      autoLink (p.apvts, ID::stem (s, "link"), "AUTO", Colours::accent),
      arrange (p.apvts, ID::stem (s, "arrange"), "ARRANGE", Colours::stem[s]),
      reverse (p.apvts, ID::stem (s, "reverse"), "REVERSE", Colours::stem[s]),
      silence (p.apvts, ID::stem (s, "silence"), "SILENCE", Colours::stem[s]),
      ratchet (p.apvts, ID::stem (s, "ratchet"), "RATCHET", Colours::edit),
      pan (p.apvts, ID::stem (s, "pan"), "PAN", Colours::dim),
      gain (p.apvts, ID::stem (s, "gain"), "GAIN", Colours::dim),
      evolveArr (p.apvts, ID::stem (s, "evolveArr"), "EVOLVE"),
      evolveRev (p.apvts, ID::stem (s, "evolveRev"), "EVOLVE"),
      on (p.apvts, ID::stem (s, "on"), "ON", Colours::stem[s]),
      solo (p.apvts, ID::stem (s, "solo"), "SOLO", Colours::armed),
      out (p.apvts, ID::stem (s, "out"), Colours::stem[s])
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &source, &keep, &slice, &wave, &hold, &capture, &autoLink, &arrange, &reverse, &silence, &ratchet,
             &pan, &gain, &evolveArr, &evolveRev, &on, &solo, &out })
        addAndMakeVisible (c);

    hold.setTooltip ("Hold: repeats the currently playing slice");
    capture.setTooltip ("IN: the track streams live into the buffer. Click to FREEZE the buffer contents (quantised by Freeze Q)");
    autoLink.setTooltip ("This stem follows the autopilot");
    keep.setTooltip ("KEEP: slices on these positions are never edited (Global = top bar). Kick: All Beats, hats: Off-Beats");
    ratchet.slider.setTooltip ("Subdivides weak-beat slices into 2 or 4 retriggers (no triplets)");
    out.setTooltip ("Mix: stem plays on the main output. Own: only on its own output (Stem " + juce::String (s + 1) + ")");

    // Touching a stem knob hands control back from the autopilot.
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

void StemLane::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRect (r);
    g.setColour (Colours::stem[stem]);
    g.fillRect (r.removeFromLeft (3.0f));

    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("STEM " + slotLetter (stem), 12, 5, 80, 18, juce::Justification::centredLeft);

    const float lvl = juce::jmin (1.0f, proc.getStem (stem).dispLevel.load());
    g.setColour (Colours::line);
    g.fillRect (88, 11, 44, 5);
    g.setColour (Colours::stem[stem]);
    g.fillRect (88, 11, (int) (44 * std::sqrt (lvl)), 5);

    const int slot = (int) proc.apvts.getRawParameterValue (ID::stem (stem, "src"))->load();
    const bool fed = RiqLiveProcessor::isSlotFed (slot);
    const auto trackName = RiqLiveProcessor::getSlotTrackName (slot);
    g.setColour (fed ? Colours::live : Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (fed ? "> " + (trackName.isNotEmpty() ? trackName : juce::String ("connected")) : juce::String ("no send"),
                juce::Rectangle<int> (138, 5, wave.getX() - 146, 18), juce::Justification::centredLeft);

    caption (g, "OUT", out.getX(), out.getY() - 12);
}

void StemLane::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromLeft (2);
    auto left = r.removeFromLeft (238);
    left.removeFromTop (22);
    auto row = left.removeFromTop (22);
    source.setBounds (row.removeFromLeft (114));
    row.removeFromLeft (6);
    keep.setBounds (row);
    left.removeFromTop (5);
    slice.setBounds (left.removeFromTop (22));
    left.removeFromTop (5);
    capture.setBounds (left.removeFromTop (28));
    left.removeFromTop (5);
    row = left.removeFromTop (24);
    hold.setBounds (row.removeFromLeft (156));
    row.removeFromLeft (6);
    autoLink.setBounds (row);

    r.removeFromLeft (10);
    auto right = r.removeFromRight (500);
    r.removeFromRight (10);
    wave.setBounds (r);

    auto knobs = right.removeFromLeft (304);
    auto evo = knobs.removeFromBottom (22);
    const int kw = knobs.getWidth() / 4;
    int i = 0;
    for (auto* k : { &arrange, &reverse, &silence, &ratchet })
        k->setBounds (knobs.getX() + kw * i++, knobs.getY(), kw, knobs.getHeight());
    evolveArr.setBounds (evo.getX() + 6, evo.getY(), kw - 12, evo.getHeight());
    evolveRev.setBounds (evo.getX() + kw + 6, evo.getY(), kw - 12, evo.getHeight());

    right.removeFromLeft (10);
    auto col = right.removeFromLeft (84);
    on.setBounds (col.getX(), col.getY() + 4, 40, 26);
    solo.setBounds (col.getX() + 44, col.getY() + 4, 40, 26);
    out.setBounds (col.getX(), col.getY() + 52, 84, 22);

    right.removeFromLeft (10);
    const int sw = right.getWidth() / 2;
    pan.setBounds (right.removeFromLeft (sw).withHeight (84));
    gain.setBounds (right.withHeight (84));
}

void StemLane::refresh()
{
    const auto state = (CaptureState) proc.getStem (stem).dispState.load();
    const auto now = juce::Time::getMillisecondCounter();
    const bool blink = (now / 250) % 2 == 0;

    if (! capture.getToggleState())
    {
        capture.setButtonText ("IN  >  LIVE TO BUFFER");
        capture.setColour (juce::TextButton::buttonColourId, Colours::live.withAlpha (0.22f));
    }
    else if (state == CaptureState::armed)
    {
        capture.setButtonText ("FREEZING ON NEXT STEP...");
        capture.setColour (juce::TextButton::buttonOnColourId, blink ? Colours::armed : Colours::armed.darker (0.6f));
    }
    else
    {
        capture.setButtonText ("FROZEN  -  CLICK FOR IN");
        capture.setColour (juce::TextButton::buttonOnColourId, Colours::frozen);
    }

    // Autopilot feedback: live value rings, locked Evolve buttons, flashes when it presses them.
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
    repaint (80, 0, juce::jmax (0, wave.getX() - 80), 26);
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
    g.drawText ("AUTOPILOT SEND", r.removeFromTop (28), juce::Justification::centredLeft);

    r.removeFromTop (40);
    g.setColour (colour);
    g.setFont (juce::FontOptions (54.0f, juce::Font::bold));
    g.drawText ("SLOT " + slotLetter (s), r.removeFromTop (64), juce::Justification::centred);

    auto meter = r.removeFromTop (22).withSizeKeepingCentre (360, 10);
    g.setColour (Colours::panelHi);
    g.fillRect (meter);
    g.setColour (colour);
    g.fillRect (meter.withWidth ((int) (meter.getWidth() * juce::jmin (1.0f, std::sqrt (proc.sendLevel.load())))));

    r.removeFromTop (12);
    if (proc.poolFull.load())
    {
        auto box = r.removeFromTop (60).reduced (10, 4);
        g.setColour (Colours::warn);
        g.fillRect (box);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawFittedText ("TOO MANY SENDS (MAX " + juce::String (SharedBusData::kMaxSends) + ")\nThis track is NOT being sent",
                          box, juce::Justification::centred, 2);
        return;
    }

    const int count = RiqLiveProcessor::getSlotSendCount (s);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("Streams this track into AUTOPILOT (stem source \"Send " + slotLetter (s) + "\")."
                      + (count > 1 ? "\nMixed on this slot: " + RiqLiveProcessor::getSlotTrackName (s) : juce::String())
                      + "\nWhile playing the track is muted here (AUTOPILOT plays it); stopped, it passes through.",
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
      presetBar (p),
      protect (p.apvts, ID::protect),
      style (p.apvts, ID::style),
      masterGain (p.apvts, ID::masterGain, "GAIN", Colours::dim),
      limDrive (p.apvts, ID::limDrive, "DRIVE", Colours::edit),
      limCeiling (p.apvts, ID::limCeiling, "CEIL", Colours::accent),
      limiter (p.apvts, ID::limiter, "LIMIT", Colours::accent),
      deck (p),
      master (p)
{
    setLookAndFeel (&lnf);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &presetBar, &protect, &style, &masterGain, &limDrive, &limCeiling, &limiter, &deck, &master })
        addAndMakeVisible (c);

    protect.setTooltip ("KEEP: slices on these positions are never edited (per-stem KEEP can override)");
    style.setTooltip ("Hitech: groove bars keep the 16th roll in phase, rolls accelerate x2 -> x4 on the last beat");
    limiter.setTooltip ("True-peak limiter on the Mix and on every stem output");
    limCeiling.slider.setTooltip ("Limiter ceiling in dBTP (inter-sample peaks included)");
    limDrive.slider.setTooltip ("Gain into the limiters (louder)");

    for (int s = 0; s < kNumStems; ++s)
    {
        lanes[(size_t) s] = std::make_unique<StemLane> (p, s);
        addAndMakeVisible (*lanes[(size_t) s]);
    }

    for (int i = 0; i < kNumScenes; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        b.setButtonText (i == 0 ? "RESET" : "SCENE " + juce::String (i));
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
    saveButton.setTooltip ("Click SAVE then a scene (or Shift-click a scene) to store the stem settings. "
                           "Scenes are kept in the Live set and in presets.");
    addAndMakeVisible (saveButton);

    setSize (1320, 936);
    startTimerHz (30);
}

RiqLiveEditor::~RiqLiveEditor()
{
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> RiqLiveEditor::meterArea() const
{
    return { limDrive.getX() - 176, 4, 168, 40 };
}

void RiqLiveEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // top bar
    g.setColour (Colours::panel);
    g.fillRect (0, 0, getWidth(), 48);
    juce::Path logoLine;
    logoLine.startNewSubPath (16.0f, 40.0f);
    logoLine.lineTo (178.0f, 40.0f);
    glowStroke (g, logoLine, Colours::accent, 1.2f);
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("AUTOPILOT", 14, 6, 170, 30, juce::Justification::centredLeft);

    caption (g, "KEEP (GLOBAL)", protect.getX(), 3);
    caption (g, "GRID STYLE", style.getX(), 3);

    // Mix loudness after the limiter
    const auto m = meterArea();
    const float lufs = proc.mixLufs.load(), gr = proc.limiterGrDb.load();
    g.setColour (Colours::bg);
    g.fillRect (m);
    g.setColour (Colours::line);
    g.drawRect (m);
    g.setColour (lufs > -9.0f ? Colours::edit : Colours::accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ((lufs <= -69.0f ? juce::String ("-inf") : juce::String (lufs, 1)) + " LUFS",
                m.withHeight (22).reduced (8, 0), juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PK " + juce::String (proc.mixPeakDb.load(), 1) + "  GR " + juce::String (gr, 1) + " dB",
                m.withTrimmedTop (22).reduced (8, 0), juce::Justification::centredLeft);

    // scenes bar
    g.setColour (Colours::panel);
    g.fillRect (getLocalBounds().removeFromBottom (60).reduced (10, 6));
}

void RiqLiveEditor::resized()
{
    auto top = juce::Rectangle<int> (0, 0, getWidth(), 48).reduced (10, 0);
    top.removeFromLeft (186);
    presetBar.setBounds (top.removeFromLeft (390).withHeight (48));
    top.removeFromLeft (16);
    protect.setBounds (top.removeFromLeft (110).withTrimmedTop (17).withHeight (26));
    top.removeFromLeft (14);
    style.setBounds (top.removeFromLeft (150).withTrimmedTop (17).withHeight (26));

    auto right = juce::Rectangle<int> (0, 0, getWidth(), 48).reduced (10, 2);
    masterGain.setBounds (right.removeFromRight (48));
    right.removeFromRight (4);
    limiter.setBounds (right.removeFromRight (56).withSizeKeepingCentre (56, 26));
    right.removeFromRight (4);
    limCeiling.setBounds (right.removeFromRight (48));
    right.removeFromRight (2);
    limDrive.setBounds (right.removeFromRight (48));

    deck.setBounds (10, 56, getWidth() - 20, 216);
    master.setBounds (10, 280, getWidth() - 20, 110);

    auto lanesArea = juce::Rectangle<int> (10, 398, getWidth() - 20, 3 * 150 + 2 * 6);
    for (auto& lane : lanes)
    {
        lane->setBounds (lanesArea.removeFromTop (150));
        lanesArea.removeFromTop (6);
    }

    auto sc = getLocalBounds().removeFromBottom (60).reduced (10, 6).reduced (8, 8);
    const int bw = (sc.getWidth() - 96) / kNumScenes;
    for (auto& b : sceneButtons)
        b.setBounds (sc.removeFromLeft (bw).reduced (2, 0));
    sc.removeFromLeft (12);
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
                                                       : valid ? Colours::accent.withAlpha (0.18f) : Colours::panelHi);
        b.setColour (juce::TextButton::textColourOffId, i == active ? juce::Colours::black : valid ? Colours::text : Colours::dim);
    }
}

void RiqLiveEditor::timerCallback()
{
    for (auto& lane : lanes)
        lane->refresh();
    master.repaint();
    deck.repaint();
    updateSceneButtons();
    presetBar.refresh();
    repaint (meterArea());
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
