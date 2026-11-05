#include "BotanicaProcessor.h"
#include "BotanicaEditor.h"
#include "BotanicaSamples.h"
#include "BotanicaImport.h"
#include <BotanicaData.h>
#include <mutex>

namespace
{
// all plugin instances share one decoded copy of the piano
std::shared_ptr<const bot::PianoBank> sharedPiano()
{
    static std::mutex m;
    static std::weak_ptr<const bot::PianoBank> weak;
    std::lock_guard<std::mutex> lock (m);
    if (auto sp = weak.lock()) return sp;
    std::shared_ptr<const bot::PianoBank> b = bot::loadPianoPak (BotanicaData::piano_pak, (size_t) BotanicaData::piano_pakSize);
    weak = b;
    return b;
}

const std::vector<bot::InstIndex>& instIndex()
{
    static const std::vector<bot::InstIndex> idx = bot::indexInstPak (BotanicaData::inst_pak, (size_t) BotanicaData::inst_pakSize);
    return idx;
}

std::shared_ptr<const bot::PianoBank> builtinBank (int i)             // i = 0..6 (Saws .. Atmos); decoded on first use, shared by all instances
{
    static std::mutex m;
    static std::weak_ptr<const bot::PianoBank> weak[16];
    std::lock_guard<std::mutex> lock (m);
    const auto& idx = instIndex();
    if (i < 0 || i >= (int) idx.size() || i >= 16) return nullptr;
    if (auto sp = weak[i].lock()) return sp;
    std::shared_ptr<const bot::PianoBank> b = bot::decodeInst (BotanicaData::inst_pak, (size_t) BotanicaData::inst_pakSize, idx[(size_t) i]);
    weak[i] = b;
    return b;
}

std::shared_ptr<const bot::NatureBank> sharedNature()
{
    static std::mutex m;
    static std::weak_ptr<const bot::NatureBank> weak;
    std::lock_guard<std::mutex> lock (m);
    if (auto sp = weak.lock()) return sp;
    std::shared_ptr<const bot::NatureBank> b = bot::loadNaturePak (BotanicaData::nature_pak, (size_t) BotanicaData::nature_pakSize);
    weak = b;
    return b;
}
}

