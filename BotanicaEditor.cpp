#include "BotanicaEditor.h"

using namespace botui;

//==============================================================================
BotanicaLook::BotanicaLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg());
    setColour (juce::Slider::textBoxTextColourId, text());
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, dim());
    setColour (juce::ComboBox::backgroundColourId, panelHi());
    setColour (juce::ComboBox::textColourId, text());
    setColour (juce::ComboBox::outlineColourId, track());
    setColour (juce::ComboBox::arrowColourId, aqua());
    setColour (juce::PopupMenu::backgroundColourId, panel());
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, aqua());
    setColour (juce::PopupMenu::highlightedTextColourId, bg());
}

void BotanicaLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                     float startAngle, float endAngle, juce::Slider& s)
{
    const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float d = (float) juce::jmin (w, h) - 8.0f;
    const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    const float r = d * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // body
    g.setColour (panelHi());
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);

    // track + value arc
    juce::Path t, v;
    const float ar = r - 2.0f;
    t.addCentredArc (c.x, c.y, ar, ar, 0.0f, startAngle, endAngle, true);
    v.addCentredArc (c.x, c.y, ar, ar, 0.0f, startAngle, angle, true);
    g.setColour (track());
    g.strokePath (t, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (v, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    const float pr1 = r * 0.20f, pr2 = r * 0.62f;
    g.setColour (col.brighter (0.2f));
    g.drawLine (c.x + pr1 * std::sin (angle), c.y - pr1 * std::cos (angle),
                c.x + pr2 * std::sin (angle), c.y - pr2 * std::cos (angle), 2.6f);
}

void BotanicaLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto col = b.findColour (juce::ToggleButton::tickColourId);
    auto r = b.getLocalBounds().toFloat();
    const auto pill = juce::Rectangle<float> (0.0f, r.getCentreY() - 9.0f, 34.0f, 18.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? col.withAlpha (0.9f) : track());
    g.fillRoundedRectangle (pill, 9.0f);
    g.setColour (on ? bg() : dim());
    g.fillEllipse (pill.getX() + (on ? 18.0f : 3.0f), pill.getY() + 3.0f, 12.0f, 12.0f);

    g.setColour (highlighted ? text() : (on ? text() : dim()));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (42.0f).toNearestInt(), juce::Justification::centredLeft);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& title, juce::Colour c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, text());
    addAndMakeVisible (slider);

    name.setText (title, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    if (auto* prm = s.getParameter (id))
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
StepGrid::StepGrid (BotanicaProcessor& p) : proc (p)
{
    for (int i = 0; i < 16; ++i)
    {
        const auto idx = (size_t) i;
        onAtt[idx] = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (BotanicaProcessor::stepOnId (i)),
                                                                   [this, i] (float v) { on[(size_t) i] = v > 0.5f; repaint(); });
        degAtt[idx] = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (BotanicaProcessor::stepDegId (i)),
                                                                    [this, i] (float v) { deg[(size_t) i] = (int) std::lround (v); repaint(); });
        onAtt[idx]->sendInitialUpdate();
        degAtt[idx]->sendInitialUpdate();
    }
    startTimerHz (30);
}

juce::Rectangle<float> StepGrid::cellBounds (int i) const
{
    const float w = (float) getWidth() / 16.0f;
    return { w * (float) i + 2.0f, 0.0f, w - 4.0f, (float) getHeight() };
}

int StepGrid::cellAt (juce::Point<float> p) const
{
    for (int i = 0; i < 16; ++i) if (cellBounds (i).contains (p)) return i;
    return -1;
}

void StepGrid::timerCallback()
{
    const int s = proc.engine.uiStep.load (std::memory_order_relaxed);
    if (s != current) { current = s; repaint(); }
}

