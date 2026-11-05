// Offline tests of the Botanica engine (no DAW, no GUI).
//   BotanicaTests [path/to/piano.pak]
#include <juce_core/juce_core.h>
#include "BotanicaSamples.h"
#include "BotanicaImport.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <set>
#include <map>
#include <string>

using namespace bot;

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do { const bool ok_ = (cond);                                                          \
         std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); \
         if (! ok_) ++failures; } while (0)

// ------------------------------------------------------------------ helpers
static std::shared_ptr<PianoBank> gBank;
static std::shared_ptr<NatureBank> gNature;
static juce::MemoryBlock gInstPak;
static std::vector<InstIndex> gInstIndex;
static std::shared_ptr<PianoBank> gPianoOverride, gPadOverride;      // set by tests that audition other instruments

static Params silent()
{
    Params p;
    p.pnLevel = p.pdLevel = p.bbLevel = p.grLevel = 0.0f;
    p.nWater = p.nRain = p.nWind = p.nWindTone = p.nBirds = p.nCrickets = 0.0f;
    p.rvMix = 0.0f; p.masterDb = 0.0f; p.grow = 0.5f;
    p.arMode = 0; p.ambGate = false; p.bdLevel = 0.0f; p.chLevel = 0.0f;
    return p;
}

struct Rig
{
    Engine e;
    double sr;
    explicit Rig (double sampleRate = 48000.0, const Params& p = silent()) : sr (sampleRate)
    {
        e.prepare (sr, 512);
        e.setPiano (gBank);
        e.setNature (gNature);
        if (gPianoOverride) e.setPiano (gPianoOverride);
        if (gPadOverride) e.setPadBank (gPadOverride);
        e.setParams (p);
        e.setTransport (false, 120.0, false, 0.0);
    }
    struct Out { std::vector<float> L, R; };

    Out run (double seconds)
    {
        const int total = (int) (seconds * sr);
        Out o; o.L.resize ((size_t) total); o.R.resize ((size_t) total);
        for (int i = 0; i < total; i += 480)
            e.render (o.L.data() + i, o.R.data() + i, std::min (480, total - i));
        return o;
    }
};

static std::vector<float> mono (const Rig::Out& o, double sr, double t0, double t1)
{
    const size_t a = (size_t) (t0 * sr), b = std::min (o.L.size(), (size_t) (t1 * sr));
    std::vector<float> m (b - a);
    for (size_t i = a; i < b; ++i) m[i - a] = 0.5f * (o.L[i] + o.R[i]);
    return m;
}

static double rmsOf (const std::vector<float>& v)
{
    double s = 0; for (float x : v) s += (double) x * x;
    return std::sqrt (s / (double) std::max<size_t> (1, v.size()));
}
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-9)); }

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

struct Spectrum
{
    std::vector<double> mag; double binHz = 1;
    Spectrum (const std::vector<float>& x, double sr, size_t N = 1 << 16)
    {
        std::vector<std::complex<double>> a (N);
        const size_t n = std::min (N, x.size());
        for (size_t i = 0; i < n; ++i)
            a[i] = (double) x[i] * (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) (n - 1)));
        fft (a);
        mag.resize (N / 2);
        for (size_t i = 0; i < N / 2; ++i) mag[i] = std::abs (a[i]);
        binHz = sr / (double) N;
    }
    // frequency of the strongest peak within +-cents of f (parabolic interpolation)
    double peakNear (double f, double cents = 60.0) const
    {
        const size_t lo = (size_t) (f * std::pow (2.0, -cents / 1200.0) / binHz), hi = (size_t) (f * std::pow (2.0, cents / 1200.0) / binHz) + 1;
        size_t k = lo; for (size_t i = lo; i <= hi && i + 1 < mag.size(); ++i) if (mag[i] > mag[k]) k = i;
        const double a = std::log (mag[k - 1] + 1e-12), b = std::log (mag[k] + 1e-12), c = std::log (mag[k + 1] + 1e-12);
        return ((double) k + 0.5 * (a - c) / (a - 2 * b + c)) * binHz;
    }
    double power (double f0, double f1) const
    {
        double s = 0;
        for (size_t i = (size_t) (f0 / binHz); i < (size_t) (f1 / binHz) && i < mag.size(); ++i) s += mag[i] * mag[i];
        return s;
    }
    double centroid (double f0 = 50, double f1 = 20000) const
    {
        double n = 0, d = 0;
        for (size_t i = (size_t) (f0 / binHz); i < (size_t) (f1 / binHz) && i < mag.size(); ++i) { n += mag[i] * mag[i] * (double) i * binHz; d += mag[i] * mag[i]; }
        return n / std::max (d, 1e-30);
    }
};

// the loudest `secs`-long stretch of a signal (for sparse sounds like birdsong)
static std::vector<float> loudest (const std::vector<float>& v, double sr, double secs)
{
    const size_t w = (size_t) (secs * sr), hop = (size_t) (0.25 * sr);
    size_t best = 0; double bestE = -1;
    for (size_t a = 0; a + w <= v.size(); a += hop)
    {
        double e = 0; for (size_t i = a; i < a + w; ++i) e += (double) v[i] * v[i];
        if (e > bestE) { bestE = e; best = a; }
    }
    return std::vector<float> (v.begin() + (long) best, v.begin() + (long) (best + w));
}

static double hz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