juce::AudioProcessorValueTreeState::ParameterLayout BotanicaProcessor::createLayout()
{
    using namespace juce;
    using PID  = ParameterID;
    using FA   = AudioParameterFloatAttributes;
    using IA   = AudioParameterIntAttributes;

    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct  = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto sec  = [] (float v, int) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 1) + " s"; };
    auto ms   = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto ct   = [] (float v, int) { return String (roundToInt (v)) + " ct"; };
    auto db   = [] (float v, int) { return String (v, 1) + " dB"; };
    auto rate = [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " /s"; };

    auto flt = [&] (const char* id, const char* name, float lo, float hi, float def, float skew,
                    std::function<String (float, int)> fmt)
    {
        NormalisableRange<float> r (lo, hi);
        if (skew != 1.0f) r.setSkewForCentre (lo + (hi - lo) * skew);       // skew = where the middle of the knob lands
        p.push_back (std::make_unique<AudioParameterFloat> (PID { id, 1 }, name, r, def, FA().withStringFromValueFunction (fmt)));
    };
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (PID { id, 1 }, name, items, def));
    };
    auto toggle = [&] (const String& id, const String& name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (PID { id, 1 }, name, def));
    };
    auto integer = [&] (const String& id, const String& name, int lo, int hi, int def, std::function<String (int, int)> fmt)
    {
        p.push_back (std::make_unique<AudioParameterInt> (PID { id, 1 }, name, lo, hi, def, IA().withStringFromValueFunction (fmt)));
    };

    StringArray keys, scaleNames;
    for (int i = 0; i < 12; ++i) keys.add (bot::noteName (i));
    for (int i = 0; i < bot::kNumScales; ++i) scaleNames.add (bot::scales()[i].name);

    // ---- key / scale / macro
    choice ("root", "Key", keys, 0);
    choice ("scale", "Scale", scaleNames, 0);
    toggle ("snap", "Snap Played Notes To Scale", false);
    {
        StringArray names { "Saws", "Lead", "Lead 2", "Organ", "Organ 2", "Strings", "Atmos", "Custom 1", "Custom 2", "Custom 3", "Custom 4" };
        StringArray pn { "Piano" }, pd { "Synth Pad" };
        pn.addArray (names); pd.addArray (names);
        choice ("pnSource", "Piano Layer Sound", pn, 0);
        choice ("pdSource", "Pad Layer Sound", pd, 0);
    }
    flt ("grow", "Grow", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("master", "Master", -24.0f, 6.0f, 0.0f, 0.8f, db);

    // ---- piano
    flt ("pnLevel", "Piano Level", 0.0f, 1.0f, 0.8f, 0.5f, pct);
    flt ("pnTone", "Piano Tone", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("pnRel", "Piano Release", 0.05f, 3.0f, 0.4f, 0.3f, sec);

    // ---- pad
    flt ("pdLevel", "Pad Level", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("pdAttack", "Pad Attack", 0.01f, 8.0f, 0.6f, 0.4f, sec);
    flt ("pdRel", "Pad Release", 0.05f, 10.0f, 2.5f, 0.4f, sec);
    flt ("pdDetune", "Pad Detune", 0.0f, 40.0f, 12.0f, 0.4f, ct);
    flt ("pdBright", "Pad Brightness", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    integer ("pdOct", "Pad Octave", -1, 1, 0, [] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " oct"; });

    // ---- bubbles
    flt ("bbLevel", "Bubble Level", 0.0f, 1.0f, 0.3f, 0.5f, pct);
    flt ("bbDensity", "Bubble Density", 0.2f, 30.0f, 2.5f, 0.2f, rate);
    flt ("bbRegister", "Bubble Register", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("bbSize", "Bubble Size", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("bbBloop", "Bubble Bloop", 0.0f, 1.0f, 0.3f, 0.5f, pct);
    toggle ("bbKey", "Bubbles In Key", true);

    // ---- nature
    flt ("nWater", "Water", 0.0f, 1.0f, 0.0f, 0.5f, pct);
    flt ("nRain", "Rain", 0.0f, 1.0f, 0.0f, 0.5f, pct);
    flt ("nWind", "Wind", 0.0f, 1.0f, 0.0f, 0.5f, pct);
    flt ("nWindTone", "Tuned Wind", 0.0f, 1.0f, 0.2f, 0.5f, pct);
    flt ("nBirds", "Birds", 0.0f, 1.0f, 0.0f, 0.5f, pct);
    flt ("nCrickets", "Crickets", 0.0f, 1.0f, 0.0f, 0.5f, pct);
    toggle ("birdsKey", "Birds In Key", true);

    // ---- scene: recordings + gating
    toggle ("ambGate", "Ambience Only While Playing", true);
    flt ("ambIn", "Fade In", 0.05f, 10.0f, 2.0f, 0.3f, sec);
    flt ("ambOut", "Fade Out", 0.05f, 15.0f, 4.0f, 0.3f, sec);
    flt ("bdLevel", "Recordings Level", 0.0f, 1.0f, 0.6f, 0.5f, pct);
    flt ("bdStream", "Stream", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("bdForest", "Forest", 0.0f, 1.0f, 0.35f, 0.5f, pct);
    flt ("bdSpring", "Spring Morning", 0.0f, 1.0f, 0.6f, 0.5f, pct);
    flt ("bdDawn", "Dawn Birds", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    toggle ("bdKeys", "Keys Pick The Scene", false);
    flt ("chLevel", "Chirp Level", 0.0f, 1.0f, 0.6f, 0.5f, pct);
    flt ("chRate", "Chirp Rate", 0.02f, 4.0f, 0.35f, 0.25f, rate);

    // ---- piano performance
    choice ("arMode", "Piano Mode", StringArray { "Direct", "Chord Arp", "Seeded Melody" }, 1);
    choice ("arRate", "Piano Rate", StringArray { "1/4", "1/8", "1/16", "1/32" }, 1);
    choice ("arPattern", "Arp Pattern", StringArray { "Up", "Down", "Up-Down", "Random" }, 2);
    integer ("arOct", "Arp Octaves", 1, 3, 2, [] (int v, int) { return String (v) + (v == 1 ? " oct" : " oct"); });
    flt ("arRest", "Piano Rests", 0.0f, 0.8f, 0.15f, 0.4f, pct);
    integer ("arSeed", "Melody Seed", 1, 99, 7, [] (int v, int) { return String (v); });

    // ---- smear: the granulizer on the whole plugin
    toggle ("gaOn", "Smear Everything", false);
    toggle ("gaFreeze", "Smear Freeze", false);
    flt ("gaMix", "Smear Mix", 0.0f, 1.0f, 0.6f, 0.5f, pct);
    flt ("gaSize", "Smear Grain Size", 20.0f, 1000.0f, 220.0f, 0.25f, ms);
    flt ("gaDensity", "Smear Density", 2.0f, 80.0f, 24.0f, 0.3f, rate);
    flt ("gaScatter", "Smear Scatter", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("gaPitch", "Smear Shimmer", 0.0f, 1.0f, 0.0f, 0.5f, pct);

    // ---- granulizer
    flt ("grLevel", "Grain Level", 0.0f, 1.0f, 0.25f, 0.5f, pct);
    flt ("grSize", "Grain Size", 20.0f, 500.0f, 120.0f, 0.2f, ms);
    integer ("grBurst", "Grains Per Step", 1, 8, 3, [] (int v, int) { return String (v); });
    flt ("grPos", "Grain Position", 0.0f, 1.0f, 0.1f, 0.5f, pct);
    flt ("grPosScatter", "Position Scatter", 0.0f, 1.0f, 0.25f, 0.5f, pct);
    integer ("grPitchScatter", "Pitch Scatter", 0, 7, 2, [] (int v, int) { return String (v) + (v == 1 ? " step" : " steps"); });
    flt ("grOct", "Octave Jumps", 0.0f, 1.0f, 0.1f, 0.5f, pct);
    flt ("grRev", "Reverse Grains", 0.0f, 1.0f, 0.15f, 0.5f, pct);
    flt ("grSpread", "Grain Spread", 0.0f, 1.0f, 0.5f, 0.5f, pct);
    flt ("grDensity", "Cloud Density", 1.0f, 60.0f, 12.0f, 0.25f, rate);
    choice ("grRate", "Step Rate", StringArray { "1/4", "1/8", "1/16", "1/32" }, 1);
    toggle ("grPattern", "Pattern Mode", true);

    const bot::Params def;
    for (int i = 0; i < 16; ++i) toggle (stepOnId (i), "Step " + String (i + 1) + " On", def.stepOn[(size_t) i]);
    for (int i = 0; i < 16; ++i)
        integer (stepDegId (i), "Step " + String (i + 1) + " Degree", -7, 7, def.stepDeg[(size_t) i],
                 [] (int v, int) { return (v > 0 ? "+" : "") + String (v); });

    // ---- reverb
    flt ("rvMix", "Reverb Mix", 0.0f, 1.0f, 0.35f, 0.5f, pct);
    flt ("rvSize", "Reverb Size", 0.0f, 1.0f, 0.8f, 0.5f, pct);
    flt ("rvDamp", "Reverb Damping", 0.0f, 1.0f, 0.5f, 0.5f, pct);

    return { p.begin(), p.end() };
}

BotanicaProcessor::BotanicaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Botanica loader"),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pianoCur = sharedPiano();
    natureCur = sharedNature();
    engine.setPiano (pianoCur);
    engine.setNature (natureCur);
    startThread();
}

BotanicaProcessor::~BotanicaProcessor()
{
    signalThreadShouldExit();
    notify();
    stopThread (8000);
}

bool BotanicaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BotanicaProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, kChunk);
    pushSources();
    engine.setParams (readParams());
}

bot::Params BotanicaProcessor::readParams() const
{
    auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    auto b = [&] (const juce::String& id) { return v (id) > 0.5f; };
    auto i = [&] (const juce::String& id) { return (int) std::lround (v (id)); };

    bot::Params p;
    p.root = i ("root"); p.scale = i ("scale"); p.snap = b ("snap");
    p.grow = v ("grow"); p.masterDb = v ("master");

    p.pnLevel = v ("pnLevel"); p.pnTone = v ("pnTone"); p.pnRel = v ("pnRel");

    p.pdLevel = v ("pdLevel"); p.pdAttack = v ("pdAttack"); p.pdRel = v ("pdRel");
    p.pdDetune = v ("pdDetune"); p.pdBright = v ("pdBright"); p.pdOct = i ("pdOct");

    p.bbLevel = v ("bbLevel"); p.bbDensity = v ("bbDensity"); p.bbRegister = v ("bbRegister");
    p.bbSize = v ("bbSize"); p.bbBloop = v ("bbBloop"); p.bbKey = b ("bbKey");

    p.nWater = v ("nWater"); p.nRain = v ("nRain"); p.nWind = v ("nWind");
    p.nWindTone = v ("nWindTone"); p.nBirds = v ("nBirds"); p.nCrickets = v ("nCrickets");
    p.birdsKey = b ("birdsKey");

    p.grLevel = v ("grLevel"); p.grSizeMs = v ("grSize"); p.grBurst = (float) i ("grBurst");
    p.grPos = v ("grPos"); p.grPosScatter = v ("grPosScatter"); p.grPitchScatter = (float) i ("grPitchScatter");
    p.grOct = v ("grOct"); p.grRev = v ("grRev"); p.grSpread = v ("grSpread"); p.grDensity = v ("grDensity");
    p.grRate = i ("grRate"); p.grPattern = b ("grPattern");
    for (int s = 0; s < 16; ++s)
    {
        p.stepOn[(size_t) s]  = b (stepOnId (s));
        p.stepDeg[(size_t) s] = i (stepDegId (s));
    }

    p.ambGate = b ("ambGate"); p.ambIn = v ("ambIn"); p.ambOut = v ("ambOut");
    p.bdLevel = v ("bdLevel"); p.bdKeys = b ("bdKeys");
    p.bdGain = { { v ("bdStream"), v ("bdForest"), v ("bdSpring"), v ("bdDawn") } };
    p.chLevel = v ("chLevel"); p.chRate = v ("chRate");
    p.arMode = i ("arMode"); p.arRate = i ("arRate"); p.arPattern = i ("arPattern");
    p.arOct = i ("arOct"); p.arRest = v ("arRest"); p.arSeed = i ("arSeed");
    p.gaOn = b ("gaOn"); p.gaFreeze = b ("gaFreeze"); p.gaMix = v ("gaMix"); p.gaSizeMs = v ("gaSize");
    p.gaDensity = v ("gaDensity"); p.gaScatter = v ("gaScatter"); p.gaPitch = v ("gaPitch");

    p.rvMix = v ("rvMix"); p.rvSize = v ("rvSize"); p.rvDamp = v ("rvDamp");
    return p;
}

void BotanicaProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                     engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())               engine.noteOff (m.getNoteNumber());
    else if (m.isController())            engine.controller (m.getControllerNumber(), m.getControllerValue());
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void BotanicaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || n == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    pushSources();
    engine.setParams (readParams());

    // host tempo / position drive the grain pattern
    bool playing = false, hasPpq = false;
    double bpm = 120.0, ppq = 0.0;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getBpm())         bpm = *t;
            if (auto q = pos->getPpqPosition()) { ppq = *q; hasPpq = true; }
        }
    const double ppqPerSample = bpm / 60.0 / juce::jmax (1.0, getSampleRate());

    auto renderRange = [&] (int start, int len)
    {
        while (len > 0)
        {
            const int c = juce::jmin (len, kChunk);
            engine.setTransport (playing, bpm, hasPpq, ppq + (double) start * ppqPerSample);
            engine.render (L + start, R + start, c);
            start += c; len -= c;
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos) { renderRange (pos, t - pos); pos = t; }
        handleMidi (meta.getMessage());
    }
    renderRange (pos, n - pos);
}