void StepGrid::paint (juce::Graphics& g)
{
    for (int i = 0; i < 16; ++i)
    {
        auto c = cellBounds (i);
        const bool beat = (i % 4) == 0;
        auto top = c.removeFromTop (c.getHeight() * 0.46f).reduced (0.0f, 1.0f);
        auto bot = c.reduced (0.0f, 1.0f).withTrimmedTop (3.0f);
        const bool isOn = on[(size_t) i];

        g.setColour (isOn ? amber().withAlpha (0.92f) : (beat ? panelHi() : track().withAlpha (0.7f)));
        g.fillRoundedRectangle (top, 5.0f);
        if (i == current)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (top.reduced (0.5f), 5.0f, 2.0f);
        }

        // degree: a bar from the middle (0) up for positive, down for negative, and a number
        g.setColour (panel().brighter (0.1f));
        g.fillRoundedRectangle (bot, 5.0f);
        const float mid = bot.getCentreY();
        const int d = deg[(size_t) i];
        const float unit = (bot.getHeight() * 0.5f - 4.0f) / 7.0f;
        g.setColour (isOn ? amber() : dim().withAlpha (0.55f));
        if (d != 0)
        {
            const float y2 = mid - (float) d * unit;
            g.fillRoundedRectangle (bot.getX() + 5.0f, std::min (mid, y2), bot.getWidth() - 10.0f, std::abs (y2 - mid) + 2.0f, 2.0f);
        }
        else
            g.fillRect (bot.getX() + 5.0f, mid - 1.0f, bot.getWidth() - 10.0f, 2.0f);

        g.setColour (isOn ? text() : dim());
        g.setFont (11.0f);
        g.drawText ((d > 0 ? "+" : "") + juce::String (d), bot.removeFromBottom (14.0f).toNearestInt(), juce::Justification::centred);
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int i = cellAt (e.position);
    if (i < 0) return;
    const auto c = cellBounds (i);
    if (e.position.y < c.getHeight() * 0.46f)
    {
        onAtt[(size_t) i]->setValueAsCompleteGesture (on[(size_t) i] ? 0.0f : 1.0f);
    }
    else
    {
        dragCell = i; dragStartY = e.position.y; dragStartDeg = deg[(size_t) i];
        degAtt[(size_t) i]->beginGesture();
        if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
            degAtt[(size_t) i]->setValueAsPartOfGesture (0.0f);
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragCell < 0) return;
    const int d = juce::jlimit (-7, 7, dragStartDeg + (int) std::lround ((dragStartY - e.position.y) / 9.0f));
    degAtt[(size_t) dragCell]->setValueAsPartOfGesture ((float) d);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (dragCell >= 0) degAtt[(size_t) dragCell]->endGesture();
    dragCell = -1;
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = cellAt (e.position);
    if (i < 0) return;
    const int d = juce::jlimit (-7, 7, deg[(size_t) i] + (w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0)));
    degAtt[(size_t) i]->setValueAsCompleteGesture ((float) d);
}

//==============================================================================
Garden::Garden (BotanicaProcessor& p) : proc (p), readPos (p.engine.events.w.load())
{
    startTimerHz (30);
}

void Garden::timerCallback()
{
    time += 1.0f / 30.0f;
    bot::UiEvent ev;
    const float w = (float) getWidth(), h = (float) getHeight();
    while (proc.engine.events.pop (readPos, ev))
    {
        if (blobs.size() > 160) break;
        Blob b {};
        b.kind = ev.kind;
        b.x = w * (0.06f + 0.88f * ev.pitch) + (rnd.nextFloat() - 0.5f) * 14.0f;
        b.age = 0.0f;
        if (ev.kind == 0)       { b.y = h * (0.80f + 0.1f * rnd.nextFloat()); b.size = 5.0f + 11.0f * ev.size; b.life = 2.2f + rnd.nextFloat(); b.vy = 30.0f + 30.0f * rnd.nextFloat(); }
        else if (ev.kind == 1)  { b.y = h * 0.86f; b.size = 14.0f + 16.0f * ev.size; b.life = 2.8f; b.vy = 8.0f; }
        else                    { b.y = h * (0.35f + 0.3f * rnd.nextFloat()); b.size = 9.0f; b.life = 1.1f; b.vy = 14.0f; }
        blobs.push_back (b);
    }
    for (auto& b : blobs) { b.age += 1.0f / 30.0f; b.y -= b.vy / 30.0f; }
    blobs.erase (std::remove_if (blobs.begin(), blobs.end(), [] (const Blob& b) { return b.age >= b.life; }), blobs.end());
    repaint();
}

