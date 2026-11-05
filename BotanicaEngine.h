#pragma once
// =============================================================================
//  BotanicaEngine.h  -  all DSP for the Botanica instrument (no GUI / host code)
//
//  Layers:   piano (sampled)  |  pads (synth)  |  bubbles (synth)
//            nature: water, rain, wind (+ tuned wind), birds, crickets (synth)
//            granulizer (grains of the piano, key-locked, step pattern)
//            shared reverb, "Grow" macro, soft limiter
//
//  Everything that picks a pitch (bubbles, birds, tuned wind, grains, optional
//  incoming-note snapping) is locked to the chosen Key + Scale.
// =============================================================================
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace bot
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float midiToHz (float m) { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }

//------------------------------------------------------------------------------
// tiny realtime-safe random generator
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    inline uint32_t u()  { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float f()     { return (float) (u() >> 8) * (1.0f / 16777216.0f); }     // 0..1
    inline float bi()    { return f() * 2.0f - 1.0f; }                              // -1..1
    inline int range (int lo, int hi) { return lo + (int) (f() * (float) (hi - lo + 1)); }
};

//------------------------------------------------------------------------------
// scales / key lock
struct Scale { const char* name; int n; int deg[12]; };

inline const Scale* scales()
{
    static const Scale s[] = {
        { "Major",            7,  { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",            7,  { 0, 2, 3, 5, 7, 8, 10 } },
        { "Major Pentatonic", 5,  { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic", 5,  { 0, 3, 5, 7, 10 } },
        { "Dorian",           7,  { 0, 2, 3, 5, 7, 9, 10 } },
        { "Lydian",           7,  { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",       7,  { 0, 2, 4, 5, 7, 9, 10 } },
        { "Harmonic Minor",   7,  { 0, 2, 3, 5, 7, 8, 11 } },
        { "Whole Tone",       6,  { 0, 2, 4, 6, 8, 10 } },
        { "Chromatic",        12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    };
    return s;
}
constexpr int kNumScales = 10;
inline const char* noteName (int pc)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}

inline bool inScale (int midi, int root, int sc)
{
    const int pc = (((midi - root) % 12) + 12) % 12;
    const auto& s = scales()[sc];
    for (int i = 0; i < s.n; ++i) if (s.deg[i] == pc) return true;
    return false;
}

// nearest scale note (ties go down)
inline int quantizeToScale (int midi, int root, int sc)
{
    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (midi - d, root, sc)) return midi - d;
        if (inScale (midi + d, root, sc)) return midi + d;
    }
    return midi;
}

// move a number of scale degrees up (+) or down (-) from a note
inline int scaleStep (int midi, int root, int sc, int steps)
{
    midi = quantizeToScale (midi, root, sc);
    const int dir = steps >= 0 ? 1 : -1;
    for (int k = 0; k < std::abs (steps); ++k)
    {
        do { midi += dir; } while (! inScale (midi, root, sc) && midi > 0 && midi < 127);
    }
    return midi;
}

inline int randomScaleNote (Rng& r, int root, int sc, int lo, int hi)
{
    int pool[128]; int n = 0;
    for (int m = lo; m <= hi && n < 128; ++m) if (inScale (m, root, sc)) pool[n++] = m;
    return n > 0 ? pool[r.range (0, n - 1)] : lo;
}

//------------------------------------------------------------------------------
// Seeded melodies: curated 16-step phrases written in chord-tone numbers.
//   0 1 2 3 ...  = chord tones from the bass note upwards (3 per octave for a triad, 4 for a 7th chord ...)
//   r = rest,  1p / 1m = one scale step above / below that chord tone (a passing or neighbour note)
// Strong beats (steps 0, 4, 8, 12) always land on chord tones, passing notes only sit on weak steps.
// A seed (1-99) picks one of 24 phrases and one of 4 variations of it (inversion, answer-first, shifted beat),
// plus a few seeded neighbour-chord-tone changes on weak steps, so every seed is a different but musical line.
struct MelTok { bool rest = false; int idx = 0; int pass = 0; };

inline const std::array<std::array<MelTok, 16>, 24>& melodyTemplates()
{
    static const std::array<std::array<MelTok, 16>, 24> t = []
    {
        static const char* src[24] = {
            "0 1 2 r 1 2 3 r 2 3 4 r 3 2 1 0",      //  1 dreamy climb, settles back down
            "0 r 1 r 2 r 3 r 4 r 3 r 2 r 1 r",      //  2 sky ascent (sparse)
            "2 r 1 r 0 r 1 2 3 r 2 r 1 0 r r",      //  3 lullaby arch
            "0 1 2 3 4 3 2 1 0 1 2 3 4 3 2 1",      //  4 classic up and down
            "0 2 1 2 0 3 2 3 1 4 3 4 2 3 1 0",      //  5 broken chord with skips
            "0 r 2 r 1 r 3 r 2 r 4 r 3 2 1 0",      //  6 pulse and lift
            "2 r 3 3m 2 r 1 1p 0 r 1 1m 2 r 1 0",   //  7 singing neighbour notes
            "4 r r 3 r r 2 r 4 r r 3 r 2 1 r",      //  8 bells
            "0 r r 1 r 2 r r 3 r r 2 r 1 r 0",      //  9 syncopated
            "4 3 2 1 0 1 2 1 3 2 1 0 2 1 0 r",      // 10 cascade
            "0 2 0 3 0 2 0 4 0 2 0 3 0 2 1 0",      // 11 pedal bounce
            "0 1 1p 2 1 2 2p 3 2 3 3p 4 3 2 1 0",   // 12 stair-steps with passing notes
            "0 r 2 r 4 r 2 r 0 r 2 r 3 r 1 r",      // 13 wide arch
            "2 2 r 2 1 r 1 1 0 0 r 0 1 2 r r",      // 14 tick-tock
            "0 1 2 r 1 2 3 r 2 3 4 r 3 2 1 r",      // 15 triplet feel
            "0 2 1 2 0 2 1 2 1 3 2 3 1 3 2 3",      // 16 alberti
            "0 1 1p 1 2 3 3p 3 4 3 2 1 0 r r r",    // 17 ornamented rise
            "0 r 1 r 2 3 r 2 1 r 2 r 3 2 1 0",      // 18 question and answer
            "4 2 4 2 3 1 3 1 4 2 4 2 3 1 0 r",      // 19 shimmer
            "0 3 2 1 0 3 2 1 1 4 3 2 1 4 3 2",      // 20 falling loops
            "0 r 0 1 r 2 r r 1 r 1 2 r 3 r r",      // 21 rhythmic motif
            "2 1 0 1 2 3 4 3 2 1 0 1 2 1 0 r",      // 22 gentle wave
            "0 1 2 3 2 1 0 r 1 2 3 4 3 2 1 r",      // 23 two phrases
            "4 r 3 r 2 r 1 r 0 r 1 r 2 r 3 r",      // 24 slow fall and rise
        };
        std::array<std::array<MelTok, 16>, 24> out {};
        for (size_t i = 0; i < 24; ++i)
        {
            const char* p = src[i]; size_t k = 0;
            while (*p && k < 16)
            {
                while (*p == ' ') ++p;
                if (! *p) break;
                MelTok m;
                if (*p == 'r') { m.rest = true; ++p; }
                else
                {
                    m.idx = *p - '0'; ++p;
                    if (*p == 'p') { m.pass = 1; ++p; } else if (*p == 'm') { m.pass = -1; ++p; }
                }
                out[i][k++] = m;
            }
        }
        return out;
    }();
    return t;
}

inline uint32_t melHash (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

// the note of the phrase before rests are thinned out
inline MelTok melodyRaw (int seed, int pos, int n, int poolLen)
{
    seed = std::max (1, std::min (99, seed));
    const int s0 = seed - 1, tpl = s0 % 24, var = (s0 / 24) % 4;
    const int p = ((pos % 16) + 16) % 16;
    const int q = var == 2 ? (p + 8) % 16 : var == 3 ? (p + 4) % 16 : p;
    MelTok t = melodyTemplates()[(size_t) tpl][(size_t) q];
    if (! t.rest)
    {
        if (var == 1) t.idx += 1;
        const uint32_t h = melHash ((uint32_t) seed * 131u + (uint32_t) p * 7919u);
        if (p % 4 != 0 && t.pass == 0 && (h % 100u) < 12u) t.idx += (h & 0x100u) ? 1 : -1;
        while (t.idx >= poolLen && t.idx >= n) t.idx -= n;
        while (t.idx < 0) t.idx += n;
    }
    return t;
}

struct Melody { bool rest = true; int idx = 0; int pass = 0; float vel = 0.6f; float len = 0.9f; };

inline Melody melodyToken (int seed, int pos, float restProb, int n, int poolLen)
{
    auto restAt = [&] (int ps)
    {
        const auto r = melodyRaw (seed, ps, n, poolLen);
        const int p = ((ps % 16) + 16) % 16;
        const uint32_t h = melHash ((uint32_t) seed * 977u + (uint32_t) p * 104729u);
        return r.rest || (p % 4 != 0 && (float) (h % 1000u) * 0.001f < restProb);
    };
    Melody m;
    const int p = ((pos % 16) + 16) % 16;
    const auto raw = melodyRaw (seed, pos, n, poolLen);
    m.rest = restAt (pos);
    m.idx = raw.idx; m.pass = raw.pass;
    const uint32_t h = melHash ((uint32_t) seed * 31u + (uint32_t) p * 65537u);
    const float jitter = ((float) (h % 1000u) * 0.001f - 0.5f) * 0.12f;
    m.vel = (p % 4 == 0 ? 0.80f : p % 2 == 0 ? 0.62f : 0.50f) + jitter;
    m.len = raw.pass != 0 ? 0.6f : restAt (pos + 1) ? 1.8f : 0.95f;
    return m;
}

// the chord that is being held: pitch classes above the lowest note (or a triad of the key if one note is held)
struct Chord
{
    int bass = 60, n = 3, off[8] = { 0, 4, 7, 0, 0, 0, 0, 0 };
    int note (int idx) const { return bass + off[idx % n] + 12 * (idx / n); }
};

//------------------------------------------------------------------------------
// DSP building blocks
struct Svf            // topology-preserving state variable filter
{
    float s1 = 0, s2 = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float fc, float q, float sr)
    {
        const float g = std::tan (kPi * std::min (fc, 0.45f * sr) / sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        bp = v1; lp = v2; hp = v0 - k * v1 - v2;
    }
    void reset() { s1 = s2 = 0; }
};

struct Pink
{
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    inline float tick (float w)
    {
        b0 = 0.99886f * b0 + w * 0.0555179f;  b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;  b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;  b5 = -0.7616f * b5 - w * 0.0168980f;
        const float o = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return o * 0.11f;
    }
};

struct SmoothRand      // sample & hold noise, smoothed: slow random modulation in -1..1
{
    float v = 0, t = 0, c = 0.001f; double ph = 0, inc = 0.0001;
    void setup (float rateHz, float smoothHz, float sr)
    {
        inc = rateHz / sr;
        c = 1.0f - std::exp (-kTwoPi * smoothHz / sr);
    }
    inline float tick (Rng& r)
    {
        ph += inc;
        if (ph >= 1.0) { ph -= 1.0; t = r.bi(); }
        v += c * (t - v);
        return v;
    }
};

struct Smooth
{
    float v = 0, t = 0, c = 0.002f;
    void setup (float sr, float seconds = 0.02f) { c = 1.0f - std::exp (-1.0f / (seconds * sr)); }
    void snap (float x) { v = t = x; }
    inline float tick() { v += c * (t - v); return v; }
};

inline float hermite (float y0, float y1, float y2, float y3, float t)
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

inline float readHermite (const float* d, int len, double pos)
{
    const int i = (int) pos;
    const float t = (float) (pos - (double) i);
    auto g = [&] (int k) { return d[std::max (0, std::min (len - 1, k))]; };
    return hermite (g (i - 1), g (i), g (i + 1), g (i + 2), t);
}

inline float polyBlep (float t, float dt)
{
    if (t < dt)        { t /= dt;           return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

//------------------------------------------------------------------------------
// piano samples
struct PianoSample
{
    int root = 60;
    double fine = 0.0;                   // measured pitch minus root, in semitones (imported recordings may be slightly off)
    double sr = 44100.0;
    juce::AudioBuffer<float> data;       // 2 channels
    int loopStart = 0, loopEnd = 0, xfade = 0;       // sustain loop (samples); loopEnd == 0: plays once, no loop
    bool looped() const { return loopEnd > loopStart && xfade > 0; }

    // keeps a loop valid: the cross-fade reads xfade samples *before* the loop start
    void sanitiseLoop()
    {
        const int len = data.getNumSamples();
        if (loopEnd > len - 4) loopEnd = len - 4;
        if (loopStart < 0) loopStart = 0;
        if (loopEnd - loopStart < 64 || xfade <= 0) { loopStart = loopEnd = xfade = 0; return; }
        xfade = std::min (xfade, std::min (loopStart, loopEnd - loopStart));
        if (xfade < 16) { loopStart = loopEnd = xfade = 0; }
    }
};

struct PianoBank
{
    std::vector<PianoSample> samples;
    float gain = 1.0f;                                // per-instrument loudness trim
    const PianoSample* nearest (int midi) const
    {
        const PianoSample* best = nullptr; int bd = 1 << 20;
        for (auto& s : samples)
        {
            const int d = std::abs (s.root - midi);
            if (d < bd) { bd = d; best = &s; }
        }
        return best;
    }
};

//------------------------------------------------------------------------------
// recorded nature: four seamless loops ("beds") and a set of short bird-chirp snippets
struct LoopSample
{
    double sr = 44100.0;
    juce::AudioBuffer<float> data;       // 2 channels (chirps: both channels identical)
};

struct NatureBank
{
    static constexpr int kBeds = 4;
    std::array<std::shared_ptr<LoopSample>, kBeds> beds;        // shared, so a dropped sound can replace one slot without copying the rest
    std::vector<std::shared_ptr<LoopSample>> chirps;
};

//------------------------------------------------------------------------------
// Events for the UI "garden" animation (audio thread writes, UI thread reads; lock-free)
struct UiEvent { uint8_t kind = 0; float pitch = 0.5f, size = 0.5f; };   // kind: 0 bubble, 1 note, 2 grain burst
struct EventRing
{
    std::array<UiEvent, 256> buf {};
    std::atomic<uint32_t> w { 0 };
    void push (uint8_t kind, float pitch01, float size)
    {
        const uint32_t i = w.load (std::memory_order_relaxed);
        buf[i & 255u] = { kind, std::max (0.0f, std::min (1.0f, pitch01)), size };
        w.store (i + 1, std::memory_order_release);
    }
    bool pop (uint32_t& r, UiEvent& e) const          // r is the reader's own position
    {
        const uint32_t ww = w.load (std::memory_order_acquire);
        if (ww - r > 256u) r = ww - 256u;
        if (r == ww) return false;
        e = buf[r & 255u]; ++r;
        return true;
    }
};

//------------------------------------------------------------------------------
struct Params
{
    int   root = 0, scale = 0;
    bool  snap = false;
    float grow = 0.5f, masterDb = 0.0f;

    float pnLevel = 0.8f, pnTone = 0.5f, pnRel = 0.4f;

    float pdLevel = 0.5f, pdAttack = 0.6f, pdRel = 2.5f, pdDetune = 12.0f, pdBright = 0.5f;
    int   pdOct = 0;                                   // -1, 0, +1

    float bbLevel = 0.3f, bbDensity = 2.5f, bbRegister = 0.5f, bbSize = 0.5f, bbBloop = 0.3f;
    bool  bbKey = true;

    float nWater = 0.0f, nRain = 0.0f, nWind = 0.0f, nWindTone = 0.2f, nBirds = 0.0f, nCrickets = 0.0f;
    bool  birdsKey = true;

    float grLevel = 0.25f, grSizeMs = 120.0f, grBurst = 3.0f, grPos = 0.1f, grPosScatter = 0.25f;
    float grPitchScatter = 2.0f, grOct = 0.1f, grRev = 0.15f, grSpread = 0.5f, grDensity = 12.0f;
    int   grRate = 1;                                  // 0:1/4  1:1/8  2:1/16  3:1/32
    bool  grPattern = true;
    std::array<bool, 16> stepOn { { true, false, true, true, false, true, false, false,
                                    true, false, true, true, false, true, false, false } };
    std::array<int, 16>  stepDeg { { 0, 0, 2, 0, 0, 4, 0, 0, 0, 0, -2, 3, 0, 2, 0, 0 } };

    float rvMix = 0.35f, rvSize = 0.8f, rvDamp = 0.5f;

    // scene: recordings and the "silent until you play" behaviour
    bool  ambGate = true;                              // ambience (recordings, bubbles, synth nature) only while keys are held
    float ambIn = 2.0f, ambOut = 4.0f;                 // seconds
    float bdLevel = 0.6f;
    std::array<float, 4> bdGain { { 0.5f, 0.35f, 0.6f, 0.5f } };   // stream, forest, spring, dawn
    bool  bdKeys = false;                              // keys pick the scene (low = stream ... high = dawn)
    float chLevel = 0.6f, chRate = 0.35f;              // chirps per second

    // piano performance
    int   arMode = 1;                                  // 0: direct, 1: chord arp, 2: seeded melody
    int   arRate = 1;                                  // 0:1/4 1:1/8 2:1/16 3:1/32
    int   arPattern = 2;                               // 0 up, 1 down, 2 up-down, 3 random
    int   arOct = 2;                                   // octaves covered (1..3)
    float arRest = 0.15f;                              // probability of a rest
    int   arSeed = 7;                                  // 1..99, picks the melody in "Seeded Melody" mode

    // smear: the granulizer on the whole plugin output
    bool  gaOn = false, gaFreeze = false;
    float gaMix = 0.6f, gaSizeMs = 220.0f, gaDensity = 24.0f, gaScatter = 0.5f, gaPitch = 0.0f;
};

//==============================================================================
class Engine
{
public:
    std::atomic<int> uiStep { -1 };                    // current pattern step (for the UI)
    EventRing events;                                  // bubbles / notes / grain bursts (for the UI)

    // counters of the pitches chosen by the bubble / grain generators (used by the tests)
    std::array<uint32_t, 128> bubbleHist {}, grainHist {};
    uint32_t grainSpawns = 0;
    std::array<int, 64> arpPosLog {};
    std::array<int, 64> arpLog {};                       // last piano arp notes (ring) and how many were played (tests)
    uint32_t arpCount = 0;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = (float) sampleRate;
        maxBlockSize = std::max (64, maxBlock);
        rvL.assign ((size_t) maxBlockSize, 0.0f);
        rvR.assign ((size_t) maxBlockSize, 0.0f);
        rng.s = 0x1234abcdu;

        for (auto& l : lv) l.setup (sr, 0.03f);
        for (auto& l : lv) l.snap (0.0f);
        masterSm.setup (sr, 0.03f);
        padSm.setup (sr, 0.03f); padSm.snap (0.0f);
        for (auto& b : bedLv) b.setup (sr, 0.4f);
        smAmt.setup (sr, 0.12f);
        smLen = (int) (6.0 * sampleRate) + 16;
        smL.assign ((size_t) smLen, 0.0f); smR.assign ((size_t) smLen, 0.0f);
        for (int i = 0; i < 4; ++i) bedPos[(size_t) i] = 0.0;

        for (int i = 0; i < 1024; ++i) hann[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / 1023.0f);

        nature.prepare (sr, rng);
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        reset();
    }

    void reset()
    {
        for (auto& v : pv) v = PianoVoice();
        for (auto& v : dv) v = PadVoice();
        for (auto& b : bubbles) b = Bubble();
        for (auto& g : grains) g = Grain();
        down.fill (false); sust.fill (false); pedal = false; nHeld = 0;
        pendingN = 0;
        gate = 0.0f; arpIdx = 0; arpDir = 1; arpLast = -1; lastArpStep = -1;
        for (auto& c : chirpV) c.on = false;
        for (auto& b : bedLv) b.snap (0.0f);
        std::fill (smL.begin(), smL.end(), 0.0f); std::fill (smR.begin(), smR.end(), 0.0f);
        for (auto& g : smG) g.on = false;
        smW = 0; smFrozen = false; smAmt.snap (0.0f); melodyPos = 0;
        nature.setHeld (held, 0, P.root, P.scale);
        reverb.reset();
    }

    void setPiano (std::shared_ptr<const PianoBank> b)
    {
        if (b.get() != piano.get()) { for (auto& v : pv) if (! v.pad) v.on = false; for (auto& g : grains) g.on = false; }
        piano = std::move (b);
    }
    void setNature (std::shared_ptr<const NatureBank> b)
    {
        if (b.get() != natureBank.get()) for (auto& c : chirpV) c.on = false;
        natureBank = std::move (b);
    }
    void setPadBank (std::shared_ptr<const PianoBank> b)                                       // null / empty = built-in synth pad
    {
        if (b.get() != padBank.get()) for (auto& v : pv) if (v.pad) v.on = false;
        padBank = std::move (b);
    }
    void setParams (const Params& p) { P = p; }

    void setTransport (bool playing, double bpmIn, bool hasPpq, double ppq)
    {
        hostPlaying = playing; bpm = bpmIn > 20.0 ? bpmIn : 120.0; havePpq = hasPpq; ppqStart = ppq;
    }

    //--------------------------------------------------------------------------
    // MIDI
    void noteOn (int note, float vel)
    {
        note = std::max (0, std::min (127, note));
        const int m = P.snap ? quantizeToScale (note, P.root, P.scale) : note;
        mapped[(size_t) note] = m;

        for (auto& v : pv) if (v.on && ! v.rel && v.note == m) v.rel = true;
        for (auto& v : dv) if (v.on && ! v.rel && v.note == m) v.rel = true;

        down[(size_t) m] = true; sust[(size_t) m] = false;
        events.push (1, ((float) m - 24.0f) / 84.0f, vel);
        if (P.arMode == 0) startPiano (m, vel);
        startPad (m, vel);
        rebuildHeld();
    }

    void noteOff (int note)
    {
        note = std::max (0, std::min (127, note));
        const int m = mapped[(size_t) note];
        if (! down[(size_t) m]) return;
        down[(size_t) m] = false;
        if (pedal) sust[(size_t) m] = true; else releaseNote (m);
        rebuildHeld();
    }

    void controller (int cc, int value)
    {
        if (cc == 64)
        {
            const bool nowDown = value >= 64;
            if (pedal && ! nowDown)
                for (int m = 0; m < 128; ++m)
                    if (sust[(size_t) m] && ! down[(size_t) m]) { sust[(size_t) m] = false; releaseNote (m); }
            pedal = nowDown;
            rebuildHeld();
        }
        else if (cc == 123 || cc == 120) allNotesOff();
    }

    void allNotesOff()
    {
        for (int m = 0; m < 128; ++m) { down[(size_t) m] = false; sust[(size_t) m] = false; releaseNote (m); }
        rebuildHeld();
    }

    //--------------------------------------------------------------------------
    void render (float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += maxBlockSize)
            renderChunk (L + off, R + off, std::min (maxBlockSize, n - off));
    }

private:
    //--------------------------------------------------------------------------
    struct PianoVoice
    {
        bool on = false, rel = false; int note = 0; const PianoSample* s = nullptr;
        double pos = 0, rate = 1; float gain = 0, env = 1, fade = 0, lpA = 1, lpL = 0, lpR = 0; uint32_t age = 0; int holdLeft = -1;
        bool pad = false, loopOn = false, xf = false; float atk = 1.0f;
    };

    struct PadVoice
    {
        bool on = false, rel = false; int note = 0; double ph[4] = { 0, 0, 0, 0 }, sub = 0;
        float freq = 440, env = 0, vel = 1, lfo = 0, lfoInc = 0.0001f, cutoff = 2000; uint32_t age = 0;
        Svf fl, fr;
    };

    struct Bubble
    {
        bool on = false; double ph = 0; float f0 = 0, chirp = 0, amp = 0, panL = 1, panR = 1;
        float e = 1, eDec = 0.99f, c = 1, cDec = 0.99f, a = 1, aDec = 0.9f; int life = 0;
    };

    struct ChirpVoice
    {
        bool on = false; const LoopSample* s = nullptr; double pos = 0, inc = 1; float gain = 0, panL = 1, panR = 1;
    };

    struct SmearGrain { bool on = false; double pos = 0, inc = 1; int age = 0, len = 1; float gain = 0, panL = 1, panR = 1; };

    struct Grain
    {
        bool on = false; int delay = 0, age = 0, len = 1; const PianoSample* s = nullptr;
        double pos = 0, inc = 1; float gain = 0, panL = 1, panR = 1;
    };

    //--------------------------------------------------------------------------
    // piano
    void startPiano (int m, float vel, int hold = -1)
    {
        if (! piano) return;
        const auto* s = piano->nearest (m);
        if (s == nullptr) return;
        auto& v = allocVoice (pv);
        v = PianoVoice();
        v.on = true; v.note = m; v.s = s; v.age = ++ageCounter; v.holdLeft = hold;
        v.loopOn = s->looped();
        v.rate = std::exp2 (((double) (m - s->root) - s->fine) / 12.0) * s->sr / (double) sr;
        v.gain = (0.12f + 0.88f * std::pow (vel, 1.5f)) * piano->gain;
        const float x = std::min (1.0f, 0.4f * P.pnTone + 0.6f * vel);
        const float fc = 1500.0f * std::pow (14.0f, x);
        v.lpA = 1.0f - std::exp (-kTwoPi * std::min (fc, 0.45f * sr) / sr);
    }

    //--------------------------------------------------------------------------
    // pads
    void startPad (int m, float vel)
    {
        if (padBank && ! padBank->samples.empty())
        {
            const int target = std::max (0, std::min (127, m + 12 * P.pdOct));
            const auto* s = padBank->nearest (target);
            if (s != nullptr)
            {
                auto& v = allocVoice (pv);
                v = PianoVoice();
                v.on = true; v.pad = true; v.note = m; v.s = s; v.age = ++ageCounter; v.loopOn = s->looped();
                v.rate = std::exp2 (((double) (target - s->root) - s->fine) / 12.0) * s->sr / (double) sr;
                v.gain = (0.6f + 0.4f * vel) * padBank->gain;
                v.env = 0.0f; v.atk = 1.0f / std::max (1.0f, P.pdAttack * sr);
                const float x = std::min (1.0f, 0.15f + 0.85f * P.pdBright);
                v.lpA = 1.0f - std::exp (-kTwoPi * std::min (1500.0f * std::pow (14.0f, x), 0.45f * sr) / sr);
                return;
            }
        }
        auto& v = allocVoice (dv);
        v = PadVoice();
        v.on = true; v.note = m; v.vel = vel; v.age = ++ageCounter;
        v.freq = midiToHz ((float) (m + 12 * P.pdOct));
        v.lfo = rng.f(); v.lfoInc = (0.15f + 0.25f * rng.f()) / sr;
        for (auto& p : v.ph) p = rng.f();
        v.sub = rng.f();
        v.fl.reset(); v.fr.reset();
    }

    void releaseNote (int m)
    {
        for (auto& v : pv) if (v.on && v.note == m) v.rel = true;
        for (auto& v : dv) if (v.on && v.note == m) v.rel = true;
    }

    template <class V, size_t N>
    V& allocVoice (std::array<V, N>& arr)
    {
        V* oldest = &arr[0];
        for (auto& v : arr) { if (! v.on) return v; if (v.age < oldest->age) oldest = &v; }
        return *oldest;
    }

    void rebuildHeld()
    {
        nHeld = 0;
        for (int m = 0; m < 128 && nHeld < 16; ++m)
            if (down[(size_t) m] || sust[(size_t) m]) held[(size_t) nHeld++] = m;
        if (nHeld == 0) { arpFresh = true; arpLast = -1; melodyPos = 0; }
        else if (nHeld > 0) keyPos = std::max (0.0f, std::min (1.0f, ((float) held[0] - 36.0f) / 60.0f));
        nature.setHeld (held, nHeld, P.root, P.scale);
    }

    //==========================================================================
    //  NATURE
    //==========================================================================
    struct Nature
    {
        double srD = 44100;
        float sr = 44100;
        Rng rng;
        int ctr = 0;
        bool birdsKey = true;
        int root = 0, scale = 0;

        // ---- water: a handful of resonant "gurgle" bands + a soft bed
        struct Band { Svf f; SmoothRand fm, am; float base = 500, gl = 1, gr = 1; };
        std::array<Band, 5> wb;
        Pink waterBed; float bedLp = 0;

        // ---- rain
        Svf rainHpL, rainHpR; SmoothRand rainFlut;
        struct Drop { bool on = false; Svf f; int left = 0; float gl = 1, gr = 1, kick = 0; };
        std::array<Drop, 10> drops;

        // ---- wind
        Pink windPinkL, windPinkR, tonePink;
        Svf windFL, windFR; SmoothRand windFreq, windGust, windFreq2;
        std::array<Svf, 4> reso; std::array<float, 4> resoPan { { -0.7f, -0.25f, 0.25f, 0.7f } };
        std::array<float, 4> resoHz { { 262, 330, 392, 524 } };

        // ---- birds
        struct Singer
        {
            int wait = 0, chirpsLeft = 0, gap = 0, pos = 0, len = 0, type = 0;
            double ph = 0, vib = 0; float f0 = 3000, f1 = 3000, amp = 0, panL = 1, panR = 1;
            float pool[3] = { 3000, 3500, 4000 };
        };
        std::array<Singer, 2> birds;

        // ---- crickets
        struct Cricket { double chirpPh = 0, car = 0; float rate = 2.2f, fc = 4500, gl = 1, gr = 1, gainMod = 1; SmoothRand mod; };
        std::array<Cricket, 3> crick;

        void prepare (float sampleRate, const Rng& seed)
        {
            sr = sampleRate; srD = sampleRate; rng = seed; rng.s ^= 0xa5a5a5a5u;
            const float base[5] = { 380, 640, 1100, 1750, 2700 };
            const float pan[5]  = { -0.5f, 0.6f, -0.3f, 0.7f, -0.7f };
            for (int i = 0; i < 5; ++i)
            {
                auto& b = wb[(size_t) i];
                b = Band();
                b.base = base[i];
                b.fm.setup (5.0f + 2.0f * (float) i, 10.0f + 3.0f * (float) i, sr);
                b.am.setup (3.0f + 1.5f * (float) i, 6.0f + 2.0f * (float) i, sr);
                b.gl = 0.5f * (1.0f - pan[i]) + 0.5f; b.gr = 0.5f * (1.0f + pan[i]) + 0.5f;
                b.fm.v = rng.bi(); b.am.v = rng.bi();
            }
            rainFlut.setup (1.5f, 3.0f, sr);
            rainHpL.set (3500.0f, 0.7f, sr); rainHpR.set (3200.0f, 0.7f, sr);
            windFreq.setup (0.17f, 0.3f, sr); windFreq2.setup (0.11f, 0.2f, sr); windGust.setup (0.08f, 0.15f, sr);
            for (auto& r : reso) r.set (400.0f, 40.0f, sr);
            for (int i = 0; i < 3; ++i)
            {
                auto& c = crick[(size_t) i];
                c.rate = 2.0f + 0.37f * (float) i; c.fc = 4300.0f + 230.0f * (float) i;
                c.chirpPh = rng.f(); c.mod.setup (0.3f, 0.5f, sr);
                const float p = -0.6f + 0.6f * (float) i;
                c.gl = 0.5f * (1.0f - p) + 0.5f; c.gr = 0.5f * (1.0f + p) + 0.5f;
            }
            for (auto& s : birds) { s = Singer(); s.wait = (int) (sr * (1.0f + 3.0f * rng.f())); }
        }

        void setHeld (const std::array<int, 16>& h, int n, int rootIn, int scaleIn)
        {
            root = rootIn; scale = scaleIn;
            // tuned wind: held notes (up an octave or two) - or the key's root / fifth / octave as a drone
            int notes[4];
            if (n > 0) for (int i = 0; i < 4; ++i) notes[i] = h[(size_t) (i % n)] + 12 + 12 * (i / std::max (n, 1) % 2);
            else { const int r = 48 + root; notes[0] = r + 12; notes[1] = r + 19; notes[2] = r + 24; notes[3] = r + 31; }
            for (int i = 0; i < 4; ++i)
            {
                float hz = midiToHz ((float) notes[i]);
                while (hz > 2400.0f) hz *= 0.5f;
                while (hz < 200.0f) hz *= 2.0f;
                resoHz[(size_t) i] = hz;
                reso[(size_t) i].set (hz, 45.0f, sr);
            }
        }

        // calibration so that level 1.0 of each layer is a sensible loudness
        static constexpr float calWater = 0.12f, calRain = 1.0f, calWind = 1.4f, calTone = 0.2f, calBirds = 0.34f, calCrick = 0.10f;

        void tick (float lWater, float lRain, float lWind, float lTone, float lBirds, float lCrick, float& l, float& r)
        {
            const bool ctl = (++ctr & 7) == 0;

            if (lWater > 0.0005f)
            {
                for (auto& b : wb)
                {
                    const float fmv = b.fm.tick (rng);
                    const float amv = b.am.tick (rng);
                    if (ctl) b.f.set (b.base * (1.0f + 0.55f * fmv), 7.0f, sr);
                    float lp, bp, hp; b.f.tick (rng.bi(), lp, bp, hp);
                    const float g = std::max (0.0f, std::min (1.0f, 0.5f + 0.9f * amv));
                    const float o = bp * (0.15f + 0.85f * g * g) * calWater * lWater;
                    l += o * b.gl; r += o * b.gr;
                }
                bedLp += 0.25f * (waterBed.tick (rng.bi()) - bedLp);
                const float bed = bedLp * 0.06f * lWater;
                l += bed; r += bed;
            }

            if (lRain > 0.0005f)
            {
                const float fl = 1.0f + 0.25f * rainFlut.tick (rng);
                float lp, bp, hp, hr;
                rainHpL.tick (rng.bi(), lp, bp, hp); rainHpR.tick (rng.bi(), lp, bp, hr);
                l += hp * 0.05f * fl * calRain * lRain;
                r += hr * 0.05f * fl * calRain * lRain;

                if (rng.f() < (45.0f + 70.0f * lRain) / sr)
                {
                    for (auto& d : drops) if (! d.on)
                    {
                        d.on = true; d.left = (int) (0.018f * sr);
                        const float hz = 1400.0f + 5600.0f * std::pow (rng.f(), 1.5f);
                        d.f.set (hz, 5.0f, sr); d.f.reset();
                        const float p = rng.bi() * 0.8f;
                        d.gl = 0.5f * (1.0f - p) + 0.5f; d.gr = 0.5f * (1.0f + p) + 0.5f;
                        d.kick = 0.25f + 0.75f * rng.f();
                        break;
                    }
                }
                for (auto& d : drops) if (d.on)
                {
                    float lp2, bp2, hp2; d.f.tick (d.kick, lp2, bp2, hp2); d.kick = 0.0f;
                    const float o = bp2 * 0.7f * lRain * calRain;
                    l += o * d.gl; r += o * d.gr;
                    if (--d.left <= 0) d.on = false;
                }
            }

            if (lWind > 0.0005f || lTone > 0.0005f)
            {
                const float f1 = windFreq.tick (rng), f2 = windFreq2.tick (rng);
                const float gust = 0.3f + 0.7f * std::pow (std::max (0.0f, std::min (1.0f, 0.5f + 0.9f * windGust.tick (rng))), 1.4f);
                if (ctl) { windFL.set (320.0f + 330.0f * f1 + 120.0f, 0.9f, sr); windFR.set (300.0f + 330.0f * f2 + 140.0f, 0.9f, sr); }
                float lp, bp, hp, lp2, bp2, hp2;
                windFL.tick (windPinkL.tick (rng.bi()), lp, bp, hp);
                windFR.tick (windPinkR.tick (rng.bi()), lp2, bp2, hp2);
                l += (bp + 0.4f * lp) * gust * calWind * lWind;
                r += (bp2 + 0.4f * lp2) * gust * calWind * lWind;

                if (lTone > 0.0005f)
                {
                    const float pn = tonePink.tick (rng.bi());
                    for (int i = 0; i < 4; ++i)
                    {
                        float a, b2, c;
                        reso[(size_t) i].tick (pn, a, b2, c);
                        const float o = b2 * gust * calTone * lTone;
                        const float p = resoPan[(size_t) i];
                        l += o * (0.5f - 0.5f * p + 0.5f); r += o * (0.5f + 0.5f * p + 0.5f);
                    }
                }
            }

            if (lBirds > 0.0005f)
                for (auto& s : birds) tickBird (s, lBirds, l, r);

            if (lCrick > 0.0005f)
            {
                for (auto& c : crick)
                {
                    c.chirpPh += c.rate / srD; if (c.chirpPh >= 1.0) c.chirpPh -= 1.0;
                    const float on = 0.38f;
                    float env = 0.0f;
                    if (c.chirpPh < on)
                    {
                        const float x = (float) c.chirpPh / on * 4.0f;
                        const float s = std::sin (kPi * (x - std::floor (x)));
                        env = s * s;
                    }
                    c.car += (c.fc * (1.0f + 0.01f * c.mod.tick (rng))) / srD; if (c.car >= 1.0) c.car -= 1.0;
                    const float o = std::sin (kTwoPi * (float) c.car) * env * calCrick * lCrick;
                    l += o * c.gl; r += o * c.gr;
                }
            }
        }

        void tickBird (Singer& s, float level, float& l, float& r)
        {
            if (s.wait > 0)
            {
                if (--s.wait == 0)
                {
                    // start a phrase
                    s.chirpsLeft = rng.range (3, 8);
                    s.type = rng.range (0, 2);
                    for (auto& p : s.pool)
                    {
                        if (birdsKey)
                        {
                            const int m = randomScaleNote (rng, root, scale, 86, 108);
                            p = midiToHz ((float) m);
                        }
                        else p = 1800.0f + 2800.0f * rng.f();
                    }
                    const float pan = rng.bi() * 0.8f;
                    s.panL = 0.5f * (1.0f - pan) + 0.5f; s.panR = 0.5f * (1.0f + pan) + 0.5f;
                    s.amp = 0.5f + 0.5f * rng.f();
                    s.gap = 0; s.pos = 0; s.len = 0;
                }
                return;
            }

            if (s.len == 0)               // between chirps
            {
                if (s.gap > 0) { --s.gap; return; }
                if (s.chirpsLeft <= 0)
                {
                    s.wait = (int) (sr * (1.5f + 5.0f * rng.f()) * (1.3f - 0.8f * std::min (1.0f, level)));
                    return;
                }
                --s.chirpsLeft;
                const float base = s.pool[rng.range (0, 2)];
                float durMs = 90.0f + 50.0f * rng.f();
                float sweep = 1.0f;
                if (s.type == 0) { sweep = 1.0f + 0.35f * rng.f(); }                                 // rising whistle
                else if (s.type == 1) { durMs = 55.0f + 30.0f * rng.f(); sweep = 1.0f - 0.3f * rng.f(); }  // falling trill
                else { durMs = 24.0f + 14.0f * rng.f(); sweep = 1.0f + 0.1f * rng.bi(); }              // quick pips
                s.f0 = base; s.f1 = base * sweep;
                s.len = std::max (16, (int) (durMs * 0.001f * sr)); s.pos = 0; s.ph = 0; s.vib = rng.f();
                s.gap = (int) ((s.type == 2 ? 30.0f : 45.0f + 45.0f * rng.f()) * 0.001f * sr);
            }

            const float u = (float) s.pos / (float) s.len;
            const float env = std::sin (kPi * u); const float e2 = env * env;
            const float f = (s.f0 + (s.f1 - s.f0) * u) * (1.0f + 0.012f * std::sin (kTwoPi * (float) s.vib));
            s.vib += 32.0 / srD; if (s.vib >= 1.0) s.vib -= 1.0;
            s.ph += f / srD; if (s.ph >= 1.0) s.ph -= 1.0;
            const float ph = (float) s.ph;
            const float o = (std::sin (kTwoPi * ph) + 0.18f * std::sin (kTwoPi * 2.0f * ph)) * e2 * s.amp * calBirds * level;
            l += o * s.panL; r += o * s.panR;
            if (++s.pos >= s.len) s.len = 0;
        }
    };

    //==========================================================================
    //  per-chunk control update and the sample loop
    //==========================================================================
    void renderChunk (float* L, float* R, int n)
    {
        const float g = P.grow;
        auto G = [g] (float k) { return std::exp2 ((g - 0.5f) * 2.0f * k); };
        if (firstChunk)
        {
            for (auto& l : lv) l.t = 0.0f;
            firstChunk = false;
        }
        lv[kPiano].t   = P.pnLevel * 0.9f;
        lv[kPad].t     = P.pdLevel * G (1.0f);
        padSm.t        = lv[kPad].t;
        lv[kBubble].t  = P.bbLevel;
        lv[kGrain].t   = P.grLevel * G (0.7f);
        lv[kWater].t   = P.nWater * G (1.2f);
        lv[kRain].t    = P.nRain * G (1.2f);
        lv[kWind].t    = P.nWind * G (1.2f);
        lv[kTone].t    = P.nWindTone * G (1.0f);
        lv[kBirds].t   = P.nBirds * G (1.2f);
        lv[kCrick].t   = P.nCrickets * G (1.2f);
        lv[kChirp].t   = P.chLevel * 0.7f * G (0.8f);
        lv[kRev].t     = std::min (1.0f, P.rvMix * G (0.8f));
        masterSm.t     = std::pow (10.0f, P.masterDb / 20.0f);

        const float bubbleRate = P.bbDensity * G (2.0f);
        const float padBright  = std::min (1.0f, P.pdBright * G (0.6f));
        nature.birdsKey = P.birdsKey;

        // scene gate + recorded beds
        const float gateUp = 1.0f - std::exp (-3.0f / (std::max (0.05f, P.ambIn) * sr));
        const float gateDn = 1.0f - std::exp (-3.0f / (std::max (0.05f, P.ambOut) * sr));
        for (int i = 0; i < 4; ++i)
        {
            float w = 1.0f;
            if (P.bdKeys) w = std::max (0.0f, 1.0f - std::fabs (keyPos * 3.0f - (float) i)) * 1.25f;
            bedLv[(size_t) i].t = P.bdLevel * P.bdGain[(size_t) i] * w * G (1.0f);
        }
        smAmt.t = P.gaOn ? std::max (0.0f, std::min (1.0f, P.gaMix)) : 0.0f;
        if (P.gaFreeze && ! smFrozen) { smFrozen = true; smFrozenW = smW; }
        else if (! P.gaFreeze) smFrozen = false;
        static const double arBeatsTab[4] = { 1.0, 0.5, 0.25, 0.125 };
        const double arBeats = arBeatsTab[std::max (0, std::min (3, P.arRate))];

        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.3f + 0.69f * P.rvSize; rp.damping = P.rvDamp; rp.wetLevel = 1.0f;
            rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = 0.0f;
            reverb.setParameters (rp);
        }

        // pad detune ratios + envelope times (control rate)
        const float det = P.pdDetune;
        const float ratios[4] = { std::exp2 (-det / 1200.0f), std::exp2 (-det * 0.35f / 1200.0f),
                                  std::exp2 (det * 0.35f / 1200.0f), std::exp2 (det / 1200.0f) };
        const float atkInc = 1.0f / std::max (1.0f, P.pdAttack * sr);
        const float padRelMul = std::pow (1.0e-3f, 1.0f / std::max (1.0f, P.pdRel * sr));
        const float pnRelMul  = std::pow (1.0e-3f, 1.0f / std::max (1.0f, P.pnRel * sr));

        // pattern clock
        const double stepBeatsTab[4] = { 1.0, 0.5, 0.25, 0.125 };
        const double stepBeats = stepBeatsTab[std::max (0, std::min (3, P.grRate))];
        const double ppqPerSample = bpm / 60.0 / (double) sr;
        const int stepLenSamples = std::max (1, (int) (stepBeats / ppqPerSample));
        const bool useHost = hostPlaying && havePpq;
        if (useHost) internalPpq = ppqStart;

        const float cloudProb = P.grDensity * G (0.7f) / sr;

        for (int i = 0; i < n; ++i)
        {
            ++ctrCount;
            // ---- clock
            if (useHost) { ppqNow = ppqStart + (double) i * ppqPerSample; internalPpq = ppqNow; }
            else         { internalPpq += ppqPerSample; ppqNow = internalPpq; }
            const long long stepIdx = (long long) std::floor (ppqNow / stepBeats);
            if (stepIdx != lastStepIdx)
            {
                lastStepIdx = stepIdx;
                const int s = (int) (((stepIdx % 16) + 16) % 16);
                uiStep.store (nHeld > 0 || hostPlaying ? s : -1, std::memory_order_relaxed);
                if (P.grPattern && P.stepOn[(size_t) s] && nHeld > 0) triggerStep (P.stepDeg[(size_t) s], stepLenSamples);
            }
            if (! P.grPattern && nHeld > 0 && rng.f() < cloudProb)
                spawnGrain (held[(size_t) rng.range (0, nHeld - 1)], 0, 0);

            // ---- scene gate
            {
                const float tgt = (! P.ambGate || nHeld > 0) ? 1.0f : 0.0f;
                gate += (tgt > gate ? gateUp : gateDn) * (tgt - gate);
            }
            // ---- piano arpeggio clock
            if (P.arMode != 0)
            {
                const long long ai = (long long) std::floor (ppqNow / arBeats);
                if (ai != lastArpStep) { lastArpStep = ai; if (nHeld > 0) arpStep (std::max (1, (int) (arBeats / ppqPerSample))); }
            }
            // ---- recorded chirps
            if (gate > 0.05f && rng.f() < P.chRate * gate / sr) spawnChirp();

            // ---- bubbles
            if (gate > 0.02f && rng.f() < bubbleRate * gate / sr) { spawnBubble(); if (rng.f() < 0.3f && pendingN < 8) pending[(size_t) pendingN++] = (int) (sr * (0.02f + 0.08f * rng.f())); }
            for (int k = 0; k < pendingN;)
            {
                if (--pending[(size_t) k] <= 0) { spawnBubble(); pending[(size_t) k] = pending[(size_t) --pendingN]; }
                else ++k;
            }

            float dl = 0, dr = 0;           // dry bus
            float sl = 0, sr_ = 0;          // reverb send

            // ---- piano
            {
                float pl = 0, pr = 0, ql = 0, qr = 0;
                for (auto& v : pv) if (v.on) { if (v.pad) tickPiano (v, ql, qr, pnRelMul, padRelMul); else tickPiano (v, pl, pr, pnRelMul, padRelMul); }
                const float a = lv[kPiano].tick(), ap = lv[kPad].tick();
                dl += pl * a; dr += pr * a; sl += pl * a * 0.30f; sr_ += pr * a * 0.30f;
                dl += ql * ap; dr += qr * ap; sl += ql * ap * 0.45f; sr_ += qr * ap * 0.45f;     // sampled pad voices
            }
            // ---- pads
            {
                float pl = 0, pr = 0;
                for (auto& v : dv) if (v.on) tickPad (v, pl, pr, ratios, atkInc, padRelMul, padBright);
                const float a = padSm.tick();
                dl += pl * a; dr += pr * a; sl += pl * a * 0.45f; sr_ += pr * a * 0.45f;
            }
            // ---- bubbles
            {
                float bl = 0, br = 0;
                for (auto& b : bubbles) if (b.on) tickBubble (b, bl, br);
                const float a = lv[kBubble].tick() * 0.9f * gate;
                dl += bl * a; dr += br * a; sl += bl * a * 0.65f; sr_ += br * a * 0.65f;
            }
            // ---- grains
            {
                float gl = 0, gr = 0;
                for (auto& gn : grains) if (gn.on) tickGrain (gn, gl, gr);
                const float a = lv[kGrain].tick() * 0.45f;
                dl += gl * a; dr += gr * a; sl += gl * a * 0.55f; sr_ += gr * a * 0.55f;
            }
            // ---- nature
            {
                float nl = 0, nr = 0;
                const float a0 = lv[kWater].tick(), a1 = lv[kRain].tick(), a2 = lv[kWind].tick(), a3 = lv[kTone].tick(),
                            a4 = lv[kBirds].tick(), a5 = lv[kCrick].tick();
                nature.tick (a0 * gate, a1 * gate, a2 * gate, a3 * gate, a4 * gate, a5 * gate, nl, nr);
                dl += nl; dr += nr; sl += nl * 0.25f; sr_ += nr * 0.25f;
            }
            // ---- recorded beds
            if (natureBank)
            {
                float bl = 0, br = 0;
                for (int k = 0; k < 4; ++k)
                {
                    if (! natureBank->beds[(size_t) k]) continue;
                    const auto& smp = *natureBank->beds[(size_t) k];
                    const int len = smp.data.getNumSamples();
                    if (len < 8) continue;
                    double& pos = bedPos[(size_t) k];
                    pos += smp.sr / (double) sr; if (pos >= (double) len) pos -= (double) len;
                    const float a = bedLv[(size_t) k].tick();
                    if (a > 1.0e-5f)
                    {
                        const int i0 = (int) pos; const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
                        const float f = (float) (pos - (double) i0);
                        const float* d0 = smp.data.getReadPointer (0); const float* d1 = smp.data.getReadPointer (1);
                        bl += (d0[i0] + f * (d0[i1] - d0[i0])) * a;
                        br += (d1[i0] + f * (d1[i1] - d1[i0])) * a;
                    }
                }
                const float gg = gate * 0.5f;
                dl += bl * gg; dr += br * gg; sl += bl * gg * 0.3f; sr_ += br * gg * 0.3f;
            }
            // ---- chirps
            {
                float cl = 0, cr = 0;
                for (auto& c : chirpV) if (c.on) tickChirp (c, cl, cr);
                const float a = lv[kChirp].tick() * gate;
                dl += cl * a; dr += cr * a; sl += cl * a * 0.5f; sr_ += cr * a * 0.5f;
            }

            // ---- smear: granulate the whole mix
            if (smLen > 0)
            {
                if (! smFrozen) { smL[(size_t) smW] = dl; smR[(size_t) smW] = dr; if (++smW >= smLen) smW = 0; }
                const float amt = smAmt.tick();
                if (amt > 1.0e-4f)
                {
                    if (rng.f() < P.gaDensity / sr) spawnSmear();
                    float wl = 0, wr = 0;
                    for (auto& g : smG) if (g.on) tickSmear (g, wl, wr);
                    const float a = amt * 1.5707963f, cd = std::cos (a), cw = std::sin (a);
                    sl += wl * cw * 0.3f; sr_ += wr * cw * 0.3f;
                    dl = dl * cd + wl * cw; dr = dr * cd + wr * cw;
                }
                else for (auto& g : smG) g.on = false;
            }

            L[i] = dl; R[i] = dr;
            const float send = 0.5f * (sl + sr_);
            rvL[(size_t) i] = rvR[(size_t) i] = send;
        }

        reverb.processStereo (rvL.data(), rvR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float mix = lv[kRev].tick() * kReverbOut;
            const float m = masterSm.tick();
            const float l = (L[i] + rvL[(size_t) i] * mix) * m;
            const float r = (R[i] + rvR[(size_t) i] * mix) * m;
            L[i] = std::tanh (l);
            R[i] = std::tanh (r);
        }
    }

    //--------------------------------------------------------------------------
    inline void tickPiano (PianoVoice& v, float& l, float& r, float pnRelMul, float padRelMul)
    {
        const auto& smp = *v.s;
        const int len = smp.data.getNumSamples();
        const float* d0 = smp.data.getReadPointer (0);
        const float* d1 = smp.data.getReadPointer (1);
        if (v.rel && ! v.xf) v.loopOn = false;                 // released: play on into the recorded tail

        float a, b;
        if (smp.looped() && (v.loopOn || v.xf) && v.pos >= (double) (smp.loopEnd - smp.xfade))
        {
            // sustain loop: cross-fade the end of the loop into the same material one loop-length earlier, then jump back
            v.xf = true;
            const double L = (double) (smp.loopEnd - smp.loopStart);
            const float t = (float) ((v.pos - (double) (smp.loopEnd - smp.xfade)) / (double) smp.xfade);
            const float c = std::cos (1.5707964f * t), sn = std::sin (1.5707964f * t);
            a = c * readHermite (d0, len, v.pos) + sn * readHermite (d0, len, v.pos - L);
            b = c * readHermite (d1, len, v.pos) + sn * readHermite (d1, len, v.pos - L);
            v.pos += v.rate;
            if (v.pos >= (double) smp.loopEnd) { v.pos -= L; v.xf = false; }
        }
        else
        {
            if (v.pos >= (double) (len - 3)) { v.on = false; return; }
            a = readHermite (d0, len, v.pos);
            b = readHermite (d1, len, v.pos);
            v.pos += v.rate;
        }
        v.lpL += v.lpA * (a - v.lpL);
        v.lpR += v.lpA * (b - v.lpR);
        v.fade = std::min (1.0f, v.fade + 1.0f / 96.0f);
        if (v.holdLeft > 0 && --v.holdLeft == 0) v.rel = true;
        if (v.rel) { v.env *= (v.pad ? padRelMul : pnRelMul); if (v.env < 1.0e-4f) { v.on = false; return; } }
        else if (v.env < 1.0f) v.env = std::min (1.0f, v.env + v.atk);
        const float g = v.gain * v.env * v.fade;
        l += v.lpL * g; r += v.lpR * g;
    }

    inline void tickPad (PadVoice& v, float& l, float& r, const float* ratios, float atkInc, float relMul, float bright)
    {
        if (v.rel) { v.env *= relMul; if (v.env < 1.0e-4f) { v.on = false; return; } }
        else if (v.env < 1.0f) v.env = std::min (1.0f, v.env + atkInc);

        float s[4];
        for (int k = 0; k < 4; ++k)
        {
            const float dt = v.freq * ratios[k] / sr;
            v.ph[k] += dt; if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
            const float t = (float) v.ph[k];
            s[k] = 2.0f * t - 1.0f - polyBlep (t, dt);
        }
        v.sub += 0.5f * v.freq / sr; if (v.sub >= 1.0) v.sub -= 1.0;
        const float sub = std::sin (kTwoPi * (float) v.sub) * 0.45f;

        const float inL = s[0] * 0.95f + s[1] * 0.7f + s[2] * 0.3f + s[3] * 0.05f + sub;
        const float inR = s[0] * 0.05f + s[1] * 0.3f + s[2] * 0.7f + s[3] * 0.95f + sub;

        if ((ctrCount & 15) == (int) (v.age & 15))
        {
            v.lfo += 16.0f * v.lfoInc * 1.0f; if (v.lfo >= 1.0f) v.lfo -= 1.0f;
            const float lfo = std::sin (kTwoPi * v.lfo);
            const float x = std::min (1.0f, 0.15f + 0.85f * bright + 0.08f * v.vel);
            v.cutoff = 220.0f * std::pow (38.0f, x) * (1.0f + 0.25f * lfo) * (0.55f + 0.45f * std::min (1.0f, v.env * 1.5f));
            v.fl.set (v.cutoff, 0.8f, sr); v.fr.set (v.cutoff, 0.8f, sr);
        }
        float a, b, c, a2, b2, c2;
        v.fl.tick (inL, a, b, c); v.fr.tick (inR, a2, b2, c2);
        const float g = v.env * 0.12f * (0.5f + 0.5f * v.vel);
        l += a * g; r += a2 * g;
    }

    //--------------------------------------------------------------------------
    // bubbles
    void spawnBubble()
    {
        const int lo = 60 + (int) (P.bbRegister * 30.0f);
        const int hi = lo + 18;
        int midi;
        if (nHeld > 0 && rng.f() < 0.7f)
        {
            const int h = held[(size_t) rng.range (0, nHeld - 1)];
            midi = lo + ((((h - lo) % 12) + 12) % 12) + 12 * rng.range (0, 1);
        }
        else if (P.bbKey) midi = randomScaleNote (rng, P.root, P.scale, lo, hi);
        else midi = rng.range (lo, hi);

        bubbleHist[(size_t) std::max (0, std::min (127, midi))]++;
        for (auto& b : bubbles) if (! b.on)
        {
            const float hz = midiToHz ((float) midi);
            const float chirp = 0.05f + 0.7f * P.bbBloop;
            const float tau = (0.5f + 2.5f * P.bbSize) * (0.004f + 8.0f / hz);
            b.on = true; b.ph = 0;
            b.f0 = hz / (1.0f + 0.55f * chirp); b.chirp = chirp;
            b.amp = 0.2f + 0.8f * rng.f() * rng.f() + 0.1f;
            events.push (0, ((float) midi - 60.0f) / 48.0f, b.amp);
            const float p = rng.bi() * 0.85f;
            b.panL = std::sqrt (0.5f * (1.0f - p)); b.panR = std::sqrt (0.5f * (1.0f + p));
            b.e = 1.0f; b.eDec = std::exp (-1.0f / (tau * sr));
            b.c = 1.0f; b.cDec = std::exp (-1.0f / (1.2f * tau * sr));
            b.a = 1.0f; b.aDec = std::exp (-1.0f / (0.0006f * sr));
            b.life = (int) (8.0f * tau * sr);
            return;
        }
    }

    inline void tickBubble (Bubble& b, float& l, float& r)
    {
        const float f = b.f0 * (1.0f + b.chirp * (1.0f - b.c));
        b.ph += f / sr; if (b.ph >= 1.0) b.ph -= 1.0;
        const float o = std::sin (kTwoPi * (float) b.ph) * b.e * (1.0f - b.a) * b.amp;
        b.e *= b.eDec; b.c *= b.cDec; b.a *= b.aDec;
        l += o * b.panL; r += o * b.panR;
        if (--b.life <= 0) b.on = false;
    }

    //--------------------------------------------------------------------------
    // piano performance: chord arpeggio / seeded melody
    Chord detectChord() const
    {
        Chord c;
        const int bass = held[0];
        bool seen[12] = {}; int cnt = 0;
        for (int k = 0; k < nHeld; ++k)
        {
            const int o = (((held[(size_t) k] - bass) % 12) + 12) % 12;
            if (! seen[o]) { seen[o] = true; ++cnt; }
        }
        if (cnt >= 2)
        {
            c.bass = bass; c.n = 0;
            for (int o = 0; o < 12 && c.n < 8; ++o) if (seen[o]) c.off[c.n++] = o;
            return c;
        }
        // one note: build the triad of the key on it
        const int q = quantizeToScale (bass, P.root, P.scale);
        const int p1 = scaleStep (q, P.root, P.scale, 2), p2 = scaleStep (q, P.root, P.scale, 4);
        c.bass = q; c.n = 3; c.off[0] = 0;
        int o1 = (((p1 - q) % 12) + 12) % 12, o2 = (((p2 - q) % 12) + 12) % 12;
        if (o1 > o2) std::swap (o1, o2);
        c.off[1] = o1; c.off[2] = o2;
        if (o1 == 0 || o1 == o2) { c.n = 2; c.off[1] = std::max (o1, o2); }
        return c;
    }

    void melodyStep (int stepLen)
    {
        if (arpFresh) { melodyPos = 0; arpFresh = false; }
        const int pos = melodyPos % 16; ++melodyPos;
        const int oc = std::max (1, std::min (3, P.arOct));
        const Chord ch = detectChord();
        const auto t = melodyToken (P.arSeed, pos, P.arRest, ch.n, ch.n * oc);
        if (t.rest) return;
        int note = ch.note (t.idx);
        if (t.pass != 0) note = scaleStep (note, P.root, P.scale, t.pass);
        note = std::max (12, std::min (120, note));
        arpLast = note;
        arpPosLog[arpCount & 63u] = pos;
        arpLog[arpCount & 63u] = note; ++arpCount;
        const float vel = std::max (0.25f, std::min (1.0f, t.vel));
        events.push (1, ((float) note - 24.0f) / 84.0f, vel);
        startPiano (note, vel, std::max (64, (int) (t.len * (float) stepLen)));
    }

    void arpStep (int stepLen)
    {
        if (! piano || nHeld == 0 || P.arMode == 0) return;
        if (P.arMode == 2) { melodyStep (stepLen); return; }
        if (rng.f() < P.arRest) return;
        const int oc = std::max (1, std::min (3, P.arOct));
        int note = held[0];

        if (P.arMode == 1)
        {
            int pool[64]; int n = 0;
            for (int o = 0; o < oc; ++o)
                for (int k = 0; k < nHeld && n < 64; ++k)
                    pool[n++] = held[(size_t) k] + 12 * o;
            std::sort (pool, pool + n);
            n = (int) (std::unique (pool, pool + n) - pool);
            if (arpFresh)
            {
                arpIdx = P.arPattern == 1 ? n - 1 : 0; arpDir = P.arPattern == 1 ? -1 : 1; arpFresh = false;
            }
            else
            {
                arpIdx = std::min (arpIdx, n - 1);
                switch (P.arPattern)
                {
                    case 0: arpIdx = (arpIdx + 1) % n; break;
                    case 1: arpIdx = (arpIdx + n - 1) % n; break;
                    case 2:
                        if (n > 1)
                        {
                            if (arpIdx + arpDir < 0 || arpIdx + arpDir > n - 1) arpDir = -arpDir;
                            arpIdx += arpDir;
                        }
                        break;
                    default: arpIdx = rng.range (0, n - 1); break;
                }
            }
            note = pool[std::max (0, std::min (n - 1, arpIdx))];
        }
        arpFresh = false;
        arpLast = note;
        arpLog[arpCount & 63u] = note; ++arpCount;
        const float vel = 0.45f + 0.4f * rng.f();
        events.push (1, ((float) note - 24.0f) / 84.0f, vel);
        startPiano (note, vel, std::max (64, (int) (0.9f * (float) stepLen)));
    }

    //--------------------------------------------------------------------------
    // smear: grains read from the last few seconds of the plugin's own output
    void spawnSmear()
    {
        for (auto& g : smG) if (! g.on)
        {
            const float sizeMs = std::max (10.0f, std::min (1000.0f, P.gaSizeMs));
            const int len = std::max (64, (int) (sizeMs * 0.001f * sr * (0.8f + 0.4f * rng.f())));
            float ratio = 1.0f;
            if (rng.f() < P.gaPitch) { static const float r[4] = { 2.0f, 2.0f, 0.5f, 1.4983f }; ratio = r[rng.range (0, 3)]; }
            const int ref = smFrozen ? smFrozenW : smW;
            const int minD = (int) ((float) len * std::max (1.0f, ratio)) + 64;
            const int delay = minD + (int) (rng.f() * 3.0f * sr * std::max (0.0f, std::min (1.0f, P.gaScatter)));
            double pos = (double) (ref - delay);
            while (pos < 0.0) pos += (double) smLen;
            const float pn = rng.bi() * 0.9f;
            g.on = true; g.pos = pos; g.inc = (double) ratio; g.age = 0; g.len = len;
            g.gain = 1.0f / std::sqrt (std::max (1.0f, P.gaDensity * sizeMs * 0.001f * 0.375f));
            g.panL = 1.0f - 0.5f * pn; g.panR = 1.0f + 0.5f * pn;
            return;
        }
    }

    inline void tickSmear (SmearGrain& g, float& l, float& r)
    {
        if (g.age >= g.len) { g.on = false; return; }
        const float w = hann[(size_t) std::min (1023, (int) (1023.0f * (float) g.age / (float) g.len))];
        const int i0 = (int) g.pos; const int i1 = i0 + 1 >= smLen ? 0 : i0 + 1;
        const float f = (float) (g.pos - (double) i0);
        const float a = smL[(size_t) i0] + f * (smL[(size_t) i1] - smL[(size_t) i0]);
        const float b = smR[(size_t) i0] + f * (smR[(size_t) i1] - smR[(size_t) i0]);
        l += a * w * g.gain * g.panL; r += b * w * g.gain * g.panR;
        g.pos += g.inc; if (g.pos >= (double) smLen) g.pos -= (double) smLen;
        ++g.age;
    }

    //--------------------------------------------------------------------------
    // recorded bird chirps
    void spawnChirp()
    {
        if (! natureBank || natureBank->chirps.empty()) return;
        for (auto& c : chirpV) if (! c.on)
        {
            const auto& sp = natureBank->chirps[(size_t) rng.range (0, (int) natureBank->chirps.size() - 1)];
            if (! sp) return;
            const auto& s = *sp;
            c.on = true; c.s = &s; c.pos = 0.0;
            c.inc = s.sr / (double) sr * (1.0 + 0.05 * (double) rng.bi());
            c.gain = 0.45f + 0.55f * rng.f();
            const float p = rng.bi() * 0.85f;
            c.panL = std::sqrt (0.5f * (1.0f - p)) * 1.2f; c.panR = std::sqrt (0.5f * (1.0f + p)) * 1.2f;
            return;
        }
    }

    inline void tickChirp (ChirpVoice& c, float& l, float& r)
    {
        const int len = c.s->data.getNumSamples();
        const int i0 = (int) c.pos;
        if (i0 >= len - 2) { c.on = false; return; }
        const float f = (float) (c.pos - (double) i0);
        const float* d = c.s->data.getReadPointer (0);
        const float o = (d[i0] + f * (d[i0 + 1] - d[i0])) * c.gain;
        c.pos += c.inc;
        l += o * c.panL; r += o * c.panR;
    }

    //--------------------------------------------------------------------------
    // granulizer
    void triggerStep (int degree, int stepLen)
    {
        const int burst = std::max (1, (int) std::lround (P.grBurst));
        const int voices = std::min (nHeld, 3);
        for (int v = 0; v < voices; ++v)
        {
            const int note = nHeld <= 3 ? held[(size_t) v] : held[(size_t) rng.range (0, nHeld - 1)];
            events.push (2, ((float) note - 24.0f) / 84.0f, 0.5f);
            for (int b = 0; b < burst; ++b)
                spawnGrain (note, degree, (int) (rng.f() * P.grSpread * (float) stepLen));
        }
    }

    void spawnGrain (int note, int degree, int delay)
    {
        if (! piano) return;
        Grain* slot = nullptr;
        for (auto& g : grains) if (! g.on) { slot = &g; break; }
        if (slot == nullptr) return;

        const int scat = (int) std::lround (P.grPitchScatter);
        int base = note;
        if (rng.f() < P.grOct) base += rng.f() < 0.5f ? 12 : -12;
        const int steps = degree + (scat > 0 ? rng.range (-scat, scat) : 0);
        const int target = std::max (12, std::min (120, scaleStep (base, P.root, P.scale, steps)));
        const auto* s = piano->nearest (target);
        if (s == nullptr) return;
        grainHist[(size_t) target]++; grainSpawns++;

        const double rate = std::exp2 (((double) (target - s->root) - s->fine) / 12.0) * s->sr / (double) sr;
        const int len = std::max (64, (int) (P.grSizeMs * 0.001f * sr * (0.75f + 0.5f * rng.f())));
        const double span = (double) len * rate;
        const double total = (double) s->data.getNumSamples();
        const double usable = std::max (0.0, std::min (1.8 * s->sr, total - span - 8.0));
        const float posn = std::max (0.0f, std::min (1.0f, P.grPos + P.grPosScatter * rng.bi()));
        double start = posn * usable;

        slot->on = true; slot->s = s; slot->age = 0; slot->len = len; slot->delay = delay;
        const bool rev = rng.f() < P.grRev;
        if (rev) { slot->pos = start + span; slot->inc = -rate; } else { slot->pos = start; slot->inc = rate; }
        slot->gain = 0.6f + 0.4f * rng.f();
        const float p = rng.bi() * 0.7f;
        slot->panL = std::sqrt (0.5f * (1.0f - p)); slot->panR = std::sqrt (0.5f * (1.0f + p));
    }

    inline void tickGrain (Grain& g, float& l, float& r)
    {
        if (g.delay > 0) { --g.delay; return; }
        const int len = g.s->data.getNumSamples();
        if (g.pos < 0.0 || g.pos > (double) (len - 3) || g.age >= g.len) { g.on = false; return; }
        const float w = hann[(size_t) ((g.age * 1023) / g.len)];
        const float a = readHermite (g.s->data.getReadPointer (0), len, g.pos);
        const float b = readHermite (g.s->data.getReadPointer (1), len, g.pos);
        g.pos += g.inc; ++g.age;
        const float o = w * g.gain;
        l += a * o * g.panL * 1.4f; r += b * o * g.panR * 1.4f;
    }

    //--------------------------------------------------------------------------
    enum { kPiano, kPad, kBubble, kGrain, kWater, kRain, kWind, kTone, kBirds, kCrick, kChirp, kRev, kNumLv };
    static constexpr float kReverbOut = 0.55f;

    float sr = 44100.0f;
    int maxBlockSize = 512;
    Params P;
    std::shared_ptr<const PianoBank> piano;
    std::shared_ptr<const PianoBank> padBank;
    std::shared_ptr<const NatureBank> natureBank;
    Rng rng;

    std::array<PianoVoice, 40> pv;
    std::array<PadVoice, 10>   dv;
    std::array<Bubble, 28>     bubbles;
    std::array<Grain, 96>      grains;
    std::array<ChirpVoice, 8>  chirpV;
    std::array<SmearGrain, 48> smG;
    std::vector<float> smL, smR;
    int smLen = 0, smW = 0, smFrozenW = 0;
    bool smFrozen = false;
    Smooth smAmt;
    std::array<Smooth, 4>      bedLv;
    std::array<double, 4>      bedPos {};
    float gate = 0.0f, keyPos = 0.5f;
    bool arpFresh = true;
    int arpIdx = 0, arpDir = 1, arpLast = -1;
    long long lastArpStep = -1;
    int melodyPos = 0;
    Nature nature;
    juce::Reverb reverb;
    std::vector<float> rvL, rvR;
    std::array<float, 1024> hann {};

    std::array<Smooth, kNumLv> lv;
    Smooth masterSm, padSm;
    bool firstChunk = true;
    uint32_t ageCounter = 0;
    int ctrCount = 0;

    // notes
    std::array<bool, 128> down {}, sust {};
    std::array<int, 128> mapped = [] { std::array<int, 128> a {}; for (int i = 0; i < 128; ++i) a[(size_t) i] = i; return a; }();
    bool pedal = false;
    std::array<int, 16> held {};
    int nHeld = 0;
    std::array<int, 8> pending {};
    int pendingN = 0;

    // transport
    bool hostPlaying = false, havePpq = false;
    double bpm = 120.0, ppqStart = 0.0, ppqNow = 0.0, internalPpq = 0.0;
    long long lastStepIdx = -1;

};

} // namespace bot