juce::AudioProcessorEditor* BotanicaProcessor::createEditor()
{
    return new BotanicaEditor (*this);
}

void BotanicaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BotanicaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            applyStateSlots();
            sendChangeMessage();
        }
}

//==============================================================================
//  sounds: loading, importing, drag and drop
//==============================================================================
void BotanicaProcessor::pushSources()
{
    auto p = std::atomic_load (&pianoCur);
    if (p.get() != pushedPiano) { engine.setPiano (p); pushedPiano = p.get(); }
    auto q = std::atomic_load (&padCur);
    if (q.get() != pushedPad) { engine.setPadBank (q); pushedPad = q.get(); }
    auto n = std::atomic_load (&natureCur);
    if (n.get() != pushedNature) { engine.setNature (n); pushedNature = n.get(); }
}

void BotanicaProcessor::retire (std::shared_ptr<const void> p)
{
    if (! p) return;
    std::lock_guard<std::mutex> l (retiredMu);
    retired.push_back (std::move (p));
    if (retired.size() > 8) retired.erase (retired.begin());
}

void BotanicaProcessor::setStatus (const juce::String& t)
{
    { std::lock_guard<std::mutex> l (mu); status = t; }
    sendChangeMessage();
}

juce::String BotanicaProcessor::statusText() const
{
    std::lock_guard<std::mutex> l (mu);
    return status;
}