void Garden::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad (juce::Colour (0xff0d3a35), 0.0f, 0.0f, juce::Colour (0xff06130e), 0.0f, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 12.0f);

    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (r, 12.0f);
    g.reduceClipRegion (clip);

    // slow light rays
    for (int i = 0; i < 4; ++i)
    {
        const float x = r.getWidth() * (0.15f + 0.23f * (float) i) + 14.0f * std::sin (time * 0.3f + (float) i);
        juce::Path ray;
        ray.addTriangle (x, 0.0f, x + 46.0f, 0.0f, x - 40.0f + 30.0f * (float) i, r.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillPath (ray);
    }

    // water-lily pads / seabed hint
    g.setColour (leaf().withAlpha (0.10f));
    for (int i = 0; i < 7; ++i)
    {
        const float x = r.getWidth() * (0.05f + 0.15f * (float) i);
        g.fillEllipse (x, r.getBottom() - 16.0f, 38.0f, 10.0f);
    }

    for (const auto& b : blobs)
    {
        const float t = b.age / b.life;
        const float fade = (1.0f - t) * std::min (1.0f, b.age * 8.0f);
        if (b.kind == 0)
        {
            const float rad = b.size * (0.7f + 0.3f * t);
            g.setColour (aqua().withAlpha (0.16f * fade));
            g.fillEllipse (b.x - rad, b.y - rad, rad * 2, rad * 2);
            g.setColour (aqua().withAlpha (0.85f * fade));
            g.drawEllipse (b.x - rad, b.y - rad, rad * 2, rad * 2, 1.6f);
            g.setColour (juce::Colours::white.withAlpha (0.55f * fade));
            g.fillEllipse (b.x - rad * 0.45f, b.y - rad * 0.55f, rad * 0.35f, rad * 0.25f);
        }
        else if (b.kind == 1)
        {
            // a note blooms: concentric petals
            const float rad = b.size * (0.5f + 1.1f * t);
            for (int k = 0; k < 6; ++k)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) k / 6.0f + t;
                g.setColour (lilac().withAlpha (0.28f * fade));
                g.fillEllipse (b.x + rad * 0.6f * std::cos (a) - rad * 0.38f, b.y + rad * 0.6f * std::sin (a) - rad * 0.38f, rad * 0.76f, rad * 0.76f);
            }
            g.setColour (cream().withAlpha (0.8f * fade));
            g.fillEllipse (b.x - 3.0f, b.y - 3.0f, 6.0f, 6.0f);
        }
        else
        {
            for (int k = 0; k < 5; ++k)
            {
                const float a = 1.2566f * (float) k + b.age * 3.0f;
                const float d = b.size * (0.4f + 2.2f * t);
                g.setColour (amber().withAlpha (0.9f * fade));
                g.fillEllipse (b.x + d * std::cos (a) - 1.8f, b.y + d * std::sin (a) - 1.8f, 3.6f, 3.6f);
            }
        }
    }
}

//==============================================================================
Knob* BotanicaPanel::addKnob (const juce::String& id, const juce::String& title, juce::Colour c)
{
    knobs.push_back (std::make_unique<Knob> (proc.apvts, id, title, c));
    addAndMakeVisible (*knobs.back());
    return knobs.back().get();
}

void BotanicaPanel::addToggle (juce::ToggleButton& b, const juce::String& id, const juce::String& txt, juce::Colour c)
{
    b.setButtonText (txt);
    b.setColour (juce::ToggleButton::tickColourId, c);
    addAndMakeVisible (b);
    buttonAtts.push_back (std::make_unique<ButtonAtt> (proc.apvts, id, b));
}

