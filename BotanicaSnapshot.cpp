// Plugin-level checks for Botanica (no DAW needed) + UI screenshots.
// Usage: BotanicaSnapshot <output-dir>
#include "BotanicaProcessor.h"
#include "BotanicaEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { const bool ok_ = (cond); std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL"); \
                              std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

static void setParam (BotanicaProcessor& p, const juce::String& id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static float getParam (BotanicaProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

static float rms (const juce::AudioBuffer<float>& b)
{
    return juce::jmax (b.getRMSLevel (0, 0, b.getNumSamples()), b.getRMSLevel (1, 0, b.getNumSamples()));
}

static void savePng (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? argv[1] : ".");
    dir.createDirectory();

    std::printf ("== Botanica plugin object ==\n");
    BotanicaProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 480);
    proc.prepareToPlay (48000.0, 480);

    CHECK (proc.acceptsMidi() && ! proc.producesMidi(), "takes MIDI in, no MIDI out");
    CHECK (proc.getTotalNumInputChannels() == 0 && proc.getTotalNumOutputChannels() == 2, "instrument: no audio in, stereo out");
    std::printf ("      %d automatable parameters\n", (int) proc.getParameters().size());
    CHECK (proc.getParameters().size() > 60, "all controls are exposed to the host");

    // sample-accurate MIDI: a note-on at sample 200 must be silent before sample 200, and sound soon after
    {
        for (auto* id : { "bbLevel", "nWater", "nWind", "nWindTone", "nBirds", "pdLevel", "grLevel", "rvMix", "bdLevel", "chLevel" })
            setParam (proc, id, 0.0f);
        setParam (proc, "arMode", 0.0f);                                   // Direct: the key itself plays
        juce::AudioBuffer<float> buf (2, 480);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 200);
        int first = -1;
        for (int b = 0; b < 12 && first < 0; ++b)
        {
            proc.processBlock (buf, midi); midi.clear();
            for (int i = 0; i < 480; ++i)
                if (std::abs (buf.getSample (0, i)) > 1.0e-4f) { first = b * 480 + i; break; }
        }
        std::printf ("      note-on at sample 200: first audible sample %d (the piano recording starts with ~8 ms of lead-in)\n", first);
        CHECK (first >= 200 && first < 200 + 48000 / 20, "MIDI note-on is placed sample-accurately and sounds within 50 ms");

        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        proc.processBlock (buf, midi);
    }

    // every layer makes sound through the real plugin path
    {
        const char* layers[][2] = { { "pnLevel", "piano" }, { "pdLevel", "pad" }, { "bbLevel", "bubbles" },
                                    { "grLevel", "granulizer" }, { "nWater", "water" }, { "nRain", "rain" },
                                    { "nWind", "wind" }, { "nBirds", "birds" }, { "nCrickets", "crickets" },
                                    { "bdStream", "recording: stream" }, { "bdForest", "recording: forest" },
                                    { "bdSpring", "recording: spring" }, { "bdDawn", "recording: dawn" }, { "chLevel", "recorded chirps" } };
        for (auto& l : layers)
        {
            BotanicaProcessor q;
            q.setPlayConfigDetails (0, 2, 48000.0, 480);
            q.prepareToPlay (48000.0, 480);
            for (auto* id : { "pnLevel", "pdLevel", "bbLevel", "grLevel", "nWater", "nRain", "nWind", "nWindTone", "nBirds", "nCrickets", "rvMix",
                              "bdStream", "bdForest", "bdSpring", "bdDawn", "chLevel" })
                setParam (q, id, 0.0f);
            setParam (q, l[0], 1.0f);
            if (juce::String (l[1]).startsWith ("recording")) setParam (q, "bdLevel", 1.0f);
            if (juce::String (l[1]) == "recorded chirps") setParam (q, "chRate", 2.0f);
            if (juce::String (l[1]) == "bubbles") setParam (q, "bbDensity", 12.0f);
            juce::AudioBuffer<float> buf (2, 480); juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
            float peak = 0; bool finite = true;
            for (int b = 0; b < 400; ++b)                                   // 4 seconds
            {
                proc.processBlock (buf, midi);                                  // (keeps the other instance warm; harmless)
                q.processBlock (buf, midi); midi.clear();
                for (int i = 0; i < 480; ++i) { const float s = buf.getSample (0, i); finite = finite && std::isfinite (s); peak = juce::jmax (peak, std::abs (s)); }
            }
            CHECK (finite && peak > 0.005f, "layer '%s' is audible through the plugin (peak %.3f)", l[1], peak);
        }
    }

    // transport: pattern follows the host tempo
    {
        class Head : public juce::AudioPlayHead
        {
        public:
            double ppq = 0, bpm = 120;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo i; i.setIsPlaying (true); i.setBpm (bpm); i.setPpqPosition (ppq); return i;
            }
        };
        auto grains = [] (double bpm)
        {
            BotanicaProcessor q;
            q.setPlayConfigDetails (0, 2, 48000.0, 480);
            q.prepareToPlay (48000.0, 480);
            Head head; head.bpm = bpm; q.setPlayHead (&head);
            for (auto* id : { "pnLevel", "pdLevel", "bbLevel", "nWater", "nWind", "nWindTone", "nBirds", "rvMix", "bdLevel", "chLevel" }) setParam (q, id, 0.0f);
            setParam (q, "grPattern", 1.0f); setParam (q, "grRate", 1.0f); setParam (q, "grBurst", 1.0f); setParam (q, "grPitchScatter", 0.0f);
            for (int i = 0; i < 16; ++i) setParam (q, BotanicaProcessor::stepOnId (i), 1.0f);
            juce::AudioBuffer<float> buf (2, 480); juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            for (int b = 0; b < 400; ++b)
            {
                q.processBlock (buf, midi); midi.clear();
                head.ppq += 480.0 * bpm / 60.0 / 48000.0;
            }
            return q.engine.grainSpawns;
        };
        const auto g120 = grains (120.0), g60 = grains (60.0);
        std::printf ("      grains in 4 s: %u at 120 BPM, %u at 60 BPM\n", g120, g60);
        CHECK (g120 > g60 * 1.6 && g60 > 0, "granulizer pattern follows the host tempo");
    }

    // state save / restore
    {
        BotanicaProcessor a, b;
        setParam (a, "root", 7); setParam (a, "scale", 3); setParam (a, "grow", 0.8f); setParam (a, "grPattern", 0.0f);
        setParam (a, BotanicaProcessor::stepOnId (4), 1.0f); setParam (a, BotanicaProcessor::stepDegId (4), -5.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto pa = a.readParams(), pb = b.readParams();
        CHECK (pb.root == 7 && pb.scale == 3 && std::abs (pb.grow - 0.8f) < 1e-3f && ! pb.grPattern && pb.stepOn[4] && pb.stepDeg[4] == -5,
               "preset save/restore round-trips (key %d, scale %d, grow %.2f, step 5: %d)", pb.root, pb.scale, pb.grow, pb.stepDeg[4]);
        CHECK (pa.root == pb.root, "restored key matches");
    }

    // sample rates and odd block sizes
    // ---- sounds, drag and drop, presets (through the real plugin object)
    {
        std::printf ("== Sounds, drop import and presets ==\n");
        auto make = [] (double sr = 48000.0)
        {
            auto q = std::make_unique<BotanicaProcessor>();
            q->setPlayConfigDetails (0, 2, sr, 480);
            q->prepareToPlay (sr, 480);
            return q;
        };
        struct Heard { float rms = 0, peak = 0; bool finite = true; };
        auto play = [] (BotanicaProcessor& q, double secs, std::initializer_list<int> notes, double measureFrom = 1.5)
        {
            juce::AudioBuffer<float> buf (2, 480); juce::MidiBuffer midi;
            for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            Heard h; double sum = 0; long cnt = 0;
            const int blocks = (int) (secs * 100.0);
            for (int b = 0; b < blocks; ++b)
            {
                q.processBlock (buf, midi); midi.clear();
                if (b >= (int) (measureFrom * 100.0))
                    for (int i = 0; i < 480; ++i)
                    {
                        const float l = buf.getSample (0, i), r = buf.getSample (1, i);
                        h.finite = h.finite && std::isfinite (l) && std::isfinite (r);
                        h.peak = juce::jmax (h.peak, std::abs (l), std::abs (r));
                        sum += (double) l * l + (double) r * r; cnt += 2;
                    }
            }
            h.rms = (float) std::sqrt (sum / (double) juce::jmax (1L, cnt));
            return h;
        };
        auto dbf = [] (float x) { return 20.0f * std::log10 (juce::jmax (x, 1.0e-9f)); };

        // every built-in sound plays through the piano layer and through the pad layer
        {
            bool allOk = true; float lowest = 0;
            for (int layer = 0; layer < 2 && allOk; ++layer)
                for (int src = 1; src <= 7; ++src)
                {
                    auto q = make();
                    for (auto* id : { "bbLevel", "nWindTone", "bdLevel", "chLevel", "grLevel", "rvMix" }) setParam (*q, id, 0.0f);
                    setParam (*q, "arMode", 0.0f);
                    setParam (*q, layer == 0 ? "pdLevel" : "pnLevel", 0.0f);
                    setParam (*q, layer == 0 ? "pnSource" : "pdSource", (float) src);
                    setParam (*q, layer == 0 ? "pnLevel" : "pdLevel", 1.0f);
                    q->waitUntilSettled (8000);
                    const auto h = play (*q, 3.0, { 55, 60, 64, 67 }, 0.2);       // (the Lead is a pluck: it fades by itself, so judge the first seconds)
                    const bool ok = h.finite && h.rms > 0.01f && h.peak < 1.0f;
                    if (! ok) { std::printf ("      layer %d sound %d: rms %.1f dB peak %.2f\n", layer, src, dbf (h.rms), h.peak); }
                    allOk = allOk && ok; lowest = lowest == 0 ? h.rms : juce::jmin (lowest, h.rms);
                }
            CHECK (allOk, "all 7 built-in sounds play on both the piano and the pad layer (quietest rms %.1f dBFS)", dbf (lowest));
        }

        // factory presets all load and make music
        {
            const int n = BotanicaProcessor::factoryPresets().size();
            bool ok = n >= 6; float quiet = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                auto q = make();
                q->loadFactoryPreset (i);
                q->waitUntilSettled (8000);
                const auto h = play (*q, 7.0, { 50, 57, 62 }, 3.0);
                const bool good = h.finite && h.rms > 0.004f && h.peak < 1.0f;
                std::printf ("      preset %-18s rms %6.1f dBFS peak %.2f\n", BotanicaProcessor::factoryPresets()[(size_t) i].name.toRawUTF8(), dbf (h.rms), h.peak);
                ok = ok && good; quiet = juce::jmin (quiet, h.rms);
            }
            CHECK (ok, "all %d factory presets load, are audible and do not clip", n);
        }

        // drag and drop: a pitched sound onto the pad layer, noise onto a nature slot, a chirp set onto Chirps
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("botanica_test_sounds");
        tmp.deleteRecursively(); tmp.createDirectory();
        auto writeWav = [&tmp] (const juce::String& name, double secs, std::function<float (double)> fn)
        {
            const auto f = tmp.getChildFile (name);
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
            if (w == nullptr) return f;
            out.release();
            const int n = (int) (secs * 44100.0);
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { const float v = fn ((double) i / 44100.0); b.setSample (0, i, v); b.setSample (1, i, v); }
            w->writeFromAudioSampleBuffer (b, 0, n);
            return f;
        };
        const auto organ = writeWav ("My Test Organ.wav", 6.0, [] (double t)
        {
            const double f = 220.0; const double e = std::min (1.0, t * 20.0) * (t < 5.5 ? 1.0 : 0.0);
            return (float) (0.3 * e * (std::sin (6.2831853 * f * t) + 0.5 * std::sin (6.2831853 * 2 * f * t) + 0.25 * std::sin (6.2831853 * 3 * f * t)));
        });
        juce::Random rnd (7);
        const auto hiss = writeWav ("My Test Rain.wav", 30.0, [&rnd] (double) { return 0.25f * (rnd.nextFloat() * 2.0f - 1.0f); });

        {
            auto q = make();
            for (auto* id : { "bbLevel", "nWindTone", "bdLevel", "chLevel", "grLevel", "rvMix", "pnLevel" }) setParam (*q, id, 0.0f);
            setParam (*q, "arMode", 0.0f); setParam (*q, "pdLevel", 1.0f);
            q->importDrop (BotanicaProcessor::DropTarget::pad, organ);
            CHECK (q->waitUntilSettled (10000), "dropped file is imported in the background (%s)", q->statusText().toRawUTF8());
            const int src = (int) std::lround (getParam (*q, "pdSource"));
            CHECK (src >= 8, "the pad layer switches to the dropped sound (source %d)", src);
            CHECK (q->soundNames (true).contains ("My Test Organ"), "the dropped sound appears under its file name");
            const auto h = play (*q, 5.0, { 57 });
            CHECK (h.finite && h.rms > 0.01f, "the dropped sound plays from the keyboard (rms %.1f dBFS)", dbf (h.rms));

            // a second plugin instance, with the first one's state, finds the sound again
            juce::MemoryBlock mb; q->getStateInformation (mb);
            auto r = make();
            for (auto* id : { "bbLevel", "nWindTone", "bdLevel", "chLevel", "grLevel", "rvMix", "pnLevel" }) setParam (*r, id, 0.0f);
            r->setStateInformation (mb.getData(), (int) mb.getSize());
            r->waitUntilSettled (10000);
            setParam (*r, "pdLevel", 1.0f);
            const auto h2 = play (*r, 5.0, { 57 });
            CHECK (h2.finite && h2.rms > 0.01f, "project state restores the dropped sound (rms %.1f dBFS)", dbf (h2.rms));
        }
        {
            auto q = make();
            for (auto* id : { "pnLevel", "pdLevel", "bbLevel", "nWindTone", "chLevel", "grLevel", "rvMix", "bdStream", "bdForest", "bdSpring", "bdDawn" }) setParam (*q, id, 0.0f);
            setParam (*q, "bdLevel", 1.0f); setParam (*q, "bdStream", 1.0f); setParam (*q, "arMode", 0.0f);
            const auto before = play (*q, 3.5, { 60 });
            q->importDrop (BotanicaProcessor::DropTarget::bed0, hiss);
            CHECK (q->waitUntilSettled (15000), "a dropped nature loop is imported (%s)", q->statusText().toRawUTF8());
            CHECK (q->bedName (0) == "My Test Rain", "the Stream slot is now called '%s'", q->bedName (0).toRawUTF8());
            const auto after = play (*q, 5.0, { 60 });
            std::printf ("      stream slot level: built-in %.1f dBFS, dropped noise %.1f dBFS\n", dbf (before.rms), dbf (after.rms));
            CHECK (after.finite && after.rms > 0.01f && std::abs (dbf (after.rms) - dbf (before.rms)) > 0.5f, "the dropped loop replaces the built-in one");
            q->resetRecordings();
            CHECK (q->bedName (0).isEmpty(), "reset brings the built-in recordings back");
        }

        // save and load your own preset
        {
            const juce::String name = "ZZ Botanica Test Preset";
            auto q = make();
            setParam (*q, "root", 9); setParam (*q, "scale", 1); setParam (*q, "grow", 0.77f); setParam (*q, "arPattern", 3);
            q->importDrop (BotanicaProcessor::DropTarget::pad, organ);
            q->waitUntilSettled (10000);
            CHECK (q->saveUserPreset (name), "preset file is written");
            CHECK (q->userPresetNames().contains (name), "the saved preset shows up in the list");

            auto r = make();
            setParam (*r, "root", 0); setParam (*r, "grow", 0.1f);
            CHECK (r->loadUserPreset (name), "preset loads in another instance");
            r->waitUntilSettled (10000);
            const auto pr = r->readParams();
            CHECK (pr.root == 9 && pr.scale == 1 && std::abs (pr.grow - 0.77f) < 1e-3f && pr.arPattern == 3, "preset restores key, scale, macro and piano pattern");
            bool named = false; for (auto& nm : r->soundNames (true)) named = named || nm.startsWith ("My Test Organ");
            CHECK (named && (int) std::lround (getParam (*r, "pdSource")) >= 8, "preset restores the dropped pad sound");
            CHECK (r->deleteUserPreset (name) && ! r->userPresetNames().contains (name), "preset can be deleted");
        }
        tmp.deleteRecursively();
        // tidy the copies the import made in the user's sounds folder
        for (auto& f : BotanicaProcessor::soundsDir().findChildFiles (juce::File::findFiles, false, "My Test*")) f.deleteFile();
    }

    // ---- seeded melody and smear, through the real plugin path
    {
        std::printf ("== Seeded melody + Smear (plugin level) ==\n");
        auto make = [] (double sr = 48000.0)
        {
            auto q = std::make_unique<BotanicaProcessor>();
            q->setPlayConfigDetails (0, 2, sr, 480);
            q->prepareToPlay (sr, 480);
            return q;
        };
        auto drive = [] (BotanicaProcessor& q, double secs, std::initializer_list<int> notes, bool release = false)
        {
            juce::AudioBuffer<float> buf (2, 480); juce::MidiBuffer midi;
            for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            const int blocks = (int) (secs * 100.0);
            double sum = 0; long cnt = 0; bool finite = true;
            for (int b = 0; b < blocks; ++b)
            {
                if (release && b == blocks / 4) for (int n : notes) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                q.processBlock (buf, midi); midi.clear();
                if (! release || b >= blocks / 2)
                    for (int i = 0; i < 480; ++i)
                    {
                        const float l = buf.getSample (0, i);
                        finite = finite && std::isfinite (l); sum += (double) l * l; ++cnt;
                    }
            }
            struct R { double rms; bool finite; };
            return R { std::sqrt (sum / (double) juce::jmax (1L, cnt)), finite };
        };

        {
            auto a = make(), b = make();
            for (auto* q : { a.get(), b.get() })
            {
                setParam (*q, "arMode", 2.0f); setParam (*q, "arRest", 0.0f); setParam (*q, "arRate", 2.0f);
                for (auto* id : { "bbLevel", "nWindTone", "bdLevel", "chLevel", "grLevel" }) setParam (*q, id, 0.0f);
            }
            setParam (*a, "arSeed", 12.0f); setParam (*b, "arSeed", 13.0f);
            const auto ra = drive (*a, 6.0, { 60, 64, 67 }), rb = drive (*b, 6.0, { 60, 64, 67 });
            std::vector<int> na, nb;
            for (uint32_t i = 0; i < juce::jmin (a->engine.arpCount, 64u); ++i) na.push_back (a->engine.arpLog[(a->engine.arpCount - 1 - i) & 63u]);
            for (uint32_t i = 0; i < juce::jmin (b->engine.arpCount, 64u); ++i) nb.push_back (b->engine.arpLog[(b->engine.arpCount - 1 - i) & 63u]);
            CHECK (ra.finite && ra.rms > 0.01 && a->engine.arpCount > 20, "seeded melody plays through the plugin (%u notes in 6 s)", a->engine.arpCount);
            CHECK (na != nb, "changing the seed parameter changes the melody");
            bool inKey = true; for (int n : na) inKey = inKey && ((n % 12) == 0 || (n % 12) == 2 || (n % 12) == 4 || (n % 12) == 5 || (n % 12) == 7 || (n % 12) == 9 || (n % 12) == 11);
            CHECK (inKey, "all melody notes are in C major (key C, major scale)");
            (void) rb;
        }
        {
            auto off = make(), on = make();
            for (auto* q : { off.get(), on.get() })
            {
                setParam (*q, "arMode", 0.0f); setParam (*q, "ambGate", 0.0f);
                for (auto* id : { "bbLevel", "nWindTone", "bdLevel", "chLevel", "grLevel", "pdLevel", "rvMix" }) setParam (*q, id, 0.0f);
                setParam (*q, "pnRel", 0.06f); setParam (*q, "gaMix", 1.0f); setParam (*q, "gaSize", 300.0f); setParam (*q, "gaScatter", 0.8f); setParam (*q, "gaDensity", 30.0f);
            }
            setParam (*on, "gaOn", 1.0f);
            const auto a = drive (*off, 5.0, { 60 }, true), b = drive (*on, 5.0, { 60 }, true);
            std::printf ("      after the note ended: smear off %.1f dBFS, on %.1f dBFS\n", 20.0 * std::log10 (std::max (a.rms, 1e-9)), 20.0 * std::log10 (std::max (b.rms, 1e-9)));
            CHECK (b.finite && b.rms > a.rms * 30.0 && b.rms > 0.002, "Smear Everything keeps a plucked note alive as a grain cloud");
        }
        CHECK (BotanicaProcessor::factoryPresets().size() >= 9, "factory preset list includes the new Cloud Garden preset");
        {
            auto q = make();
            int idx = -1; for (int i = 0; i < BotanicaProcessor::factoryPresets().size(); ++i) if (BotanicaProcessor::factoryPresets()[(size_t) i].name == "Cloud Garden") idx = i;
            q->loadFactoryPreset (idx);
            const auto pr = q->readParams();
            CHECK (idx >= 0 && pr.gaOn && pr.arMode == 2 && pr.arSeed == 17 && std::abs (pr.gaSizeMs - 380.0f) < 1.0f,
                   "Cloud Garden preset switches on Smear and Seeded Melody (seed %d, grain %.0f ms)", pr.arSeed, pr.gaSizeMs);
        }
    }

    // silent until played, through the real plugin path
    {
        BotanicaProcessor q;
        q.setPlayConfigDetails (0, 2, 48000.0, 480);
        q.prepareToPlay (48000.0, 480);
        juce::AudioBuffer<float> buf (2, 480); juce::MidiBuffer midi;
        float idle = 0;
        for (int b = 0; b < 300; ++b) { q.processBlock (buf, midi); idle = juce::jmax (idle, rms (buf)); }
        CHECK (idle < 1.0e-6f, "default patch is silent until a key is pressed (%.1e)", idle);
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
        float heard = 0;
        for (int b = 0; b < 600; ++b) { q.processBlock (buf, midi); midi.clear(); if (b > 400) heard = juce::jmax (heard, rms (buf)); }
        CHECK (heard > 0.02f, "default patch fills in while keys are held (rms %.3f)", heard);
    }

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int bs : { 1, 37, 480, 4096 })
        {
            BotanicaProcessor q;
            q.setPlayConfigDetails (0, 2, sr, bs);
            q.prepareToPlay (sr, bs);
            juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 90), bs / 2);
            bool finite = true;
            for (int b = 0; b < 30; ++b)
            {
                q.processBlock (buf, midi); midi.clear();
                for (int i = 0; i < bs; ++i) finite = finite && std::isfinite (buf.getSample (0, i)) && std::isfinite (buf.getSample (1, i)) && std::abs (buf.getSample (0, i)) <= 1.0001f;
            }
            if (! finite) { CHECK (false, "sample rate %.0f, block %d produced bad audio", sr, bs); }
        }
    CHECK (true, "sample rates 44.1k-192k with block sizes 1..4096 stay finite and bounded");

    std::printf ("== Editor screenshots ==\n");
    {
        BotanicaProcessor p2;
        p2.setPlayConfigDetails (0, 2, 48000.0, 480);
        p2.prepareToPlay (48000.0, 480);
        setParam (p2, "root", 2); setParam (p2, "scale", 1);               // D minor
        setParam (p2, "bbDensity", 6.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p2.createEditor());
        auto* be = dynamic_cast<BotanicaEditor*> (ed.get());
        ed->setVisible (true);

        juce::AudioBuffer<float> buf (2, 480); juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);
        for (int b = 0; b < 260; ++b)                                     // ~2.6 s of playing, the pond reads the events
        {
            p2.processBlock (buf, midi); midi.clear();
            if (b % 3 == 0 && be != nullptr) be->debugTick();
        }
        savePng (*ed, dir.getChildFile ("botanica_ui.png"));
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PLUGIN CHECKS PASSED" : "PLUGIN CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