bool BotanicaProcessor::isAudioFile (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return f.existsAsFile() && (e == ".wav" || e == ".mp3" || e == ".ogg" || e == ".flac" || e == ".aif" || e == ".aiff");
}

juce::File BotanicaProcessor::soundsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Botanica").getChildFile ("Sounds");
}

juce::File BotanicaProcessor::presetsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Botanica").getChildFile ("Presets");
}

void BotanicaProcessor::queue (Job j)
{
    { std::lock_guard<std::mutex> l (jobMu); jobs.push_back (std::move (j)); }
    ++pendingJobs;
    notify();
}

juce::StringArray BotanicaProcessor::soundNames (bool padLayer) const
{
    juce::StringArray out;
    out.add (padLayer ? "Synth Pad" : "Piano");
    for (const auto& ii : instIndex()) out.add (juce::String (ii.name));
    std::lock_guard<std::mutex> l (mu);
    for (int i = 0; i < kNumCustom; ++i)
        out.add (customBank[i] != nullptr ? customPath[i].getFileNameWithoutExtension() : juce::String ("Empty slot ") + juce::String (i + 1));
    return out;
}

juce::String BotanicaProcessor::bedName (int bed) const
{
    std::lock_guard<std::mutex> l (mu);
    return bedCustom[bed] != nullptr ? bedPath[bed].getFileNameWithoutExtension() : juce::String();
}