BotanicaPanel::BotanicaPanel (BotanicaProcessor& p)
    : proc (p), steps (p), garden (p)
{
    setLookAndFeel (&look);

    // key / scale
    rootBox.addItemList (proc.apvts.getParameter ("root")->getAllValueStrings(), 1);
    scaleBox.addItemList (proc.apvts.getParameter ("scale")->getAllValueStrings(), 1);
    rateBox.addItemList (proc.apvts.getParameter ("grRate")->getAllValueStrings(), 1);
    rootA  = std::make_unique<ComboAtt> (proc.apvts, "root", rootBox);
    scaleA = std::make_unique<ComboAtt> (proc.apvts, "scale", scaleBox);
    rateA  = std::make_unique<ComboAtt> (proc.apvts, "grRate", rateBox);
    for (auto* c : { &rootBox, &scaleBox, &rateBox }) addAndMakeVisible (*c);

    modeBox.addItemList (proc.apvts.getParameter ("arMode")->getAllValueStrings(), 1);
    arRateBox.addItemList (proc.apvts.getParameter ("arRate")->getAllValueStrings(), 1);
    patBox.addItemList (proc.apvts.getParameter ("arPattern")->getAllValueStrings(), 1);
    modeA   = std::make_unique<ComboAtt> (proc.apvts, "arMode", modeBox);
    arRateA = std::make_unique<ComboAtt> (proc.apvts, "arRate", arRateBox);
    patA    = std::make_unique<ComboAtt> (proc.apvts, "arPattern", patBox);
    for (auto* cb : { &modeBox, &arRateBox, &patBox }) addAndMakeVisible (*cb);

    addToggle (snapBtn, "snap", "Snap played notes to scale", aqua());
    addToggle (bbKeyBtn, "bbKey", "Bubbles stay in key", aqua());
    addToggle (ambBtn, "ambGate", "Silent until you play", leaf());
    addToggle (sceneKeysBtn, "bdKeys", "Keys pick the scene", leaf());
    addToggle (patternBtn, "grPattern", "Pattern (off = cloud)", amber());
    addToggle (smearBtn, "gaOn", "Smear everything", rose());
    addToggle (freezeBtn, "gaFreeze", "Freeze the cloud", rose());

    // macro
    kMacro = { addKnob ("grow", "GROW", rose()), addKnob ("master", "MASTER", text()) };

    kPiano  = { addKnob ("pnLevel", "LEVEL", cream()), addKnob ("pnTone", "TONE", cream()), addKnob ("pnRel", "RELEASE", cream()),
                addKnob ("arOct", "OCTAVES", cream()), addKnob ("arRest", "RESTS", cream()), addKnob ("arSeed", "SEED", cream()) };
    kPad    = { addKnob ("pdLevel", "LEVEL", lilac()), addKnob ("pdAttack", "ATTACK", lilac()), addKnob ("pdRel", "RELEASE", lilac()),
                addKnob ("pdDetune", "DETUNE", lilac()), addKnob ("pdBright", "BRIGHT", lilac()), addKnob ("pdOct", "OCTAVE", lilac()) };
    kBubble = { addKnob ("bbLevel", "LEVEL", aqua()), addKnob ("bbDensity", "DENSITY", aqua()), addKnob ("bbRegister", "REGISTER", aqua()),
                addKnob ("bbSize", "SIZE", aqua()), addKnob ("bbBloop", "BLOOP", aqua()) };
    kScene  = { addKnob ("bdLevel", "LEVEL", leaf()), addKnob ("bdStream", "STREAM", leaf()), addKnob ("bdForest", "FOREST", leaf()),
                addKnob ("bdSpring", "SPRING", leaf()), addKnob ("bdDawn", "DAWN", leaf()), addKnob ("chLevel", "CHIRPS", leaf()),
                addKnob ("chRate", "CHIRP RATE", leaf()), addKnob ("nWindTone", "TUNED WIND", leaf()) };
    kSmear  = { addKnob ("gaMix", "MIX", rose()), addKnob ("gaSize", "GRAIN SIZE", rose()), addKnob ("gaDensity", "DENSITY", rose()),
                addKnob ("gaScatter", "SCATTER", rose()), addKnob ("gaPitch", "SHIMMER", rose()) };
    kSpace  = { addKnob ("ambIn", "FADE IN", leaf()), addKnob ("ambOut", "FADE OUT", leaf()),
                addKnob ("rvMix", "REVERB", sky()), addKnob ("rvSize", "SIZE", sky()), addKnob ("rvDamp", "DAMPING", sky()) };
    kGrain  = { addKnob ("grLevel", "LEVEL", amber()), addKnob ("grSize", "SIZE", amber()), addKnob ("grBurst", "PER STEP", amber()),
                addKnob ("grPos", "POSITION", amber()), addKnob ("grPosScatter", "POS. SCATTER", amber()),
                addKnob ("grPitchScatter", "PITCH SCATTER", amber()), addKnob ("grOct", "OCTAVES", amber()),
                addKnob ("grRev", "REVERSE", amber()), addKnob ("grSpread", "SPREAD", amber()), addKnob ("grDensity", "CLOUD DENS.", amber()) };

    addAndMakeVisible (steps);
    addAndMakeVisible (garden);

    // sound pickers (the lists are filled by refreshSounds: they include your own sounds)
    auto sourceBox = [this] (juce::ComboBox& box, const char* id, bool pad)
    {
        addAndMakeVisible (box);
        auto att = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (id), [&box] (float v)
        {
            box.setSelectedId ((int) std::lround (v) + 1, juce::dontSendNotification);
        });
        auto* attRaw = att.get();
        box.onChange = [this, &box, attRaw, pad, id]
        {
            const int sel = box.getSelectedId();
            if (sel == 900)
            {
                box.setSelectedId ((int) std::lround (proc.apvts.getRawParameterValue (id)->load()) + 1, juce::dontSendNotification);
                chooseFileFor (pad ? BotanicaProcessor::DropTarget::pad : BotanicaProcessor::DropTarget::piano);
            }
            else if (sel > 0) attRaw->setValueAsCompleteGesture ((float) (sel - 1));
        };
        return att;
    };
    pnSrcAtt = sourceBox (pnSoundBox, "pnSource", false);
    pdSrcAtt = sourceBox (pdSoundBox, "pdSource", true);
    pnSrcAtt->sendInitialUpdate(); pdSrcAtt->sendInitialUpdate();

    // presets
    addAndMakeVisible (presetBox);
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &delBtn, &soundsBtn }) addAndMakeVisible (*b);
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (+1); };
    saveBtn.onClick = [this] { savePresetDialog(); };
    delBtn.onClick = [this]
    {
        const auto n = proc.currentPresetName();
        if (proc.userPresetNames().contains (n)) { proc.deleteUserPreset (n); proc.loadFactoryPreset (0); }
        refreshPresets();
    };
    soundsBtn.onClick = [this] { showSoundsMenu(); };
    addAndMakeVisible (rollBtn);
    rollBtn.setTooltip ("Roll a new melody seed (Seeded Melody mode)");
    rollBtn.onClick = [this]
    {
        juce::Random rnd;
        if (auto* seed = proc.apvts.getParameter ("arSeed"))
        {
            int s = rnd.nextInt (juce::Range<int> (1, 100));
            while (s == (int) std::lround (proc.apvts.getRawParameterValue ("arSeed")->load())) s = rnd.nextInt (juce::Range<int> (1, 100));
            seed->setValueNotifyingHost (seed->convertTo0to1 ((float) s));
        }
        if (auto* mode = proc.apvts.getParameter ("arMode")) mode->setValueNotifyingHost (mode->convertTo0to1 (2.0f));
    };
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1000) proc.loadUserPreset (presetBox.getText());
        else if (id > 0) proc.loadFactoryPreset (id - 1);
        refreshPresets();
    };

    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    statusLabel.setColour (juce::Label::textColourId, dim());
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (statusLabel);

    refreshSounds();
    refreshPresets();
    startTimerHz (4);

    setSize (1100, 1164);          // last: this triggers resized(), which positions all the children above
}