// ------------------------------------------------------------------ tests
int main (int argc, char** argv)
{
    const juce::File pakFile (argc > 1 ? argv[1] : "piano.pak");
    juce::MemoryBlock pak;
    if (! pakFile.loadFileAsData (pak)) { std::printf ("cannot read %s\n", pakFile.getFullPathName().toRawUTF8()); return 2; }
    gBank = loadPianoPak (pak.getData(), pak.getSize());
    {
        juce::MemoryBlock npak;
        const juce::File nf = pakFile.getSiblingFile ("nature.pak");
        if (! nf.loadFileAsData (npak)) { std::printf ("cannot read %s\n", nf.getFullPathName().toRawUTF8()); return 2; }
        gNature = loadNaturePak (npak.getData(), npak.getSize());
        const juce::File inf = pakFile.getSiblingFile ("inst.pak");
        if (! inf.loadFileAsData (gInstPak)) { std::printf ("cannot read %s\n", inf.getFullPathName().toRawUTF8()); return 2; }
        gInstIndex = indexInstPak (gInstPak.getData(), gInstPak.getSize());
    }

    std::printf ("== Piano samples ==\n");
    {
        bool rootsOk = gBank->samples.size() == 25, lenOk = true, stereo = true;
        for (size_t i = 0; i < gBank->samples.size(); ++i)
        {
            const auto& s = gBank->samples[i];
            rootsOk = rootsOk && s.root == 24 + 3 * (int) i;
            lenOk = lenOk && s.data.getNumSamples() > (int) (1.0 * s.sr);
            stereo = stereo && s.data.getMagnitude (0, 0, s.data.getNumSamples()) > 0.3f && s.data.getMagnitude (1, 0, s.data.getNumSamples()) > 0.3f;
        }
        CHECK (rootsOk, "25 samples, roots C1..C7 every minor third");
        CHECK (lenOk, "every sample longer than 1 s");
        CHECK (stereo, "both channels carry signal");
    }

    std::printf ("== Piano pitch (engine output vs equal temperament, 48 kHz) ==\n");
    {
        double worst = 0;
        for (int note : { 48, 52, 57, 60, 61, 62, 66, 70, 72, 74, 78, 81, 84, 88, 93, 96 })
        {
            Params p = silent(); p.pnLevel = 1.0f;
            Rig r (48000.0, p); r.e.noteOn (note, 0.8f);
            auto o = r.run (1.3);
            const double f = Spectrum (mono (o, 48000, 0.05, 1.05), 48000).peakNear (hz (note), 70.0);
            const double c = cents (f, hz (note));
            worst = std::max (worst, std::abs (c));
            if (std::abs (c) > 12) std::printf ("      note %d: %+.1f cents\n", note, c);
        }
        CHECK (worst < 12.0, "fundamental within 12 cents of true pitch on all tested notes (worst %.1f cents)", worst);

        // same at 96 kHz and 44.1 kHz (sample-rate conversion is part of the voice)
        for (double sr : { 44100.0, 96000.0 })
        {
            Params p = silent(); p.pnLevel = 1.0f;
            Rig r (sr, p); r.e.noteOn (64, 0.8f);
            auto o = r.run (1.3);
            const double c = cents (Spectrum (mono (o, sr, 0.05, 1.05), sr).peakNear (hz (64), 70.0), hz (64));
            CHECK (std::abs (c) < 10.0, "E4 at %.0f Hz sample rate: %+.1f cents", sr, c);
        }
    }

    std::printf ("== Piano expression ==\n");
    {
        Params p = silent(); p.pnLevel = 1.0f; p.pnTone = 0.5f;
        Rig a (48000.0, p), b (48000.0, p);
        a.e.noteOn (60, 0.25f); b.e.noteOn (60, 1.0f);
        auto oa = a.run (1.0), ob = b.run (1.0);
        const auto ma = mono (oa, 48000, 0.02, 0.5), mb = mono (ob, 48000, 0.02, 0.5);
        const double hfA = Spectrum (ma, 48000).power (3000, 12000) / Spectrum (ma, 48000).power (100, 3000);
        const double hfB = Spectrum (mb, 48000).power (3000, 12000) / Spectrum (mb, 48000).power (100, 3000);
        CHECK (rmsOf (mb) > 2.0 * rmsOf (ma), "velocity: loud note %.1f dB louder than soft note", db (rmsOf (mb)) - db (rmsOf (ma)));
        CHECK (hfB > 1.5 * hfA, "velocity: loud note is brighter (HF share x%.1f)", hfB / hfA);

        // release
        p.pnRel = 0.3f; Rig r (48000.0, p); r.e.noteOn (60, 0.8f);
        auto o1 = r.run (0.5); const double before = rmsOf (mono (o1, 48000, 0.3, 0.5));
        r.e.noteOff (60); auto o2 = r.run (1.0);
        const double after = rmsOf (mono (o2, 48000, 0.5, 0.7));
        CHECK (db (after) < db (before) - 40.0, "release 0.3 s: >40 dB quieter 0.5 s after key up (%.0f dB)", db (after) - db (before));

        // sustain pedal keeps the note, lifting it releases
        Rig s (48000.0, p);
        s.e.controller (64, 127); s.e.noteOn (60, 0.8f); auto q1 = s.run (0.4);
        s.e.noteOff (60); auto q2 = s.run (0.6);
        const double held = rmsOf (mono (q2, 48000, 0.3, 0.6)); const double ref = rmsOf (mono (q1, 48000, 0.25, 0.4));
        s.e.controller (64, 0); auto q3 = s.run (1.0);
        const double gone = rmsOf (mono (q3, 48000, 0.6, 0.9));
        CHECK (db (held) > db (ref) - 20.0, "sustain pedal: note keeps ringing after key up (%.0f dB vs held)", db (held) - db (ref));
        CHECK (db (gone) < db (held) - 30.0, "releasing the pedal damps it (%.0f dB)", db (gone) - db (held));
    }

    std::printf ("== Scale lock of incoming notes ==\n");
    {
        struct T { int in, out; };
        for (T t : { T { 61, 60 }, T { 66, 65 }, T { 70, 69 }, T { 64, 64 } })
        {
            Params p = silent(); p.pnLevel = 1.0f; p.snap = true; p.root = 0; p.scale = 0;   // C major
            Rig r (48000.0, p); r.e.noteOn (t.in, 0.8f);
            auto o = r.run (1.3);
            const double c = cents (Spectrum (mono (o, 48000, 0.05, 1.05), 48000).peakNear (hz (t.out), 70.0), hz (t.out));
            CHECK (std::abs (c) < 15.0, "C major, snap on: played %d sounds as %d (%+.1f cents)", t.in, t.out, c);
        }
        Params p = silent(); p.pnLevel = 1.0f; p.snap = false;
        Rig r (48000.0, p); r.e.noteOn (61, 0.8f); auto o = r.run (1.3);
        const double c = cents (Spectrum (mono (o, 48000, 0.05, 1.05), 48000).peakNear (hz (61), 70.0), hz (61));
        CHECK (std::abs (c) < 15.0, "snap off: 61 stays 61");
    }

    std::printf ("== Bubbles ==\n");
    {
        Params p = silent(); p.bbLevel = 1.0f; p.bbDensity = 8.0f; p.bbKey = true; p.root = 2; p.scale = 1;   // D minor
        Rig r (48000.0, p); r.e.bubbleHist.fill (0);
        auto o = r.run (30.0);
        uint32_t total = 0; bool allIn = true;
        for (int m = 0; m < 128; ++m) { total += r.e.bubbleHist[(size_t) m]; if (r.e.bubbleHist[(size_t) m] && ! inScale (m, 2, 1)) allIn = false; }
        std::printf ("      %u bubbles in 30 s (density 8/s)\n", total);
        CHECK (allIn, "free-running bubbles only use notes of D minor");
        CHECK (total > 150 && total < 650, "density roughly follows the knob (%u bubbles)", total);
        CHECK (rmsOf (mono (o, 48000, 1, 30)) > 1e-3, "bubbles are audible");

        // bubbles follow held chord
        p.bbDensity = 10.0f; p.root = 0; p.scale = 9;     // chromatic: only the held notes can explain the pitch classes
        Rig c (48000.0, p); c.e.bubbleHist.fill (0);
        c.e.noteOn (60, 0.8f); c.e.noteOn (64, 0.8f); c.e.noteOn (67, 0.8f);
        c.run (30.0);
        uint32_t tot = 0, inChord = 0;
        for (int m = 0; m < 128; ++m) { tot += c.e.bubbleHist[(size_t) m]; const int pc = m % 12; if (pc == 0 || pc == 4 || pc == 7) inChord += c.e.bubbleHist[(size_t) m]; }
        CHECK ((double) inChord / (double) tot > 0.6, "with a C major chord held, %.0f%% of bubbles are chord tones", 100.0 * inChord / tot);
    }

    std::printf ("== Granulizer ==\n");
    {
        Params p = silent(); p.grLevel = 1.0f; p.root = 7; p.scale = 2;   // G major pentatonic
        p.grPattern = true; p.grRate = 1; p.grBurst = 3; p.grPitchScatter = 3.0f; p.grOct = 0.3f;
        p.stepOn.fill (true);
        Rig r (48000.0, p); r.e.grainHist.fill (0); r.e.grainSpawns = 0;
        r.e.noteOn (55, 0.8f);      // G3
        auto o = r.run (10.0);
        bool allIn = true; for (int m = 0; m < 128; ++m) if (r.e.grainHist[(size_t) m] && ! inScale (m, 7, 2)) allIn = false;
        std::printf ("      %u grains in 10 s at 120 BPM, 1/8 steps, burst 3\n", r.e.grainSpawns);
        CHECK (allIn, "every grain pitch is inside G major pentatonic (even with scatter and octave jumps)");
        CHECK (r.e.grainSpawns > 60 && r.e.grainSpawns < 200, "pattern triggers bursts on every active step (%u grains; ~120 expected)", r.e.grainSpawns);
        CHECK (rmsOf (mono (o, 48000, 1, 10)) > 1e-3, "grains are audible");

        // pattern: only steps 0 and 8 active -> 2 triggers per 16 steps (= 2 bars at 1/8, 4 s)
        Params q = p; q.stepOn.fill (false); q.stepOn[0] = q.stepOn[8] = true; q.grBurst = 1; q.grPitchScatter = 0;
        Rig s (48000.0, q); s.e.grainSpawns = 0; s.e.noteOn (55, 0.8f); s.run (8.0);
        CHECK (s.e.grainSpawns >= 3 && s.e.grainSpawns <= 5, "two active steps of sixteen -> ~4 bursts in two 16-step cycles (%u)", s.e.grainSpawns);

        // nothing happens without held keys
        Rig n (48000.0, p); n.e.grainSpawns = 0; n.run (4.0);
        CHECK (n.e.grainSpawns == 0, "no keys held -> no grains");

        // host sync: tempo change scales the rate
        Rig fast (48000.0, p); fast.e.setTransport (false, 240.0, false, 0.0); fast.e.noteOn (55, 0.8f); fast.e.grainSpawns = 0; fast.run (5.0);
        Rig slow (48000.0, p); slow.e.setTransport (false, 120.0, false, 0.0); slow.e.noteOn (55, 0.8f); slow.e.grainSpawns = 0; slow.run (5.0);
        CHECK ((double) fast.e.grainSpawns > 1.6 * (double) slow.e.grainSpawns, "240 BPM makes ~2x the grains of 120 BPM (%u vs %u)", fast.e.grainSpawns, slow.e.grainSpawns);

        // cloud mode
        Params c = p; c.grPattern = false; c.grDensity = 20.0f;
        Rig cl (48000.0, c); cl.e.noteOn (55, 0.8f); cl.e.grainSpawns = 0; cl.run (10.0);
        CHECK (cl.e.grainSpawns > 120 && cl.e.grainSpawns < 320, "cloud mode: ~density x time grains (%u, ~200 expected)", cl.e.grainSpawns);

        // pitch: grains are the piano's pitches (not noise): spectral peak at the held note when unscattered
        Params u = silent(); u.grLevel = 1.0f; u.root = 0; u.scale = 9; u.grPattern = true; u.grPitchScatter = 0; u.grOct = 0; u.grSizeMs = 400; u.grBurst = 1;
        u.stepOn.fill (false); u.stepOn[0] = true; u.grPos = 0.0f; u.grPosScatter = 0.0f; u.grRev = 0.0f;
        Rig g (48000.0, u); g.e.noteOn (60, 0.8f); auto go = g.run (3.0);
        const double f = Spectrum (mono (go, 48000, 0.0, 1.4), 48000).peakNear (hz (60), 70.0);
        CHECK (std::abs (cents (f, hz (60))) < 20.0, "grain from C4 sounds at C4 (%+.1f cents)", cents (f, hz (60)));
    }

    std::printf ("== Pads ==\n");
    {
        Params p = silent(); p.pdLevel = 1.0f; p.pdAttack = 0.3f;
        Rig r (48000.0, p); r.e.noteOn (57, 0.8f); auto o = r.run (3.0);
        const auto m = mono (o, 48000, 1.0, 2.0);
        const Spectrum sp (m, 48000);
        const double f = sp.peakNear (hz (57), 80.0);
        CHECK (std::abs (cents (f, hz (57))) < 25.0, "pad plays A3 (%+.1f cents; detune spreads the peak)", cents (f, hz (57)));
        double l = 0, rr = 0; for (size_t i = 48000; i < 96000; ++i) { l += (double) o.L[i] * o.L[i]; rr += (double) o.R[i] * o.R[i]; }
        CHECK (std::abs (10 * std::log10 (l / rr)) < 2.0, "pad is balanced left/right");
        CHECK (rmsOf (mono (o, 48000, 0.0, 0.05)) < 0.3 * rmsOf (m), "attack: starts soft and swells");
    }

    std::printf ("== Nature layers ==\n");
    {
        auto layer = [] (auto setter, double secs = 8.0)
        {
            Params p = silent(); setter (p);
            Rig r (48000.0, p); auto o = r.run (secs);
            return mono (o, 48000, 1.0, secs);
        };
        const auto water  = layer ([] (Params& p) { p.nWater = 1.0f; });
        const auto rain   = layer ([] (Params& p) { p.nRain = 1.0f; });
        const auto wind   = layer ([] (Params& p) { p.nWind = 1.0f; });
        const auto birds  = layer ([] (Params& p) { p.nBirds = 1.0f; }, 20.0);
        const auto crick  = layer ([] (Params& p) { p.nCrickets = 1.0f; });
        for (auto* v : { &water, &rain, &wind, &birds, &crick }) { bool fin = true; for (float x : *v) if (! std::isfinite (x)) fin = false; CHECK (fin, "finite output"); }
        const double cw = Spectrum (loudest (water, 48000, 1.3), 48000).centroid(), cr = Spectrum (loudest (rain, 48000, 1.3), 48000).centroid(),
                     cn = Spectrum (loudest (wind, 48000, 1.3), 48000).centroid(), cb = Spectrum (loudest (birds, 48000, 1.3), 48000).centroid(),
                     cc = Spectrum (loudest (crick, 48000, 1.3), 48000).centroid();
        std::printf ("      level @1.0 (dBFS rms): water %.1f  rain %.1f  wind %.1f  birds %.1f  crickets %.1f\n",
                     db (rmsOf (water)), db (rmsOf (rain)), db (rmsOf (wind)), db (rmsOf (birds)), db (rmsOf (crick)));
        std::printf ("      spectral centroid (Hz): water %.0f  rain %.0f  wind %.0f  birds %.0f  crickets %.0f\n", cw, cr, cn, cb, cc);
        CHECK (cn < 1500, "wind is low and airy (centroid %.0f Hz)", cn);
        CHECK (cr > 3000, "rain is bright (centroid %.0f Hz)", cr);
        CHECK (cw > 400 && cw < 4000, "water sits in the mids (centroid %.0f Hz)", cw);
        CHECK (cb > 2000, "birds are high (centroid %.0f Hz)", cb);
        CHECK (cc > 3800 && cc < 6000, "crickets around 4-5 kHz (centroid %.0f Hz)", cc);
        for (auto* v : { &water, &rain, &wind, &birds, &crick })
            CHECK (db (rmsOf (*v)) > -50.0 && db (rmsOf (*v)) < -8.0, "layer level is sensible (%.1f dBFS)", db (rmsOf (*v)));

        // tuned wind follows the held note
        Params p = silent(); p.nWindTone = 1.0f;
        Rig r (48000.0, p); r.e.noteOn (57, 0.8f); auto o = r.run (6.0);
        const Spectrum sp (mono (o, 48000, 1.0, 5.0), 48000);
        const double f = sp.peakNear (440.0, 60.0);
        CHECK (std::abs (cents (f, 440.0)) < 25.0, "tuned wind resonates at the held note's pitch (A, %+.1f cents)", cents (f, 440.0));
    }

    std::printf ("== Recorded nature ==\n");
    {
        CHECK (gNature && gNature->chirps.size() >= 8, "%d bird chirps loaded", gNature ? (int) gNature->chirps.size() : 0);
        bool loaded = true, seamOk = true;
        for (int k = 0; k < NatureBank::kBeds; ++k)
        {
            const auto& b = *gNature->beds[(size_t) k];
            const int len = b.data.getNumSamples();
            loaded = loaded && len > (int) (15.0 * b.sr);
            // click check at the loop seam: the jump from the last to the first sample vs the typical sample-to-sample step
            double worst = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = b.data.getReadPointer (ch);
                double sum = 0; for (int i = 1; i < len; ++i) sum += std::abs (d[i] - d[i - 1]);
                const double typ = sum / (len - 1), jump = std::abs (d[0] - d[len - 1]);
                worst = std::max (worst, jump / std::max (typ, 1e-9));
            }
            std::printf ("      bed %d: %.1f s, seam jump = %.1fx a typical sample step\n", k, len / b.sr, worst);
            seamOk = seamOk && worst < 8.0;
        }
        CHECK (loaded, "all 4 loops are longer than 15 s");
        CHECK (seamOk, "loops are seamless (no click at the wrap point)");
    }

    std::printf ("== Scenes: silent until a key is played ==\n");
    {
        Params p;                                   // the real default patch, with the scene fully up
        p.bdLevel = 1.0f; p.chLevel = 1.0f; p.chRate = 1.5f; p.bbLevel = 1.0f; p.nWindTone = 1.0f;
        p.pnLevel = p.pdLevel = p.grLevel = 0.0f;       // judge the ambience on its own (piano and pads are note-triggered)
        Rig r (48000.0, p);
        auto idle = r.run (4.0);
        const double idleDb = db (rmsOf (mono (idle, 48000.0, 0.5, 4.0)));
        CHECK (idleDb < -90.0, "nothing plays before a key is pressed (%.0f dBFS)", idleDb);

        r.e.noteOn (60, 0.8f); r.e.noteOn (64, 0.8f); r.e.noteOn (67, 0.8f);
        auto on = r.run (8.0);
        const double earlyDb = db (rmsOf (mono (on, 48000.0, 0.0, 0.3))), lateDb = db (rmsOf (mono (on, 48000.0, 5.0, 8.0)));
        std::printf ("      first 0.3 s %.1f dBFS, after fade-in %.1f dBFS\n", earlyDb, lateDb);
        CHECK (lateDb > -35.0, "the scene fills in while keys are held (%.1f dBFS)", lateDb);
        CHECK (earlyDb < lateDb - 6.0, "ambience fades in gently rather than switching on");

        r.e.allNotesOff();
        auto off = r.run (20.0);
        const double tailDb = db (rmsOf (mono (off, 48000.0, 17.0, 20.0)));
        CHECK (tailDb < -70.0, "everything fades out after the keys are released (%.0f dBFS)", tailDb);

        Params q = p; q.ambGate = false;
        Rig r2 (48000.0, q);
        auto always = r2.run (6.0);
        CHECK (db (rmsOf (mono (always, 48000.0, 2.0, 6.0))) > -45.0, "with the gate switched off the ambience runs by itself");
    }

    std::printf ("== Scenes: each recording and the key-to-scene mapping ==\n");
    {
        for (int k = 0; k < 4; ++k)
        {
            Params p = silent(); p.bdLevel = 1.0f; p.bdGain = { { 0, 0, 0, 0 } }; p.bdGain[(size_t) k] = 1.0f;
            Rig r (48000.0, p);
            auto o = r.run (6.0);
            const double d = db (rmsOf (mono (o, 48000.0, 1.0, 6.0)));
            const bool fin = std::isfinite (d);
            CHECK (fin && d > -42.0 && d < -6.0, "recording %d is audible and sane (%.1f dBFS)", k, d);
        }
        auto lvl = [] (int k, int note)
        {
            Params p = silent(); p.bdLevel = 1.0f; p.bdKeys = true; p.ambGate = true; p.ambIn = 0.1f;
            p.bdGain = { { 0, 0, 0, 0 } }; p.bdGain[(size_t) k] = 1.0f;
            Rig r (48000.0, p);
            r.e.noteOn (note, 0.8f);
            auto o = r.run (4.0);
            return db (rmsOf (mono (o, 48000.0, 1.5, 4.0)));
        };
        const double sLow = lvl (0, 36), sHigh = lvl (0, 96), dLow = lvl (3, 36), dHigh = lvl (3, 96);
        std::printf("      stream: low key %.0f dB, high key %.0f dB | dawn: low key %.0f dB, high key %.0f dB\n", sLow, sHigh, dLow, dHigh);
        CHECK (sLow > sHigh + 25.0 && dHigh > dLow + 25.0, "low keys call the stream, high keys the dawn birds");
    }

    std::printf ("== Recorded bird chirps ==\n");
    {
        Params p = silent(); p.chLevel = 1.0f; p.chRate = 2.0f; p.ambGate = true; p.ambIn = 0.1f;
        Rig r (48000.0, p);
        r.e.noteOn (60, 0.8f);
        auto o = r.run (12.0);
        auto m = mono (o, 48000.0, 1.0, 12.0);
        Spectrum sp (loudest (m, 48000.0, 1.4), 48000.0);
        const double rms = db (rmsOf (m));
        std::printf ("      rms %.1f dBFS, spectral centroid of the busiest stretch %.0f Hz\n", rms, sp.centroid());
        CHECK (rms > -55.0 && rms < -12.0, "chirps are audible at a sensible level (%.1f dBFS)", rms);
        CHECK (sp.centroid() > 1800.0 && sp.centroid() < 9000.0, "chirps sound like birds (bright, %.0f Hz centroid)", sp.centroid());

        // rate control: more chirps per second -> more energy
        Params q = p; q.chRate = 0.1f;
        Rig r2 (48000.0, q); r2.e.noteOn (60, 0.8f);
        auto o2 = r2.run (12.0);
        CHECK (db (rmsOf (mono (o2, 48000.0, 1.0, 12.0))) < rms - 3.0, "chirp rate control thins them out");
    }

    std::printf ("== Piano performance: chord arp / random in key ==\n");
    {
        auto run = [] (Params p, std::initializer_list<int> notes, double secs, Rig::Out* keep = nullptr)
        {
            Rig r (48000.0, p);
            for (int n : notes) r.e.noteOn (n, 0.8f);
            auto o = r.run (secs);
            std::vector<int> seq;
            const uint32_t cnt = r.e.arpCount;
            for (uint32_t i = cnt > 64 ? cnt - 64 : 0; i < cnt; ++i) seq.push_back (r.e.arpLog[i & 63u]);
            if (keep) *keep = o;
            return seq;
        };
        Params p = silent(); p.pnLevel = 1.0f; p.arMode = 1; p.arRest = 0.0f; p.arOct = 1; p.arRate = 1; p.arPattern = 0;
        auto up = run (p, { 60, 64, 67 }, 4.0);
        bool upOk = up.size() >= 14;
        for (size_t i = 0; i < up.size(); ++i) upOk = upOk && up[i] == (int[]) { 60, 64, 67 }[i % 3];
        CHECK (upOk, "chord arp (up) plays C-E-G-C-E-G... (%zu notes in 4 s at 1/8, 120 BPM)", up.size());
        CHECK (up.size() >= 14 && up.size() <= 17, "arp speed follows the tempo (%zu steps, expected 16)", up.size());

        p.arPattern = 1; auto dn = run (p, { 60, 64, 67 }, 2.0);
        CHECK (dn.size() >= 4 && dn[0] == 67 && dn[1] == 64 && dn[2] == 60 && dn[3] == 67, "down pattern runs G-E-C-G");

        p.arPattern = 2; auto ud = run (p, { 60, 64, 67 }, 3.0);
        const int expUd[] = { 60, 64, 67, 64, 60, 64, 67, 64 };
        bool udOk = ud.size() >= 8; for (int i = 0; i < 8 && udOk; ++i) udOk = ud[(size_t) i] == expUd[i];
        CHECK (udOk, "up-down pattern bounces C-E-G-E-C-E-G-E");

        p.arOct = 2; p.arPattern = 0; auto two = run (p, { 60, 64, 67 }, 3.0);
        bool hasHigh = false; for (int n : two) hasHigh = hasHigh || n >= 72;
        CHECK (hasHigh, "2 octaves reaches the octave above");

        p.arRate = 2; auto fast = run (p, { 60, 64, 67 }, 2.0);
        CHECK (fast.size() >= 14, "1/16 is twice as fast (%zu steps in 2 s)", fast.size());

        Params pr = p; pr.arRate = 1; pr.arRest = 0.5f; auto rests = run (pr, { 60, 64, 67 }, 8.0);
        CHECK (rests.size() > 8 && rests.size() < 28, "rests thin the arp out (%zu of 32 steps played)", rests.size());

        // sound: a piano note every step, zero attack, then it releases by itself
        Rig::Out keep; Params pa = silent(); pa.pnLevel = 1.0f; pa.arMode = 1; pa.arRest = 0.0f; pa.arOct = 1; pa.arPattern = 0;
        run (pa, { 60, 64, 67 }, 3.0, &keep);
        auto m = mono (keep, 48000.0, 0.0, 3.0);
        CHECK (db (rmsOf (m)) > -40.0, "the arp is audible (%.1f dBFS)", db (rmsOf (m)));
        Rig rr (48000.0, pa); rr.e.noteOn (60, 0.8f); rr.e.noteOn (64, 0.8f); rr.e.noteOn (67, 0.8f); rr.run (1.0);
        rr.e.allNotesOff(); auto tail = rr.run (6.0);
        CHECK (db (rmsOf (mono (tail, 48000.0, 4.5, 6.0))) < -70.0, "arp ends when the keys are released");

        // random in key
        Params rk = silent(); rk.pnLevel = 1.0f; rk.arMode = 2; rk.arRest = 0.0f; rk.arOct = 2; rk.root = 2; rk.scale = 1; rk.arRate = 2;   // D minor
        auto rnd = run (rk, { 50 }, 12.0);
        bool inKey = rnd.size() >= 40; std::set<int> distinct;
        for (int n : rnd) { inKey = inKey && inScale (n, 2, 1); distinct.insert (n); }
        CHECK (inKey, "random mode only plays notes of D minor (%zu notes)", rnd.size());
        CHECK (distinct.size() >= 5, "random mode wanders over %zu different pitches", distinct.size());
        bool inRange = true; for (int n : rnd) inRange = inRange && n >= 50 && n <= 50 + 28;
        CHECK (inRange, "random notes stay near the held key");

        // direct mode is unchanged
        Params dm = silent(); dm.pnLevel = 1.0f; dm.arMode = 0;
        Rig rd (48000.0, dm); rd.e.noteOn (60, 0.8f); auto od = rd.run (1.0);
        CHECK (db (rmsOf (mono (od, 48000.0, 0.0, 1.0))) > -40.0 && rd.e.arpCount == 0, "Direct mode still plays the key itself");
    }

    std::printf ("== Built-in instruments (your recordings) ==\n");
    {
        CHECK (gInstIndex.size() == 7, "7 instruments in the pack (%zu)", gInstIndex.size());
        const char* want[] = { "Saws", "Lead", "Lead 2", "Organ", "Organ 2", "Strings", "Atmos" };
        bool names = gInstIndex.size() == 7;
        for (size_t i = 0; i < gInstIndex.size() && i < 7; ++i) names = names && gInstIndex[i].name == want[i];
        CHECK (names, "names: Saws, Lead, Lead 2, Organ, Organ 2, Strings, Atmos");

        for (const auto& ii : gInstIndex)
        {
            auto bank = decodeInst (gInstPak.getData(), gInstPak.getSize(), ii);
            int looped = 0; bool stereo = true;
            for (auto& z : bank->samples) { looped += z.looped() ? 1 : 0; stereo = stereo && z.data.getMagnitude (0, 0, z.data.getNumSamples()) > 0.05f; }
            const bool sustained = looped * 2 > (int) bank->samples.size();

            // hold a note for 8 s on the piano layer: it must keep sounding (looped instruments) and must not click or dip at the loop points
            double worstDip = 0, level = -200;
            for (int note : { 48, 60, 72 })
            {
                Params p = silent(); p.pnLevel = 1.0f; p.arMode = 0;
                Rig r (48000.0, p); r.e.setPiano (bank);
                r.e.noteOn (note, 0.8f);
                auto o = r.run (8.0);
                auto m = mono (o, 48000.0, 3.0, 8.0);
                if (sustained)
                {
                    level = std::max (level, db (rmsOf (m)));
                    // 20 ms RMS series vs its own 0.8 s neighbourhood: a click or a dropout at a loop point shows up as a spike/dip
                    const size_t w = 960; std::vector<double> rr;
                    for (size_t i = 0; i + w <= m.size(); i += w) { double e = 0; for (size_t k = 0; k < w; ++k) e += (double) m[i + k] * m[i + k]; rr.push_back (std::sqrt (e / w) + 1e-9); }
                    for (size_t i = 20; i + 20 < rr.size(); ++i)
                    {
                        double mean = 0; for (size_t k = i - 20; k <= i + 20; ++k) mean += rr[k]; mean /= 41.0;
                        worstDip = std::max (worstDip, std::abs (db (rr[i]) - db (mean)));
                    }
                }
            }
            std::printf ("      %-8s %2zu zones, %2d looped%s", ii.name.c_str(), bank->samples.size(), looped, sustained ? "" : " (plucked: plays once)");
            if (sustained) std::printf (", held 8 s: %.1f dBFS, worst level wobble %.1f dB\n", level, worstDip); else std::printf ("\n");
            CHECK (! bank->samples.empty() && stereo, "%s decodes", ii.name.c_str());
            if (sustained)
            {
                CHECK (level > -45.0, "%s keeps sounding while the key is held (%.1f dBFS after 3-8 s)", ii.name.c_str(), level);
                CHECK (worstDip < 8.0, "%s loops without dropouts (worst level wobble %.1f dB, no deep dips)", ii.name.c_str(), worstDip);
            }
            else
            {
                Params p = silent(); p.pnLevel = 1.0f; p.arMode = 0;
                Rig r (48000.0, p); r.e.setPiano (bank); r.e.noteOn (60, 0.8f);
                auto o = r.run (8.0);
                CHECK (db (rmsOf (mono (o, 48000.0, 0.0, 1.0))) > -40.0 && db (rmsOf (mono (o, 48000.0, 6.0, 8.0))) < -60.0, "%s plays once and dies away", ii.name.c_str());
            }

            // as the pad layer: fades in with the attack time, holds, releases with the release time
            {
                Params p = silent(); p.pdLevel = 1.0f; p.pdAttack = 1.0f; p.pdRel = 1.0f;
                Rig r (48000.0, p); r.e.setPadBank (bank);
                r.e.noteOn (60, 0.8f); r.e.noteOn (64, 0.8f); r.e.noteOn (67, 0.8f);
                auto o = r.run (6.0);
                const double a0 = db (rmsOf (mono (o, 48000.0, 0.0, 0.15))), a1 = db (rmsOf (mono (o, 48000.0, 3.0, 6.0)));
                r.e.allNotesOff();
                auto t = r.run (6.0);
                const double tail = db (rmsOf (mono (t, 48000.0, 4.0, 6.0)));
                std::printf ("         as pad: first 0.15 s %.0f dB, held %.1f dBFS, 4-6 s after release %.0f dB\n", a0, a1, tail);
                if (sustained || ii.name == "Atmos") CHECK (a0 < a1 - 6.0, "%s as a pad fades in", ii.name.c_str());
                CHECK (tail < -60.0, "%s as a pad releases and goes quiet", ii.name.c_str());
                if (sustained || ii.name == "Atmos") CHECK (a1 > -40.0, "%s as a pad sustains (%.1f dBFS)", ii.name.c_str(), a1);
            }
        }
    }

    std::printf ("== Importing dropped sounds ==\n");
    {
        const double SR = 44100.0;
        auto makeBuf = [&] (double secs) { Decoded d; d.sr = SR; d.data.setSize (2, (int) (secs * SR)); d.data.clear(); return d; };
        auto sawNote = [&] (Decoded& d, double t0, double midi, double sus, double rel)
        {
            const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            const int a = (int) (t0 * SR), len = (int) ((0.02 + sus + rel) * SR);
            for (int i = 0; i < len && a + i < d.length(); ++i)
            {
                const double t = (double) i / SR;
                double env = t < 0.02 ? t / 0.02 : (t < 0.02 + sus ? 1.0 : std::exp (-(t - 0.02 - sus) * 9.0));
                double v = 0; for (int h = 1; h <= 8; ++h) v += std::sin (6.283185307 * f * h * t) / h;
                const float x = (float) (0.25 * v * env);
                d.data.setSample (0, a + i, x); d.data.setSample (1, a + i, x);
            }
        };

        // (a) a recording with one note per slot, like the piano file
        {
            auto d = makeBuf (2.6 * 12 + 1.0);
            for (int i = 0; i < 12; ++i) sawNote (d, 0.05 + i * 2.6, 48 + i, 1.7, 0.4);
            juce::String info;
            auto bank = importInstrument (d, info);
            bool roots = bank->samples.size() == 12; double worst = 0; int looped = 0;
            for (size_t i = 0; i < bank->samples.size(); ++i)
            {
                roots = roots && bank->samples[i].root == 48 + (int) i;
                worst = std::max (worst, std::abs (bank->samples[i].fine));
                looped += bank->samples[i].looped() ? 1 : 0;
            }
            std::printf ("      note-per-slot file: %zu zones (%s), worst pitch error %.0f cents, %d looped\n", bank->samples.size(), info.toRawUTF8(), worst * 100.0, looped);
            CHECK (roots, "finds all 12 notes and their pitches (C3..B3)");
            CHECK (worst < 0.15, "pitch measured to within 15 cents");
            CHECK (looped >= 10, "sustain loops are found for the notes");

            Params p = silent(); p.pnLevel = 1.0f; p.arMode = 0;
            Rig r (48000.0, p); r.e.setPiano (bank); r.e.noteOn (55, 0.8f);
            auto o = r.run (5.0);
            Spectrum sp (mono (o, 48000.0, 1.0, 2.5), 48000.0);
            CHECK (std::abs (cents (sp.peakNear (hz (55)), hz (55))) < 15.0, "imported sound plays at the right pitch (%.1f cents)", cents (sp.peakNear (hz (55)), hz (55)));
            CHECK (db (rmsOf (mono (o, 48000.0, 3.0, 5.0))) > -40.0, "and sustains while the key is held");
            Rig r2 (48000.0, p); r2.e.setPiano (bank); r2.e.noteOn (53, 0.8f);               // between zones: repitched
            auto o2 = r2.run (2.5);
            Spectrum sp2 (mono (o2, 48000.0, 0.5, 2.0), 48000.0);
            CHECK (std::abs (cents (sp2.peakNear (hz (53)), hz (53))) < 15.0, "notes between the recorded ones are tuned correctly");
        }
        // (b) one sustained note
        {
            auto d = makeBuf (7.0); sawNote (d, 0.0, 50.0, 6.5, 0.4);
            juce::String info; auto bank = importInstrument (d, info);
            std::printf ("      single note: %s\n", info.toRawUTF8());
            CHECK (bank->samples.size() == 1 && bank->samples[0].root == 50 && std::abs (bank->samples[0].fine) < 0.15, "single note: pitch D3 found");
            CHECK (bank->samples.size() == 1 && bank->samples[0].looped(), "long single sounds get a sustain loop");
        }
        // (c) noise texture with no pitch
        {
            auto d = makeBuf (6.0); std::mt19937 g (5); std::uniform_real_distribution<float> u (-0.3f, 0.3f);
            for (int i = 0; i < d.length(); ++i) { const float x = u (g); d.data.setSample (0, i, x); d.data.setSample (1, i, x * 0.8f); }
            juce::String info; auto bank = importInstrument (d, info);
            CHECK (bank->samples.size() == 1 && bank->samples[0].root == 60, "unpitched sound is played from C4 (%s)", info.toRawUTF8());
        }
        // (d) nature loop
        {
            auto d = makeBuf (45.0); std::mt19937 g (9); std::normal_distribution<float> nd (0.0f, 0.1f);
            float lp = 0; for (int i = 0; i < d.length(); ++i) { lp += 0.2f * (nd (g) - lp); const float x = lp * (1.0f + 0.3f * std::sin (i * 0.00002f)); d.data.setSample (0, i, x); d.data.setSample (1, i, x * 0.9f); }
            LoopSample bed; const bool ok = importBed (d, bed);
            const int len = bed.data.getNumSamples();
            const float* x = bed.data.getReadPointer (0);
            double sum = 0; for (int i = 1; i < len; ++i) sum += std::abs (x[i] - x[i - 1]);
            const double jump = std::abs (x[0] - x[len - 1]) / (sum / (len - 1));
            std::printf ("      bed: %.1f s, seam jump %.1fx a typical step, level %.1f dBFS\n", len / bed.sr, jump, imp::rmsDb (bed.data, 0, len));
            CHECK (ok && len > (int) (20 * SR) && jump < 8.0, "dropped ambience becomes a seamless loop");
            CHECK (std::abs (imp::rmsDb (bed.data, 0, len) + 22.0) < 1.5, "and is levelled like the built-in loops");
        }
        // (e) bird calls
        {
            auto d = makeBuf (22.0); std::mt19937 g (3); std::normal_distribution<float> nd (0.0f, 0.004f);
            for (int i = 0; i < d.length(); ++i) { const float x = nd (g); d.data.setSample (0, i, x); d.data.setSample (1, i, x); }
            for (int c = 0; c < 6; ++c)
            {
                const int a = (int) ((2.0 + 3.2 * c) * SR);
                for (int i = 0; i < (int) (0.6 * SR); ++i)
                {
                    const double t = (double) i / SR; const float e = (float) std::sin (3.14159 * t / 0.6);
                    const float x = 0.3f * e * (float) std::sin (6.283185 * (3000.0 + 800.0 * std::sin (t * 40.0)) * t);
                    d.data.addSample (0, a + i, x); d.data.addSample (1, a + i, x);
                }
            }
            std::vector<std::shared_ptr<LoopSample>> out; const bool ok = importChirps (d, out);
            std::printf ("      chirps cut from a 22 s file: %zu\n", out.size());
            CHECK (ok && out.size() == 6, "six bird calls are found and cut out (%zu)", out.size());
            bool lengths = true; for (auto& c : out) lengths = lengths && c->data.getNumSamples() > (int) (0.5 * SR) && c->data.getNumSamples() < (int) (1.6 * SR);
            CHECK (lengths, "each cut is about one call long");
        }
        // (f) real recordings, if a folder is given: BOT_REAL_AUDIO=/path (wav files)
        if (const char* dir = std::getenv ("BOT_REAL_AUDIO"))
        {
            for (auto& f : juce::File (dir).findChildFiles (juce::File::findFiles, false, "*.wav"))
            {
                Decoded d; if (! decodeFile (f, d)) continue;
                juce::String info; auto bank = importInstrument (d, info);
                int lo = 999, hi = 0, lp = 0; for (auto& z : bank->samples) { lo = std::min (lo, z.root); hi = std::max (hi, z.root); lp += z.looped(); }
                std::printf ("      REAL %-45s -> %zu zones, notes %d..%d, %d looped (%s)\n", f.getFileName().toRawUTF8(), bank->samples.size(), lo, hi, lp, info.toRawUTF8());
            }
        }
    }

    std::printf ("== Seeded melodies (chord-aware) ==\n");
    {
        // the phrase library itself
        bool strongOk = true, passOk = true, rangeOk = true, leapOk = true, detOk = true;
        std::set<std::string> distinct; int chordTones = 0, notes = 0; std::map<int, int> strongPlayed;
        for (int seed = 1; seed <= 99; ++seed)
        {
            std::string sig;
            int prev = -1;
            for (int pos = 0; pos < 16; ++pos)
            {
                const auto a = melodyToken (seed, pos, 0.0f, 3, 6), b = melodyToken (seed, pos, 0.0f, 3, 6);
                detOk = detOk && a.rest == b.rest && a.idx == b.idx && a.pass == b.pass;
                if (pos % 4 == 0 && ! a.rest) { strongOk = strongOk && a.pass == 0; ++strongPlayed[seed]; }   // a note on a strong beat is always a chord tone
                if (a.pass != 0) { passOk = passOk && pos % 4 != 0; }                       // passing notes only off the strong beats
                if (! a.rest)
                {
                    rangeOk = rangeOk && a.idx >= 0 && a.idx < 6;
                    if (prev >= 0 && a.pass == 0) leapOk = leapOk && std::abs (a.idx - prev) <= 4;
                    if (a.pass == 0) { prev = a.idx; ++chordTones; }
                    ++notes;
                }
                sig += a.rest ? "r" : std::to_string (a.idx) + (a.pass > 0 ? "p" : a.pass < 0 ? "m" : "") ;
                sig += ",";
            }
            distinct.insert (sig);
        }
        bool beatsOk = true; for (auto& kv : strongPlayed) beatsOk = beatsOk && kv.second >= 2;
        CHECK (strongOk, "notes on strong beats (steps 1, 5, 9, 13) are always chord tones");
        CHECK (beatsOk, "every phrase plays at least 2 of its 4 strong beats");
        CHECK (passOk, "passing notes only sit off the strong beats");
        CHECK (rangeOk && leapOk, "notes stay inside the chord range with no wild leaps");
        CHECK (detOk, "a seed always gives the same phrase");
        CHECK (distinct.size() >= 85, "99 seeds give %zu different phrases", distinct.size());
        CHECK (chordTones * 100 >= notes * 80, "%d%% of the notes are chord tones, the rest are passing notes", chordTones * 100 / std::max (1, notes));

        // through the engine: the notes follow the chord that is held
        auto play = [] (std::initializer_list<int> keys, int seed, int root, int scale, double secs, int rate = 2)
        {
            Params p = silent(); p.pnLevel = 1.0f; p.arMode = 2; p.arRest = 0.0f; p.arOct = 2; p.arSeed = seed; p.root = root; p.scale = scale; p.arRate = rate;
            Rig r (48000.0, p);
            for (int k : keys) r.e.noteOn (k, 0.8f);
            r.run (secs);
            struct Out { std::vector<int> notes, pos; };
            Out o; const uint32_t cnt = r.e.arpCount;
            for (uint32_t i = cnt > 64 ? cnt - 64 : 0; i < cnt; ++i) { o.notes.push_back (r.e.arpLog[i & 63u]); o.pos.push_back (r.e.arpPosLog[i & 63u]); }
            return o;
        };
        auto pcIn = [] (int note, std::initializer_list<int> pcs) { for (int pc : pcs) if (((note % 12) + 12) % 12 == pc) return true; return false; };

        bool allOk = true, strongTones = true; size_t total = 0;
        for (int seed : { 1, 7, 12, 30, 55, 80, 99 })
        {
            auto o = play ({ 60, 64, 67 }, seed, 0, 0, 5.0);                      // C major chord, key C major
            total += o.notes.size();
            for (size_t i = 0; i < o.notes.size(); ++i)
            {
                allOk = allOk && inScale (o.notes[i], 0, 0);
                if (o.pos[i] % 4 == 0) strongTones = strongTones && pcIn (o.notes[i], { 0, 4, 7 });
            }
        }
        CHECK (allOk && total > 100, "C major chord: every note stays in C major (%zu notes over 7 seeds)", total);
        CHECK (strongTones, "C major chord: notes on strong beats are C, E or G");

        // a chord that is NOT the first triad of the key: A minor in C major, then F major
        auto am = play ({ 57, 60, 64 }, 12, 0, 0, 5.0);
        bool amOk = true; for (size_t i = 0; i < am.notes.size(); ++i) if (am.pos[i] % 4 == 0) amOk = amOk && pcIn (am.notes[i], { 9, 0, 4 });
        CHECK (amOk && ! am.notes.empty(), "A minor chord: strong beats play A, C or E");
        auto fm = play ({ 53, 57, 60 }, 30, 0, 0, 5.0);
        bool fmOk = true; for (size_t i = 0; i < fm.notes.size(); ++i) if (fm.pos[i] % 4 == 0) fmOk = fmOk && pcIn (fm.notes[i], { 5, 9, 0 });
        CHECK (fmOk && ! fm.notes.empty(), "F major chord: strong beats play F, A or C");

        // a 7th chord is detected as four notes
        auto c7 = play ({ 48, 52, 55, 59 }, 4, 0, 0, 6.0);       // Cmaj7
        std::set<int> pcs; for (int n : c7.notes) pcs.insert (((n % 12) + 12) % 12);
        bool sevenSeen = pcs.count (11) > 0;
        CHECK (sevenSeen, "a 4-note chord uses all four chord tones (B of Cmaj7 is played)");

        // one held note: the arp builds the triad of the key on it (D minor: D F A)
        auto one = play ({ 50 }, 1, 2, 1, 6.0);
        bool oneOk = ! one.notes.empty(); bool strong1 = true;
        for (size_t i = 0; i < one.notes.size(); ++i)
        {
            oneOk = oneOk && inScale (one.notes[i], 2, 1);
            if (one.pos[i] % 4 == 0) strong1 = strong1 && pcIn (one.notes[i], { 2, 5, 9 });
        }
        CHECK (oneOk && strong1, "a single held D in D minor plays a D minor phrase (D, F, A on strong beats)");

        // the phrase repeats every 16 steps and different seeds sound different
        auto s1 = play ({ 60, 64, 67 }, 5, 0, 0, 9.0), s1b = play ({ 60, 64, 67 }, 5, 0, 0, 9.0), s2 = play ({ 60, 64, 67 }, 6, 0, 0, 9.0);
        CHECK (s1.notes == s1b.notes && ! s1.notes.empty(), "same seed + same chord = identical notes");
        CHECK (s1.notes != s2.notes, "another seed plays different notes");
        // 16-step loop: the notes of one pass equal the notes of the next pass
        bool loops = s1.notes.size() >= 24;
        if (loops)
        {
            const size_t n = s1.notes.size();
            std::map<int, std::vector<int>> byPos;
            for (size_t i = 0; i < n; ++i) byPos[s1.pos[i]].push_back (s1.notes[i]);
            for (auto& kv : byPos) for (int v : kv.second) loops = loops && v == kv.second[0];
        }
        CHECK (loops, "the phrase repeats exactly every 16 steps");

        // changing chord mid-phrase follows the new chord
        {
            Params p = silent(); p.pnLevel = 1.0f; p.arMode = 2; p.arRest = 0.0f; p.arOct = 2; p.arSeed = 3; p.arRate = 2;
            Rig r (48000.0, p);
            for (int k : { 60, 64, 67 }) r.e.noteOn (k, 0.8f);
            r.run (3.0);
            for (int k : { 60, 64, 67 }) r.e.noteOff (k);
            for (int k : { 55, 59, 62 }) r.e.noteOn (k, 0.8f);                      // G major
            const uint32_t before = r.e.arpCount;
            r.run (4.0);
            bool follows = r.e.arpCount > before + 8;
            for (uint32_t i = before + 2; i < r.e.arpCount && follows; ++i)
                if (r.e.arpPosLog[i & 63u] % 4 == 0) follows = follows && pcIn (r.e.arpLog[i & 63u], { 7, 11, 2 });
            CHECK (follows, "after the chord changes to G major the strong beats play G, B, D");
        }

        // rests: more rests = fewer notes, strong beats keep playing
        {
            Params p = silent(); p.pnLevel = 1.0f; p.arMode = 2; p.arOct = 2; p.arSeed = 20; p.arRate = 2;
            auto count = [&] (float rest) { p.arRest = rest; Rig r (48000.0, p); for (int k : { 60, 64, 67 }) r.e.noteOn (k, 0.8f); r.run (8.0); return r.e.arpCount; };
            const auto a = count (0.0f), b = count (0.7f);
            CHECK (b < a && b * 10 > a * 3, "Rests thin the melody out (%u -> %u notes in 8 s)", a, b);
        }

        // the sound
        {
            Params p = silent(); p.pnLevel = 1.0f; p.arMode = 2; p.arRate = 1; p.arSeed = 1;
            Rig r (48000.0, p); for (int k : { 60, 64, 67 }) r.e.noteOn (k, 0.8f);
            auto o = r.run (6.0);
            CHECK (db (rmsOf (mono (o, 48000.0, 1.0, 6.0))) > -40.0, "the melody is audible (%.1f dBFS)", db (rmsOf (mono (o, 48000.0, 1.0, 6.0))));
        }
    }

    std::printf ("== Smear: granulizer on the whole plugin ==\n");
    {
        // a full default scene to feed it
        Params base;
        base.bdLevel = 0.8f; base.chLevel = 0.6f; base.bbLevel = 0.4f; base.arMode = 1;
        auto scene = [&] (Params p, double secs, bool release, double* fedMs = nullptr)
        {
            Rig r (48000.0, p);
            for (int k : { 50, 57, 62, 65 }) r.e.noteOn (k, 0.8f);
            (void) release; (void) fedMs;
            return r.run (secs);
        };
        const auto dry = scene (base, 8.0, false);
        const double dryDb = db (rmsOf (mono (dry, 48000.0, 3.0, 8.0)));

        Params off = base; off.gaOn = false; off.gaMix = 1.0f;
        const auto offOut = scene (off, 8.0, false);
        double maxDiff = 0; for (size_t i = 0; i < dry.L.size() && i < offOut.L.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (dry.L[i] - offOut.L[i]));
        CHECK (maxDiff < 1.0e-6, "with Smear switched off the sound is untouched");

        Params zero = base; zero.gaOn = true; zero.gaMix = 0.0f;
        const auto zeroOut = scene (zero, 8.0, false);
        double md2 = 0; for (size_t i = 0; i < dry.L.size() && i < zeroOut.L.size(); ++i) md2 = std::max (md2, (double) std::abs (dry.L[i] - zeroOut.L[i]));
        CHECK (md2 < 1.0e-5, "Smear mix at 0%% leaves the sound untouched");

        for (float mix : { 0.5f, 1.0f })
        {
            Params p = base; p.gaOn = true; p.gaMix = mix;
            const auto o = scene (p, 8.0, false);
            const double d = db (rmsOf (mono (o, 48000.0, 3.0, 8.0)));
            std::printf ("      smear mix %.0f%%: %.1f dBFS (dry %.1f dBFS)\n", mix * 100.0f, d, dryDb);
            CHECK (std::abs (d - dryDb) < 5.0, "smeared level stays close to the dry level at %.0f%% mix", mix * 100.0f);
            bool fin = true, bounded = true; for (float v : o.L) { fin = fin && std::isfinite (v); bounded = bounded && std::abs (v) <= 1.0f; }
            CHECK (fin && bounded, "smeared output is finite and never clips");
        }

        // it really smears: a short plucked note keeps sounding after it has ended
        {
            Params p = silent(); p.pnLevel = 1.0f; p.pnRel = 0.05f; p.arMode = 0; p.ambGate = false;
            p.gaOn = true; p.gaMix = 1.0f; p.gaSizeMs = 300.0f; p.gaDensity = 30.0f; p.gaScatter = 0.8f;
            auto run = [&] (bool on)
            {
                p.gaOn = on;
                Rig r (48000.0, p);
                r.e.noteOn (60, 1.0f); auto a = r.run (0.25); r.e.noteOff (60); auto b = r.run (5.0);
                (void) a; return b;
            };
            const auto sm = run (true), plain = run (false);
            const double tailSm = db (rmsOf (mono (sm, 48000.0, 1.0, 2.2))), tailPlain = db (rmsOf (mono (plain, 48000.0, 1.0, 2.2)));
            std::printf ("      1-2 s after the note ended: plain %.0f dBFS, smeared %.0f dBFS\n", tailPlain, tailSm);
            CHECK (tailPlain < -75.0 && tailSm > -55.0, "a note is smeared into a cloud that outlasts it");
            const double late = db (rmsOf (mono (sm, 48000.0, 4.5, 5.0)));
            CHECK (late < tailSm - 6.0, "and the cloud dies away naturally (%.0f dBFS later)", late);
        }

        // freeze keeps the sound alive for as long as you like
        {
            Params p = silent(); p.pdLevel = 1.0f; p.arMode = 0; p.ambGate = false; p.pdRel = 0.3f;
            p.gaOn = true; p.gaMix = 1.0f; p.gaSizeMs = 250.0f; p.gaDensity = 24.0f; p.gaScatter = 0.6f;
            Rig r (48000.0, p);
            for (int k : { 50, 57, 62 }) r.e.noteOn (k, 0.8f);
            r.run (3.0);
            p.gaFreeze = true; r.e.setParams (p);
            r.run (0.2);
            r.e.allNotesOff();
            auto a = r.run (6.0), b = r.run (6.0);
            const double da = db (rmsOf (mono (a, 48000.0, 2.0, 6.0))), dbb = db (rmsOf (mono (b, 48000.0, 0.0, 6.0)));
            std::printf ("      frozen: %.1f dBFS after 2-6 s, %.1f dBFS after 6-12 s\n", da, dbb);
            CHECK (da > -45.0 && std::abs (da - dbb) < 4.0, "freeze holds the smeared sound steady after the keys are released");
            p.gaFreeze = false; r.e.setParams (p);
            auto c = r.run (12.0);
            CHECK (db (rmsOf (mono (c, 48000.0, 8.0, 12.0))) < -70.0, "unfreezing lets it fade away");
        }

        // pitch shimmer adds octave / fifth grains: more high content
        {
            Params p = silent(); p.pdLevel = 1.0f; p.arMode = 0; p.ambGate = false; p.gaOn = true; p.gaMix = 1.0f; p.gaSizeMs = 150.0f; p.gaDensity = 40.0f;
            auto cent = [&] (float pitch)
            {
                p.gaPitch = pitch; Rig r (48000.0, p);
                for (int k : { 50, 57, 62 }) r.e.noteOn (k, 0.8f);
                auto o = r.run (6.0); auto m = mono (o, 48000.0, 2.0, 6.0);
                return Spectrum (loudest (m, 48000.0, 1.4), 48000.0).centroid();
            };
            const double c0 = cent (0.0f), c1 = cent (1.0f);
            std::printf ("      spectral centroid: shimmer off %.0f Hz, on %.0f Hz\n", c0, c1);
            CHECK (c1 > c0 * 1.15, "pitch shimmer brightens the cloud");
        }

        // robustness at other sample rates and block sizes
        {
            bool ok = true;
            for (double sr : { 44100.0, 96000.0, 192000.0 })
                for (int bs : { 1, 64, 4096 })
                {
                    Params p = base; p.gaOn = true; p.gaMix = 0.8f; p.gaSizeMs = 1000.0f; p.gaDensity = 80.0f; p.gaScatter = 1.0f; p.gaPitch = 1.0f;
                    Engine e; e.prepare (sr, bs); e.setPiano (gBank); e.setNature (gNature); e.setParams (p);
                    e.noteOn (50, 0.9f); e.noteOn (57, 0.9f);
                    std::vector<float> L ((size_t) bs), R ((size_t) bs);
                    const int blocks = (int) (sr * 2.0 / bs) + 1;
                    for (int b = 0; b < blocks && ok; ++b)
                    {
                        e.setTransport (true, 120.0, true, 0.0);
                        e.render (L.data(), R.data(), bs);
                        for (int i = 0; i < bs; ++i) ok = ok && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]) && std::abs (L[(size_t) i]) <= 1.0f;
                    }
                }
            CHECK (ok, "smear stays finite and bounded at 44.1-192 kHz with blocks of 1 to 4096 and extreme settings");
        }
    }

    std::printf ("== Macro, balance and levels ==\n");
    {
        auto chord = [] (const Params& p, double secs, Rig::Out* keep = nullptr)
        {
            Rig r (48000.0, p);
            for (int n : { 48, 55, 60, 64, 67 }) r.e.noteOn (n, 0.8f);
            auto o = r.run (secs);
            const auto m = mono (o, 48000, 0.15, 3.5);          // what you hear while playing
            if (keep) *keep = o;
            return m;
        };
        Params lo, hi; lo.grow = 0.0f; hi.grow = 1.0f;
        const double rl = rmsOf (chord (lo, 6)), rh = rmsOf (chord (hi, 6));
        CHECK (db (rh) > db (rl) + 3.0, "Grow macro: fuller at 100%% than at 0%% (%.1f dB)", db (rh) - db (rl));

        Rig::Out out; const auto d = chord (Params(), 6, &out);
        float pk = 0; for (size_t i = 0; i < out.L.size(); ++i) pk = std::max ({ pk, std::abs (out.L[i]), std::abs (out.R[i]) });
        std::printf ("      default settings, C major chord: rms %.1f dBFS, peak %.2f\n", db (rmsOf (d)), pk);
        CHECK (db (rmsOf (d)) > -30.0 && db (rmsOf (d)) < -9.0, "default patch loudness is sensible (%.1f dBFS)", db (rmsOf (d)));
        CHECK (pk < 0.98f, "default patch does not hit the limiter (peak %.2f)", pk);

        // per-layer contribution with a held chord, each layer alone at its default level
        std::printf ("      per-layer rms with the default knob position, chord held:\n");
        struct L { const char* n; void (*f) (Params&); };
        for (auto l : { L { "piano", [] (Params& p) { p.pnLevel = Params().pnLevel; } },
                        L { "pad", [] (Params& p) { p.pdLevel = Params().pdLevel; } },
                        L { "bubbles", [] (Params& p) { p.bbLevel = Params().bbLevel; } },
                        L { "granulizer", [] (Params& p) { p.grLevel = Params().grLevel; } },
                        L { "water", [] (Params& p) { p.nWater = Params().nWater; } },
                        L { "wind", [] (Params& p) { p.nWind = Params().nWind; } },
                        L { "birds", [] (Params& p) { p.nBirds = Params().nBirds; } },
                        L { "tuned wind", [] (Params& p) { p.nWindTone = Params().nWindTone; } },
                        L { "reverb (pad+piano)", [] (Params& p) { p.pdLevel = Params().pdLevel; p.pnLevel = Params().pnLevel; p.rvMix = Params().rvMix; p.rvSize = Params().rvSize; } },
                        L { "same, no reverb", [] (Params& p) { p.pdLevel = Params().pdLevel; p.pnLevel = Params().pnLevel; p.rvMix = 0.0f; } } })
        {
            Params p = silent(); l.f (p);
            std::printf ("        %-10s %.1f dBFS\n", l.n, db (rmsOf (chord (p, 6))));
        }
    }

    std::printf ("== Reverb ==\n");
    {
        // play a short piano note, release it, then listen to what is left 1.2-2.2 s later
        auto tail = [] (float mix, float size, double t0, double t1, Rig::Out* keep = nullptr)
        {
            Params p = silent(); p.pnLevel = 1.0f; p.pnRel = 0.08f; p.rvMix = mix; p.rvSize = size;
            Rig r (48000.0, p);
            r.e.noteOn (60, 0.9f);
            auto o1 = r.run (0.4);
            r.e.noteOff (60);
            auto o2 = r.run (3.0);
            Rig::Out all; all.L = o1.L; all.R = o1.R;
            all.L.insert (all.L.end(), o2.L.begin(), o2.L.end());
            all.R.insert (all.R.end(), o2.R.begin(), o2.R.end());
            if (keep) *keep = all;
            return rmsOf (mono (all, 48000, 0.4 + t0, 0.4 + t1));
        };
        const double dry = tail (0.0f, 0.7f, 1.2, 2.2);
        const double wet = tail (0.5f, Params().rvSize, 1.2, 2.2);
        const double small = tail (0.5f, 0.1f, 1.2, 2.2), large = tail (0.5f, 1.0f, 1.2, 2.2);
        std::printf ("      tail level 1.2-2.2 s after key up: dry %.0f dBFS, reverb 50%% %.0f dBFS (size 0.1: %.0f, size 1.0: %.0f)\n",
                     db (dry), db (wet), db (small), db (large));
        CHECK (db (dry) < -90.0, "reverb off: nothing rings after the damped key release");
        CHECK (db (wet) > -60.0, "reverb on: an audible tail remains (%.0f dBFS)", db (wet));
        CHECK (db (large) > db (small) + 6.0, "size knob: a large room rings longer than a small one (%.1f dB)", db (large) - db (small));

        Rig::Out o; tail (0.6f, 0.7f, 1.0, 2.0, &o);
        double sLR = 0, sLL = 0, sRR = 0;
        for (size_t i = (size_t) (1.4 * 48000); i < (size_t) (2.4 * 48000); ++i)
        { sLR += (double) o.L[i] * o.R[i]; sLL += (double) o.L[i] * o.L[i]; sRR += (double) o.R[i] * o.R[i]; }
        const double corr = sLR / std::sqrt (sLL * sRR + 1e-30);
        CHECK (corr < 0.95, "reverb tail is wide stereo (L/R correlation %.2f)", corr);

        // extremes stay finite
        bool ok = true;
        for (float sz : { 0.0f, 1.0f }) for (float mx : { 0.0f, 1.0f })
        { Params p = silent(); p.pnLevel = 1.0f; p.rvMix = mx; p.rvSize = sz; p.rvDamp = sz; Rig r (44100.0, p); r.e.noteOn (72, 1.0f); auto q = r.run (3.0);
          for (float x : q.L) if (! std::isfinite (x) || std::abs (x) > 1.0f) ok = false; }
        CHECK (ok, "reverb extremes (size/mix/damp 0 and 1) stay finite and bounded");
    }

    std::printf ("== Robustness and CPU ==\n");
    {
        // random everything for 60 seconds
        Rig r (44100.0, silent());
        std::mt19937 rng (11); std::uniform_real_distribution<float> u (0, 1);
        bool bad = false; float peak = 0; double t = 0;
        std::vector<float> L (777), R (777);
        for (int block = 0; block < 3400; ++block)
        {
            Params p;
            p.root = (int) (u (rng) * 11.99f); p.scale = (int) (u (rng) * 9.99f); p.snap = u (rng) < 0.5f; p.grow = u (rng); p.masterDb = u (rng) * 12 - 6;
            p.pnLevel = u (rng); p.pnTone = u (rng); p.pnRel = 0.05f + u (rng) * 3; p.pdLevel = u (rng); p.pdAttack = 0.05f + u (rng) * 6; p.pdRel = 0.1f + u (rng) * 8;
            p.pdDetune = u (rng) * 40; p.pdBright = u (rng); p.pdOct = (int) (u (rng) * 2.99f) - 1; p.bbLevel = u (rng); p.bbDensity = u (rng) * 16; p.bbRegister = u (rng);
            p.bbSize = u (rng); p.bbBloop = u (rng); p.nWater = u (rng); p.nRain = u (rng); p.nWind = u (rng); p.nWindTone = u (rng); p.nBirds = u (rng); p.nCrickets = u (rng);
            p.grLevel = u (rng); p.grSizeMs = 20 + u (rng) * 480; p.grBurst = 1 + u (rng) * 7; p.grPos = u (rng); p.grPosScatter = u (rng); p.grPitchScatter = u (rng) * 7;
            p.grOct = u (rng); p.grRev = u (rng); p.grSpread = u (rng); p.grDensity = 1 + u (rng) * 59; p.grRate = (int) (u (rng) * 3.99f); p.grPattern = u (rng) < 0.5f;
            for (int s = 0; s < 16; ++s) { p.stepOn[(size_t) s] = u (rng) < 0.6f; p.stepDeg[(size_t) s] = (int) (u (rng) * 14.99f) - 7; }
            p.rvMix = u (rng); p.rvSize = u (rng); p.rvDamp = u (rng);
            r.e.setParams (p);
            r.e.setTransport (u (rng) < 0.3f, 60 + u (rng) * 140, true, t);
            const int ev = (int) (u (rng) * 6);
            for (int k = 0; k < ev; ++k)
            {
                const int note = (int) (u (rng) * 127.99f);
                const float x = u (rng);
                if (x < 0.55f) r.e.noteOn (note, 0.05f + 0.95f * u (rng)); else if (x < 0.9f) r.e.noteOff (note);
                else if (x < 0.95f) r.e.controller (64, u (rng) < 0.5f ? 127 : 0); else if (x < 0.97f) r.e.allNotesOff();
            }
            r.e.render (L.data(), R.data(), (int) L.size());
            t += 777.0 / 44100.0 * 2.0;
            for (size_t i = 0; i < L.size(); ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true; peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) }); }
        }
        CHECK (! bad, "60 s of random automation, notes, pedal and transport: no NaN/inf");
        CHECK (peak <= 1.0001f, "output never exceeds full scale (peak %.3f)", peak);

        // voice stealing: hammer 200 notes
        Params p = silent(); p.pnLevel = 1; p.pdLevel = 1;
        Rig s (48000.0, p); for (int i = 0; i < 200; ++i) { s.e.noteOn (36 + i % 60, 0.9f); s.run (0.01); }
        auto so = s.run (0.5); bool fin = true; for (float x : so.L) if (! std::isfinite (x)) fin = false;
        CHECK (fin, "200 rapid notes (voice stealing) stay finite");

        // CPU: everything on, 10 notes held, dense settings
        Params h; h.pnLevel = h.pdLevel = h.bbLevel = h.grLevel = 1.0f; h.bbDensity = 16.0f; h.grPattern = true; h.stepOn.fill (true); h.grBurst = 8; h.grRate = 3;
        h.nWater = h.nRain = h.nWind = h.nWindTone = h.nBirds = h.nCrickets = 1.0f; h.grSizeMs = 500; h.grow = 1.0f;
        Rig c (48000.0, h);
        for (int n : { 36, 43, 48, 52, 55, 60, 64, 67, 72, 76 }) c.e.noteOn (n, 0.8f);
        const double secs = 20.0;
        const auto t0 = std::chrono::steady_clock::now();
        c.run (secs);
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("      worst-case patch: %.1f s of audio rendered in %.2f s -> %.1f%% of one core\n", secs, el, 100.0 * el / secs);
        CHECK (el / secs < 0.35, "worst-case CPU load is acceptable (%.1f%% of a core)", 100.0 * el / secs);

        Params d;
        Rig dc (48000.0, d); for (int n : { 48, 55, 60, 64, 67 }) dc.e.noteOn (n, 0.8f);
        const auto t1 = std::chrono::steady_clock::now(); dc.run (secs);
        const double el2 = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
        std::printf ("      default patch, 5 notes: %.1f%% of one core\n", 100.0 * el2 / secs);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