juce::String BotanicaProcessor::chirpName() const
{
    std::lock_guard<std::mutex> l (mu);
    return chirpCustom.empty() ? juce::String() : chirpPath.getFileNameWithoutExtension();
}

void BotanicaProcessor::importDrop (DropTarget target, const juce::File& file)
{
    if (! isAudioFile (file)) { setStatus ("That is not an audio file (use wav, mp3, ogg, flac or aiff)."); return; }

    // keep a copy next to the plugin's data so the sound survives the original being moved or deleted
    juce::File dest = file;
    const auto dir = soundsDir();
    if (file.getParentDirectory() != dir && dir.createDirectory().wasOk())
    {
        auto copy = dir.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);
        if (file.copyFileTo (copy)) dest = copy;
    }

    if (target == DropTarget::piano || target == DropTarget::pad)
    {
        int slot = -1;
        {
            std::lock_guard<std::mutex> l (mu);
            for (int i = 0; i < kNumCustom && slot < 0; ++i) if (customPath[i] == juce::File() ) slot = i;
            if (slot < 0) { slot = nextCustom; nextCustom = (nextCustom + 1) % kNumCustom; }
            customPath[slot] = dest;
        }
        apvts.state.setProperty ("c" + juce::String (slot), dest.getFullPathName(), nullptr);
        if (auto* prm = apvts.getParameter (target == DropTarget::piano ? "pnSource" : "pdSource"))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (8 + slot)));
        queue ({ kInstrument, slot, dest });
    }
    else if (target == DropTarget::chirps)
    {
        { std::lock_guard<std::mutex> l (mu); chirpPath = dest; }
        apvts.state.setProperty ("ch", dest.getFullPathName(), nullptr);
        queue ({ kChirps, 0, dest });
    }
    else
    {
        const int bed = (int) target - (int) DropTarget::bed0;
        { std::lock_guard<std::mutex> l (mu); bedPath[bed] = dest; }
        apvts.state.setProperty ("b" + juce::String (bed), dest.getFullPathName(), nullptr);
        queue ({ kBed, bed, dest });
    }
    setStatus ("Importing " + dest.getFileName() + " ...");
}

void BotanicaProcessor::resetRecordings()
{
    {
        std::lock_guard<std::mutex> l (mu);
        for (int i = 0; i < bot::NatureBank::kBeds; ++i) { bedCustom[i] = nullptr; bedPath[i] = juce::File(); }
        chirpCustom.clear(); chirpPath = juce::File();
    }
    for (int i = 0; i < bot::NatureBank::kBeds; ++i) apvts.state.removeProperty ("b" + juce::String (i), nullptr);
    apvts.state.removeProperty ("ch", nullptr);
    rebuildNature();
    setStatus ("Back to the built-in nature recordings.");
}

void BotanicaProcessor::rebuildNature()
{
    auto base = sharedNature();
    auto nb = std::make_shared<bot::NatureBank>();
    {
        std::lock_guard<std::mutex> l (mu);
        for (int i = 0; i < bot::NatureBank::kBeds; ++i) nb->beds[(size_t) i] = bedCustom[i] ? bedCustom[i] : base->beds[(size_t) i];
        nb->chirps = chirpCustom.empty() ? base->chirps : chirpCustom;
    }
    std::shared_ptr<const bot::NatureBank> published = nb;
    auto old = std::atomic_exchange (&natureCur, published);
    retire (old);
}