BotanicaPanel::~BotanicaPanel()
{
    setLookAndFeel (nullptr);
}

void BotanicaPanel::layoutKnobs (juce::Rectangle<int> area, const std::vector<Knob*>& ks, int cols, int cellH)
{
    const int cw = area.getWidth() / cols;
    for (size_t i = 0; i < ks.size(); ++i)
        ks[i]->setBounds (area.getX() + ((int) i % cols) * cw, area.getY() + ((int) i / cols) * cellH, cw, cellH);
}

void BotanicaPanel::paint (juce::Graphics& g)
{
    g.fillAll (bg());

    // header
    g.setColour (text());
    g.setFont (juce::Font (juce::FontOptions (32.0f, juce::Font::bold)));
    g.drawText ("BOTANICA", 62, 14, 260, 40, juce::Justification::centredLeft);
    g.setColour (dim());
    g.setFont (13.0f);
    g.drawText (juce::String::fromUTF8 ("piano  \xc2\xb7  pads  \xc2\xb7  nature  \xc2\xb7  bubbles  \xc2\xb7  granulizer"), 64, 52, 330, 18, juce::Justification::centredLeft);

    // leaf logo
    {
        juce::Path leafP;
        leafP.startNewSubPath (22.0f, 62.0f);
        leafP.cubicTo (14.0f, 38.0f, 30.0f, 20.0f, 52.0f, 18.0f);
        leafP.cubicTo (54.0f, 40.0f, 44.0f, 58.0f, 22.0f, 62.0f);
        leafP.closeSubPath();
        g.setColour (leaf());
        g.fillPath (leafP);
        g.setColour (bg());
        g.drawLine (24.0f, 58.0f, 46.0f, 26.0f, 2.0f);
        g.setColour (aqua());
        g.fillEllipse (44.0f, 52.0f, 9.0f, 9.0f);
        g.fillEllipse (53.0f, 44.0f, 5.0f, 5.0f);
    }

    for (const auto& p : panels)
    {
        g.setColour (panel());
        g.fillRoundedRectangle (p.r.toFloat(), 12.0f);
        g.setColour (p.colour);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) p.r.getX() + 14.0f, (float) p.r.getY() + 13.0f, 5.0f, 15.0f), 2.5f);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (p.title, p.r.getX() + 26, p.r.getY() + 10, p.r.getWidth() - 40, 22, juce::Justification::centredLeft);
    }

    g.setColour (dim());
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText ("KEY", keyLabelArea, juce::Justification::centredLeft);
    g.drawText ("SCALE", scaleLabelArea, juce::Justification::centredLeft);
    g.drawText ("STEP RATE", rateLabelArea, juce::Justification::centredLeft);
    g.setColour (dim());
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawFittedText ("Everything you hear is cut into grains and smeared into a cloud. Hold a chord, switch it on, then try Freeze.", smearHintArea, juce::Justification::topLeft, 4);
    g.drawText ("MODE", modeLabelArea, juce::Justification::centredLeft);
    g.drawText ("RATE", arRateLabelArea, juce::Justification::centredLeft);
    g.drawText ("PATTERN", patLabelArea, juce::Justification::centredLeft);
    g.drawText ("PATTERN  (click = on/off, drag = scale degree)", stepsLabelArea, juce::Justification::centredLeft);
}

void BotanicaPanel::resized()
{
    panels.clear();
    const int m = 16, top = 104;

    // header controls
    keyLabelArea   = { 420, 14, 60, 14 };   rootBox.setBounds (420, 30, 70, 28);
    scaleLabelArea = { 500, 14, 100, 14 };  scaleBox.setBounds (500, 30, 160, 28);
    snapBtn.setBounds (680, 32, 230, 24);
    layoutKnobs ({ 930, 4, 160, 92 }, kMacro, 2, 92);
    prevBtn.setBounds (420, 68, 28, 28);   presetBox.setBounds (452, 68, 250, 28);   nextBtn.setBounds (706, 68, 28, 28);
    saveBtn.setBounds (742, 68, 64, 28);   delBtn.setBounds (812, 68, 70, 28);

    const int cell = 100;
    auto body = [] (juce::Rectangle<int> r) { return r.withTrimmedTop (38).reduced (8, 0); };

    // row 1: piano, pads, bubbles
    const int r1h = 280;
    juce::Rectangle<int> pPiano (m, top, 370, r1h), pPad (pPiano.getRight() + 8, top, 330, r1h),
                         pBub (pPad.getRight() + 8, top, getWidth() - m - (pPad.getRight() + 8), r1h);
    panels.push_back ({ "PIANO", cream(), pPiano });
    panels.push_back ({ "PADS", lilac(), pPad });
    panels.push_back ({ "BUBBLES", aqua(), pBub });

    // piano: three drop-downs, then the knobs
    {
        const int cw = (pPiano.getWidth() - 28 - 16) / 3;
        const int y = pPiano.getY() + 52;
        modeLabelArea  = { pPiano.getX() + 14, y - 14, cw, 14 };            modeBox.setBounds (pPiano.getX() + 14, y + 2, cw, 28);
        arRateLabelArea = { pPiano.getX() + 22 + cw, y - 14, cw, 14 };      arRateBox.setBounds (pPiano.getX() + 22 + cw, y + 2, cw, 28);
        patLabelArea   = { pPiano.getX() + 30 + 2 * cw, y - 14, cw, 14 };   patBox.setBounds (pPiano.getX() + 30 + 2 * cw, y + 2, cw, 28);
        layoutKnobs (juce::Rectangle<int> (pPiano.getX() + 8, pPiano.getY() + 92, pPiano.getWidth() - 16, 2 * 92), kPiano, 3, 92);
    }
    pianoRect = pPiano; padRect = pPad;
    pnSoundBox.setBounds (pPiano.getX() + 100, pPiano.getY() + 8, pPiano.getWidth() - 100 - 14 - 62, 26);
    rollBtn.setBounds (pPiano.getRight() - 14 - 54, pPiano.getY() + 8, 54, 26);
    pdSoundBox.setBounds (pPad.getX() + 70, pPad.getY() + 8, pPad.getWidth() - 70 - 14, 26);
    layoutKnobs (body (pPad), kPad, 3, 120);
    layoutKnobs (body (pBub), kBubble, 3, 100);
    bbKeyBtn.setBounds (pBub.getX() + 24, pBub.getBottom() - 34, 220, 24);

    // row 2: nature recordings, ambience + reverb
    const int y2 = top + r1h + 8, r2h = 246;
    juce::Rectangle<int> pScene (m, y2, 560, r2h), pSpace (pScene.getRight() + 8, y2, getWidth() - m - (pScene.getRight() + 8), r2h);
    panels.push_back ({ "NATURE RECORDINGS", leaf(), pScene });
    panels.push_back ({ "AMBIENCE + SPACE", sky(), pSpace });
    sceneRect = pScene;
    soundsBtn.setBounds (pScene.getRight() - 104, pScene.getY() + 8, 92, 26);
    statusLabel.setBounds (pScene.getX() + 230, pScene.getY() + 8, pScene.getWidth() - 230 - 112, 26);
    layoutKnobs (body (pScene), kScene, 4, cell);
    ambBtn.setBounds (pSpace.getX() + 24, pSpace.getY() + 42, 220, 24);
    sceneKeysBtn.setBounds (pSpace.getX() + 260, pSpace.getY() + 42, 200, 24);
    layoutKnobs (juce::Rectangle<int> (pSpace.getX() + 8, pSpace.getY() + 84, pSpace.getWidth() - 16, 140), kSpace, 5, 140);

    // row 3: granulizer + pond
    const int y3 = y2 + r2h + 8, r3h = 348;
    juce::Rectangle<int> pGr (m, y3, getWidth() - 2 * m, r3h);
    panels.push_back ({ "GRANULIZER", amber(), pGr });
    layoutKnobs (juce::Rectangle<int> (pGr.getX() + 8, pGr.getY() + 34, 640, 2 * cell), kGrain, 5, cell);
    rateLabelArea = { pGr.getX() + 668, pGr.getY() + 48, 120, 14 };
    rateBox.setBounds (pGr.getX() + 668, pGr.getY() + 66, 110, 28);
    patternBtn.setBounds (pGr.getX() + 668, pGr.getY() + 108, 176, 24);
    garden.setBounds (pGr.getX() + 820, pGr.getY() + 14, pGr.getWidth() - 820 - 14, 2 * cell + 20);
    stepsLabelArea = { pGr.getX() + 18, pGr.getY() + 34 + 2 * cell + 18, 480, 14 };
    steps.setBounds (pGr.getX() + 14, stepsLabelArea.getBottom() + 4, pGr.getWidth() - 28, pGr.getBottom() - (stepsLabelArea.getBottom() + 4) - 14);

    // row 4: smear (the granulizer on everything)
    const int y4 = y3 + r3h + 8, r4h = getHeight() - y4 - m;
    juce::Rectangle<int> pSm (m, y4, getWidth() - 2 * m, r4h);
    panels.push_back ({ "SMEAR  -  granulize the whole plugin", rose(), pSm });
    panels.back().title = "SMEAR - granulize the whole plugin";
    layoutKnobs (juce::Rectangle<int> (pSm.getX() + 8, pSm.getY() + 38, 640, 100), kSmear, 5, 100);
    smearBtn.setBounds (pSm.getX() + 680, pSm.getY() + 52, 200, 24);
    freezeBtn.setBounds (pSm.getX() + 680, pSm.getY() + 90, 200, 24);
    smearHintArea = { pSm.getX() + 880, pSm.getY() + 46, pSm.getWidth() - 880 - 18, 84 };
}