void BotanicaProcessor::applyStateSlots()
{
    auto prop = [this] (const juce::String& k) { return juce::File (apvts.state.getProperty (k, "").toString()); };
    bool changed = false;
    for (int i = 0; i < kNumCustom; ++i)
    {
        const auto f = prop ("c" + juce::String (i));
        std::lock_guard<std::mutex> l (mu);
        if (f == juce::File() || ! f.existsAsFile()) { if (customBank[i] != nullptr || customPath[i] != juce::File()) { customBank[i] = nullptr; customPath[i] = juce::File(); changed = true; } }
        else if (f != customPath[i] || customBank[i] == nullptr) { customPath[i] = f; customBank[i] = nullptr; queue ({ kInstrument, i, f }); }
    }
    for (int i = 0; i < bot::NatureBank::kBeds; ++i)
    {
        const auto f = prop ("b" + juce::String (i));
        std::lock_guard<std::mutex> l (mu);
        if (f == juce::File() || ! f.existsAsFile()) { if (bedCustom[i] != nullptr) { bedCustom[i] = nullptr; bedPath[i] = juce::File(); changed = true; } }
        else if (f != bedPath[i] || bedCustom[i] == nullptr) { bedPath[i] = f; queue ({ kBed, i, f }); }
    }
    {
        const auto f = prop ("ch");
        std::lock_guard<std::mutex> l (mu);
        if (f == juce::File() || ! f.existsAsFile()) { if (! chirpCustom.empty()) { chirpCustom.clear(); chirpPath = juce::File(); changed = true; } }
        else if (f != chirpPath || chirpCustom.empty()) { chirpPath = f; queue ({ kChirps, 0, f }); }
    }
    if (changed) { ++customGen; rebuildNature(); }
}

std::shared_ptr<const bot::PianoBank> BotanicaProcessor::bankFor (int index)
{
    if (index <= 0) return sharedPiano();
    if (index <= 7) return builtinBank (index - 1);
    std::lock_guard<std::mutex> l (mu);
    return customBank[index - 8];
}

void BotanicaProcessor::refreshSources()
{
    const int pn = (int) std::lround (apvts.getRawParameterValue ("pnSource")->load());
    const int pd = (int) std::lround (apvts.getRawParameterValue ("pdSource")->load());
    const int gen = customGen.load();
    if (pn != loadedPn.load() || (gen != loadedGen && pn >= 8))
    {
        if (auto b = bankFor (pn)) { retire (std::atomic_exchange (&pianoCur, b)); loadedPn = pn; sendChangeMessage(); }
    }
    if (pd != loadedPd.load() || (gen != loadedGen && pd >= 8))
    {
        if (pd == 0) { retire (std::atomic_exchange (&padCur, std::shared_ptr<const bot::PianoBank>())); loadedPd = pd; }
        else if (auto b = bankFor (pd)) { retire (std::atomic_exchange (&padCur, b)); loadedPd = pd; sendChangeMessage(); }
    }
    loadedGen = gen;
}

void BotanicaProcessor::run()
{
    while (! threadShouldExit())
    {
        Job j; bool have = false;
        { std::lock_guard<std::mutex> l (jobMu); if (! jobs.empty()) { j = jobs.front(); jobs.erase (jobs.begin()); have = true; } }
        if (have)
        {
            doJob (j);
            --pendingJobs;
            continue;
        }
        refreshSources();
        wait (120);
    }
}

void BotanicaProcessor::doJob (const Job& j)
{
    bot::Decoded d;
    if (! bot::decodeFile (j.file, d))
    {
        setStatus ("Could not read " + j.file.getFileName() + " (too long, too short or an unsupported format).");
        return;
    }
    if (j.kind == kInstrument)
    {
        juce::String info;
        auto bank = bot::importInstrument (d, info);
        if (bank->samples.empty()) { setStatus (j.file.getFileName() + ": " + info); return; }
        {
            std::lock_guard<std::mutex> l (mu);
            customBank[j.index] = bank;
        }
        ++customGen;
        setStatus (j.file.getFileName() + ": " + info + " - loaded into Custom " + juce::String (j.index + 1));
    }
    else if (j.kind == kBed)
    {
        auto bed = std::make_shared<bot::LoopSample>();
        if (! bot::importBed (d, *bed)) { setStatus (j.file.getFileName() + " is too short to make a loop."); return; }
        { std::lock_guard<std::mutex> l (mu); bedCustom[j.index] = bed; }
        rebuildNature();
        static const char* names[] = { "Stream", "Forest", "Spring", "Dawn" };
        setStatus (j.file.getFileName() + " now plays in the " + names[j.index] + " slot.");
    }
    else
    {
        std::vector<std::shared_ptr<bot::LoopSample>> ch;
        if (! bot::importChirps (d, ch) || ch.empty()) { setStatus ("No short calls found in " + j.file.getFileName() + "."); return; }
        { std::lock_guard<std::mutex> l (mu); chirpCustom = ch; }
        rebuildNature();
        setStatus (juce::String ((int) ch.size()) + " chirps cut from " + j.file.getFileName() + ".");
    }
}