//==============================================================================
BotanicaEditor::BotanicaEditor (BotanicaProcessor& p) : AudioProcessorEditor (&p), panel (p)
{
    addAndMakeVisible (panel);
    limits.setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    limits.setSizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 3 / 2, kBaseH * 3 / 2);
    setConstrainer (&limits);
    setResizable (true, true);
    setSize (kBaseW * 7 / 10, kBaseH * 7 / 10);        // opens at 70 %: fits a laptop screen; drag a corner for any size
}

void BotanicaEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseW;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, kBaseW, kBaseH);
}


//==============================================================================
//  sounds: drag and drop, pickers, presets
//==============================================================================
BotanicaPanel::Hit BotanicaPanel::hitAt (juce::Point<int> p) const
{
    using T = BotanicaProcessor::DropTarget;
    Hit h;
    auto set = [&h] (T t, juce::Rectangle<int> r, const juce::String& l) { h.valid = true; h.target = t; h.area = r; h.label = l; };
    if (pianoRect.contains (p))      set (T::piano, pianoRect, "Drop to use this sound for the PIANO layer");
    else if (padRect.contains (p))   set (T::pad, padRect, "Drop to use this sound for the PADS layer");
    else
    {
        static const T beds[4] = { T::bed0, T::bed1, T::bed2, T::bed3 };
        static const char* names[4] = { "STREAM", "FOREST", "SPRING", "DAWN" };
        for (int i = 0; i < 4; ++i)
            if (kScene[(size_t) i + 1]->getBounds().contains (p))
                set (beds[i], kScene[(size_t) i + 1]->getBounds().expanded (4), juce::String ("Drop to replace ") + names[i]);
        if (kScene[5]->getBounds().contains (p))
            set (T::chirps, kScene[5]->getBounds().expanded (4), "Drop to replace the bird chirps");
    }
    return h;
}

bool BotanicaPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (BotanicaProcessor::isAudioFile (juce::File (f))) return true;
    return false;
}

void BotanicaPanel::fileDragEnter (const juce::StringArray&, int x, int y) { hover = hitAt ({ x, y }); repaint(); }
void BotanicaPanel::fileDragMove  (const juce::StringArray&, int x, int y) { hover = hitAt ({ x, y }); repaint(); }
void BotanicaPanel::fileDragExit  (const juce::StringArray&) { hover = {}; repaint(); }

void BotanicaPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    hover = {};
    const auto h = hitAt ({ x, y });
    repaint();
    if (! h.valid)
    {
        flash = "Drop onto Piano, Pads, a nature knob or Chirps"; flashUntil = juce::Time::getMillisecondCounterHiRes() + 4000;
        return;
    }
    for (auto& f : files)
        if (BotanicaProcessor::isAudioFile (juce::File (f))) { proc.importDrop (h.target, juce::File (f)); break; }
}

void BotanicaPanel::chooseFileFor (BotanicaProcessor::DropTarget t)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), "*.wav;*.mp3;*.ogg;*.flac;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, t] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.importDrop (t, f);
                          });
}

void BotanicaPanel::showSoundsMenu()
{
    using T = BotanicaProcessor::DropTarget;
    juce::PopupMenu m;
    m.addSectionHeader ("Replace a nature sound with a file");
    m.addItem (1, "Stream...");  m.addItem (2, "Forest...");  m.addItem (3, "Spring...");  m.addItem (4, "Dawn...");
    m.addItem (5, "Bird chirps...");
    m.addSeparator();
    m.addItem (9, "Back to the built-in recordings");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&soundsBtn), [this] (int r)
    {
        static const T t[5] = { T::bed0, T::bed1, T::bed2, T::bed3, T::chirps };
        if (r >= 1 && r <= 5) chooseFileFor (t[r - 1]);
        else if (r == 9) proc.resetRecordings();
    });
}

void BotanicaPanel::refreshSounds()
{
    for (int pad = 0; pad < 2; ++pad)
    {
        auto& box = pad ? pdSoundBox : pnSoundBox;
        const auto names = proc.soundNames (pad == 1);
        const int cur = (int) std::lround (proc.apvts.getRawParameterValue (pad ? "pdSource" : "pnSource")->load());
        box.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i) box.addItem (names[i], i + 1);
        box.addSeparator();
        box.addItem ("Load a sound file...", 900);
        box.setSelectedId (cur + 1, juce::dontSendNotification);
    }
    static const char* beds[4] = { "STREAM", "FOREST", "SPRING", "DAWN" };
    for (int i = 0; i < 4; ++i)
    {
        const auto n = proc.bedName (i);
        kScene[(size_t) i + 1]->setTitle (n.isEmpty() ? juce::String (beds[i]) : n.substring (0, 12).toUpperCase());
    }
    const auto cn = proc.chirpName();
    kScene[5]->setTitle (cn.isEmpty() ? juce::String ("CHIRPS") : cn.substring (0, 12).toUpperCase());
}

void BotanicaPanel::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    const auto n = BotanicaProcessor::factoryPresets().size();
    for (size_t i = 0; i < n; ++i) presetBox.addItem (BotanicaProcessor::factoryPresets()[i].name, (int) i + 1);
    const auto users = proc.userPresetNames();
    if (users.size() > 0)
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("My presets");
        for (int i = 0; i < users.size(); ++i) presetBox.addItem (users[i], 1000 + i);
    }
    const auto cur = proc.currentPresetName();
    int id = 0;
    for (size_t i = 0; i < n; ++i) if (BotanicaProcessor::factoryPresets()[i].name == cur) id = (int) i + 1;
    for (int i = 0; i < users.size(); ++i) if (users[i] == cur) id = 1000 + i;
    if (id > 0) presetBox.setSelectedId (id, juce::dontSendNotification);
    else presetBox.setText (cur, juce::dontSendNotification);
    delBtn.setEnabled (users.contains (cur));
}

void BotanicaPanel::stepPreset (int dir)
{
    juce::StringArray all;
    for (auto& f : BotanicaProcessor::factoryPresets()) all.add (f.name);
    const int nFactory = all.size();
    all.addArray (proc.userPresetNames());
    int idx = all.indexOf (proc.currentPresetName());
    idx = (idx < 0 ? 0 : idx + dir + all.size()) % all.size();
    if (idx < nFactory) proc.loadFactoryPreset (idx); else proc.loadUserPreset (all[idx]);
    refreshPresets();
}

void BotanicaPanel::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Give your preset a name. It is stored on this computer and appears in the preset list.",
                                     juce::MessageBoxIconType::NoIcon, this);
    const auto cur = proc.currentPresetName();
    w->addTextEditor ("name", proc.userPresetNames().contains (cur) ? cur : juce::String(), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BotanicaPanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (r == 1 && safe != nullptr)
        {
            const auto name = w->getTextEditorContents ("name").trim();
            if (name.isNotEmpty()) { safe->proc.saveUserPreset (name); safe->refreshPresets(); }
        }
    }), true);
}

void BotanicaPanel::timerCallback()
{
    // rebuild the lists when something changed (an import finished, a preset was saved, the host changed a program ...)
    juce::String sig = proc.currentPresetName() + "|" + proc.userPresetNames().joinIntoString (",") + "|"
                     + proc.soundNames (false).joinIntoString (",") + "|" + proc.chirpName();
    for (int i = 0; i < 4; ++i) sig << "|" << proc.bedName (i);
    for (auto* id : { "pnSource", "pdSource" }) sig << "|" << (int) proc.apvts.getRawParameterValue (id)->load();
    if (sig != lastSig) { lastSig = sig; refreshSounds(); refreshPresets(); }

    const auto now = juce::Time::getMillisecondCounterHiRes();
    juce::String msg = now < flashUntil ? flash : proc.statusText();
    if (msg.isEmpty()) msg = "Drop a sound onto Piano, Pads or a nature knob";
    statusLabel.setText (msg, juce::dontSendNotification);
}

void BotanicaPanel::paintOverChildren (juce::Graphics& g)
{
    if (! hover.valid) return;
    const auto r = hover.area.toFloat();
    g.setColour (aqua().withAlpha (0.16f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (aqua());
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.5f);
    g.setColour (text());
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText (hover.label, hover.area.reduced (6), juce::Justification::centred);
}