bool BotanicaProcessor::waitUntilSettled (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        const int pn = (int) std::lround (apvts.getRawParameterValue ("pnSource")->load());
        const int pd = (int) std::lround (apvts.getRawParameterValue ("pdSource")->load());
        if (pendingJobs.load() == 0 && loadedPn.load() == pn && loadedPd.load() == pd) return true;
        juce::Thread::sleep (20);
    }
    return false;
}

//==============================================================================
//  presets
//==============================================================================
const std::vector<BotanicaProcessor::Factory>& BotanicaProcessor::factoryPresets()
{
    using V = std::vector<std::pair<juce::String, float>>;
    static const std::vector<Factory> list = {
        { "Init", {} },
        { "Sky Gazing", V {
            { "root", 2 }, { "scale", 0 },
            { "arMode", 1 }, { "arPattern", 2 }, { "arRate", 1 }, { "arOct", 2 }, { "arRest", 0.2f },
            { "pnSource", 0 }, { "pnLevel", 0.7f }, { "pnRel", 1.2f },
            { "pdSource", 6 }, { "pdLevel", 0.55f }, { "pdAttack", 1.6f }, { "pdRel", 4.0f }, { "pdBright", 0.45f },
            { "bdLevel", 0.6f }, { "bdStream", 0.15f }, { "bdForest", 0.0f }, { "bdSpring", 0.6f }, { "bdDawn", 0.45f },
            { "chLevel", 0.45f }, { "chRate", 0.25f }, { "bbLevel", 0.18f }, { "bbDensity", 1.8f }, { "grLevel", 0.18f },
            { "rvMix", 0.5f }, { "rvSize", 0.9f }, { "ambIn", 3.0f }, { "ambOut", 6.0f } } },
        { "Quiet Stream", V {
            { "root", 9 }, { "scale", 1 },
            { "arMode", 2 }, { "arSeed", 5 }, { "arRate", 1 }, { "arOct", 2 }, { "arRest", 0.3f },
            { "pnSource", 0 }, { "pnTone", 0.4f }, { "pnRel", 1.5f },
            { "pdSource", 4 }, { "pdLevel", 0.4f }, { "pdAttack", 1.0f },
            { "bdLevel", 0.7f }, { "bdStream", 0.9f }, { "bdForest", 0.25f }, { "bdSpring", 0.0f }, { "bdDawn", 0.0f },
            { "chLevel", 0.2f }, { "chRate", 0.15f }, { "bbLevel", 0.35f }, { "bbDensity", 3.5f }, { "grLevel", 0.0f }, { "rvMix", 0.45f } } },
        { "Morning Forest", V {
            { "root", 7 }, { "scale", 2 },
            { "arMode", 1 }, { "arPattern", 0 }, { "arRate", 2 }, { "arOct", 1 }, { "arRest", 0.3f },
            { "pnLevel", 0.6f }, { "pdSource", 7 }, { "pdLevel", 0.5f }, { "pdAttack", 2.5f },
            { "bdLevel", 0.7f }, { "bdForest", 0.8f }, { "bdSpring", 0.3f }, { "bdDawn", 0.55f }, { "bdStream", 0.0f },
            { "chLevel", 0.8f }, { "chRate", 0.9f }, { "bbLevel", 0.12f }, { "grLevel", 0.15f } } },
        { "Warm Organ Pads", V {
            { "root", 0 }, { "scale", 0 }, { "arMode", 0 },
            { "pnLevel", 0.3f }, { "pdSource", 4 }, { "pdLevel", 0.8f }, { "pdAttack", 0.6f }, { "pdRel", 2.5f },
            { "bdLevel", 0.25f }, { "bdSpring", 0.3f }, { "chLevel", 0.1f }, { "bbLevel", 0.1f }, { "grLevel", 0.0f }, { "rvMix", 0.4f } } },
        { "Saw Dreams", V {
            { "root", 4 }, { "scale", 1 },
            { "pnSource", 1 }, { "arMode", 2 }, { "arSeed", 31 }, { "arRate", 2 }, { "arOct", 3 }, { "arRest", 0.2f }, { "pnRel", 0.8f }, { "pnLevel", 0.55f },
            { "pdSource", 3 }, { "pdLevel", 0.35f }, { "pdAttack", 1.2f }, { "bbLevel", 0.3f }, { "grLevel", 0.3f },
            { "bdLevel", 0.3f }, { "chLevel", 0.2f }, { "rvMix", 0.55f }, { "rvSize", 0.9f } } },
        { "Lead Sparkle", V {
            { "root", 11 }, { "scale", 3 },
            { "pnSource", 2 }, { "arMode", 2 }, { "arSeed", 62 }, { "arRate", 2 }, { "arOct", 2 }, { "arRest", 0.2f }, { "pnRel", 0.6f },
            { "pdSource", 7 }, { "pdLevel", 0.4f }, { "bbLevel", 0.35f }, { "bbDensity", 4.0f }, { "grLevel", 0.35f },
            { "chLevel", 0.3f }, { "bdSpring", 0.35f }, { "rvMix", 0.5f } } },
        { "Cloud Garden", V {
            { "root", 5 }, { "scale", 5 },
            { "arMode", 2 }, { "arSeed", 17 }, { "arRate", 1 }, { "arOct", 2 }, { "arRest", 0.25f }, { "pnRel", 1.4f }, { "pnLevel", 0.7f },
            { "pdSource", 6 }, { "pdLevel", 0.5f }, { "pdAttack", 2.0f }, { "pdRel", 5.0f },
            { "bdLevel", 0.5f }, { "bdSpring", 0.5f }, { "bdDawn", 0.3f }, { "chLevel", 0.3f }, { "bbLevel", 0.15f }, { "grLevel", 0.0f },
            { "gaOn", 1 }, { "gaMix", 0.55f }, { "gaSize", 380.0f }, { "gaDensity", 18.0f }, { "gaScatter", 0.75f }, { "gaPitch", 0.2f },
            { "rvMix", 0.55f }, { "rvSize", 0.92f }, { "ambIn", 3.0f }, { "ambOut", 6.0f } } },
        { "Pure Piano", V {
            { "arMode", 0 }, { "pdLevel", 0.25f }, { "bdLevel", 0.0f }, { "chLevel", 0.0f }, { "bbLevel", 0.0f }, { "grLevel", 0.0f }, { "rvMix", 0.3f } } },
    };
    return list;
}

int BotanicaProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
const juce::String BotanicaProcessor::getProgramName (int i) { return i >= 0 && i < getNumPrograms() ? factoryPresets()[(size_t) i].name : juce::String(); }
void BotanicaProcessor::setCurrentProgram (int i) { if (i != currentProgram && i >= 0 && i < getNumPrograms()) loadFactoryPreset (i); }

void BotanicaProcessor::loadFactoryPreset (int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    const auto& f = factoryPresets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            r->setValueNotifyingHost (r->getDefaultValue());
    for (const auto& kv : f.values)
        if (auto* r = apvts.getParameter (kv.first))
            r->setValueNotifyingHost (r->convertTo0to1 (kv.second));
    for (int i = 0; i < kNumCustom; ++i) apvts.state.removeProperty ("c" + juce::String (i), nullptr);
    for (int i = 0; i < bot::NatureBank::kBeds; ++i) apvts.state.removeProperty ("b" + juce::String (i), nullptr);
    apvts.state.removeProperty ("ch", nullptr);
    apvts.state.setProperty ("preset", f.name, nullptr);
    currentProgram = index;
    applyStateSlots();
    sendChangeMessage();
}

juce::StringArray BotanicaProcessor::userPresetNames() const
{
    juce::StringArray out;
    for (auto& f : presetsDir().findChildFiles (juce::File::findFiles, false, "*.botpreset"))
        out.add (f.getFileNameWithoutExtension());
    out.sort (true);
    return out;
}

bool BotanicaProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty() || ! presetsDir().createDirectory().wasOk()) return false;
    apvts.state.setProperty ("preset", clean, nullptr);
    if (auto xml = apvts.copyState().createXml())
    {
        const bool ok = xml->writeTo (presetsDir().getChildFile (clean + ".botpreset"));
        sendChangeMessage();
        return ok;
    }
    return false;
}

bool BotanicaProcessor::loadUserPreset (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (presetsDir().getChildFile (name + ".botpreset"));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    apvts.state.setProperty ("preset", name, nullptr);
    applyStateSlots();
    sendChangeMessage();
    return true;
}

bool BotanicaProcessor::deleteUserPreset (const juce::String& name)
{
    const bool ok = presetsDir().getChildFile (name + ".botpreset").deleteFile();
    sendChangeMessage();
    return ok;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BotanicaProcessor();
}
