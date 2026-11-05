#pragma once
// Turns audio files that the user drops on the plugin into playable sounds:
//   importInstrument - a "one note per slot" recording (like the piano) or a single sustained note / pad
//   importBed        - a seamless nature loop
//   importChirps     - short calls cut out of a longer recording
// Everything here is offline analysis; it runs on a background thread, never on the audio thread.
#include <juce_audio_formats/juce_audio_formats.h>
#include "BotanicaEngine.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace bot
{
struct Decoded
{
    juce::AudioBuffer<float> data;       // 2 channels
    double sr = 44100.0;
    int length() const { return data.getNumSamples(); }
};

inline bool decodeFile (const juce::File& f, Decoded& out, double maxSeconds = 720.0)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples < 2048 || r->sampleRate < 8000.0) return false;
    if ((double) r->lengthInSamples / r->sampleRate > maxSeconds) return false;
    const int len = (int) r->lengthInSamples;
    out.sr = r->sampleRate;
    out.data.setSize (2, len);
    if (! r->read (&out.data, 0, len, 0, true, true)) return false;
    if (r->numChannels == 1) out.data.copyFrom (1, 0, out.data, 0, 0, len);
    return true;
}

namespace imp
{
inline std::vector<float> mono (const Decoded& d)
{
    const int n = d.length();
    std::vector<float> m ((size_t) n);
    const float* a = d.data.getReadPointer (0); const float* b = d.data.getReadPointer (1);
    for (int i = 0; i < n; ++i) m[(size_t) i] = 0.5f * (a[i] + b[i]);
    return m;
}

inline double dbOf (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-9)); }

// YIN fundamental estimate; returns 0 when the signal is not clearly pitched
inline double yin (const float* x, int n, double sr, double fmin, double fmax, double& confidence)
{
    const int tauMax = std::min (n / 2, (int) (sr / fmin)), tauMin = std::max (2, (int) (sr / fmax));
    const int W = n - tauMax;
    if (W < 256 || tauMax <= tauMin + 2) { confidence = 0; return 0; }
    std::vector<double> d ((size_t) tauMax + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0;
        for (int j = 0; j < W; ++j) { const double e = (double) x[j] - (double) x[j + tau]; s += e * e; }
        d[(size_t) tau] = s;
    }
    std::vector<double> c ((size_t) tauMax + 1, 1.0);
    double run = 0;
    for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; c[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0; }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (c[(size_t) tau] < 0.15 && c[(size_t) tau] <= c[(size_t) tau - 1] && c[(size_t) tau] <= c[(size_t) tau + 1]) { best = tau; break; }
    if (best < 0)
    {
        double m = 1e9;
        for (int tau = tauMin; tau < tauMax; ++tau) if (c[(size_t) tau] < m) { m = c[(size_t) tau]; best = tau; }
        if (m > 0.3) { confidence = 0; return 0; }
    }
    double t = best;
    if (best > 1 && best < tauMax)
    {
        const double a = c[(size_t) best - 1], b = c[(size_t) best], cc = c[(size_t) best + 1], den = a - 2 * b + cc;
        if (std::abs (den) > 1e-12) t += 0.5 * (a - cc) / den;
    }
    confidence = 1.0 - c[(size_t) best];
    return sr / t;
}

// pitch of a stretch of audio, median over a few windows; midi (fractional) or -1
inline double pitchOf (const std::vector<float>& m, double sr, int from, int to)
{
    std::vector<double> est;
    const int win = (int) (0.093 * sr);                         // ~4096 samples at 44.1 kHz
    const int span = to - from;
    if (span < win) return -1.0;
    for (int k = 0; k < 3; ++k)
    {
        const int a = from + (int) ((double) (span - win) * (0.2 + 0.3 * k));
        double conf = 0;
        const double f = yin (m.data() + a, win, sr, 28.0, 2200.0, conf);
        if (f > 0 && conf > 0.7) est.push_back (69.0 + 12.0 * std::log2 (f / 440.0));
    }
    if (est.size() < 2) return -1.0;
    std::sort (est.begin(), est.end());
    const double med = est[est.size() / 2];
    return std::abs (est.front() - est.back()) < 0.6 ? med : -1.0;
}

inline std::vector<float> envRms (const std::vector<float>& m, int hop)
{
    std::vector<float> e;
    for (size_t i = 0; i + (size_t) hop <= m.size(); i += (size_t) hop)
    {
        double s = 0; for (int k = 0; k < hop; ++k) s += (double) m[i + (size_t) k] * m[i + (size_t) k];
        e.push_back ((float) std::sqrt (s / hop));
    }
    return e;
}

inline void scaleBuffer (juce::AudioBuffer<float>& b, float g) { b.applyGain (g); }

// level (dBFS, mono mix) of a stretch
inline double rmsDb (const juce::AudioBuffer<float>& b, int from, int to)
{
    from = std::max (0, from); to = std::min (b.getNumSamples(), to);
    if (to - from < 64) return -120.0;
    double s = 0;
    for (int ch = 0; ch < 2; ++ch) { const float* d = b.getReadPointer (ch); for (int i = from; i < to; ++i) s += (double) d[i] * d[i]; }
    return dbOf (std::sqrt (s / (2.0 * (to - from))));
}

struct LoopFit { double drift = 99; double start = 0, len = 0; bool ok = false; };

// looks for the stretch before the release whose level at both ends matches (so a loop there is inaudible)
inline LoopFit fitLoop (const std::vector<float>& mono, double sr, int onset, int end)
{
    LoopFit r;
    const int hop = std::max (1, (int) (0.02 * sr));
    std::vector<double> env, t;
    for (int i = onset; i + hop <= end; i += hop)
    {
        double s = 0; for (int k = 0; k < hop; ++k) s += (double) mono[(size_t) (i + k)] * mono[(size_t) (i + k)];
        env.push_back (dbOf (std::sqrt (s / hop)) ); t.push_back ((double) (i - onset + hop / 2) / sr);
    }
    if (env.size() < 40) return r;
    std::vector<double> ref;
    for (size_t k = 0; k < t.size(); ++k) if (t[k] > 0.6 && t[k] < 1.2) ref.push_back (env[k]);
    if (ref.empty()) return r;
    std::sort (ref.begin(), ref.end());
    const double refDb = ref[ref.size() / 2];
    double te = t.back();
    for (size_t k = 0; k < t.size(); ++k) if (t[k] > 1.2 && env[k] < refDb - 6.0) { te = t[k]; break; }
    const double tl = te - 0.15;
    for (double tgt : { 1.0, 0.8, 0.6 })
        for (double st = 0.5; st <= tl - tgt + 1e-6; st += 0.05)
        {
            std::vector<double> e;
            for (size_t k = 0; k < t.size(); ++k) if (t[k] >= st && t[k] < st + tgt) e.push_back (env[k]);
            if (e.size() < 15) continue;
            const size_t q = std::max<size_t> (4, e.size() / 5);
            double a = 0, b = 0;
            for (size_t k = 0; k < q; ++k) { a += e[k]; b += e[e.size() - 1 - k]; }
            const double dr = std::abs (a - b) / (double) q;
            if (! r.ok || dr < r.drift - 0.05 || (std::abs (dr - r.drift) <= 0.05 && tgt > r.len))
            { r.ok = true; r.drift = dr; r.start = st; r.len = tgt; }
        }
    return r;
}
} // namespace imp

//------------------------------------------------------------------------------
// instrument: many notes in a row (one slot each) or a single note / texture
inline std::shared_ptr<PianoBank> importInstrument (const Decoded& d, juce::String& info)
{
    auto bank = std::make_shared<PianoBank>();
    const int n = d.length();
    const double sr = d.sr;
    auto m = imp::mono (d);

    // --- onsets (rises above 8 % of the peak after at least 0.3 s of quiet)
    const int hop = std::max (1, (int) (0.01 * sr));
    std::vector<float> pk;
    for (int i = 0; i + hop <= n; i += hop) { float mx = 0; for (int k = 0; k < hop; ++k) mx = std::max (mx, std::abs (m[(size_t) (i + k)])); pk.push_back (mx); }
    // the stereo mix can cancel: use the louder channel for detection
    {
        const float* a = d.data.getReadPointer (0); const float* b = d.data.getReadPointer (1);
        for (size_t k = 0; k < pk.size(); ++k)
        {
            float mx = pk[k];
            for (int j = 0; j < hop; ++j) { const size_t i = k * (size_t) hop + (size_t) j; mx = std::max (mx, std::max (std::abs (a[i]), std::abs (b[i]))); }
            pk[k] = mx;
        }
    }
    const float peak = *std::max_element (pk.begin(), pk.end());
    std::vector<int> onsets;
    {
        int below = 1000;
        for (size_t k = 0; k < pk.size(); ++k)
        {
            if (pk[k] > 0.08f * peak) { if (below >= 30) onsets.push_back ((int) k * hop); below = 0; }
            else ++below;
        }
    }
    bool regular = onsets.size() >= 6;
    double gap = 0;
    if (regular)
    {
        std::vector<double> g; for (size_t i = 1; i < onsets.size(); ++i) g.push_back ((double) (onsets[i] - onsets[i - 1]));
        std::sort (g.begin(), g.end()); gap = g[g.size() / 2];
        int good = 0; for (double x : g) if (std::abs (x - gap) < 0.15 * gap) ++good;
        regular = gap > 0.6 * sr && gap < 12.0 * sr && good >= (int) (0.6 * (double) g.size());
    }

    struct Z { int root; double fine; PianoSample s; };
    std::vector<Z> zones;

    if (regular)
    {
        // a slot may be empty (rests between notes), so measure every slot that starts near an onset
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            const int on = onsets[i];
            const int slotEnd = i + 1 < onsets.size() ? onsets[i + 1] - (int) (0.03 * sr) : std::min (n, on + (int) gap);
            const int start = std::max (0, on - (int) (0.008 * sr));
            const int maxLen = (int) (4.0 * sr);
            int end = std::min (std::min (slotEnd, start + maxLen), n);
            if (end - start < (int) (0.5 * sr)) continue;
            // last audible point (-70 dB) + a short tail
            int last = start;
            { const int h = std::max (1, (int) (0.01 * sr));
              for (int p = start; p + h <= end; p += h) { double s = 0; for (int k = 0; k < h; ++k) s += (double) m[(size_t) (p + k)] * m[(size_t) (p + k)]; if (std::sqrt (s / h) > 3.0e-4) last = p + h; } }
            end = std::min (end, last + (int) (0.12 * sr));
            if (end - start < (int) (0.5 * sr)) continue;

            const double midi = imp::pitchOf (m, sr, on + (int) (0.25 * sr), std::min (end, on + (int) (1.2 * sr)));
            if (midi < 12.0 || midi > 120.0) continue;
            Z z; z.root = (int) std::lround (midi); z.fine = midi - (double) z.root;
            z.s.root = z.root; z.s.fine = z.fine; z.s.sr = sr;
            z.s.data.setSize (2, end - start);
            for (int ch = 0; ch < 2; ++ch) z.s.data.copyFrom (ch, 0, d.data, ch, start, end - start);
            z.s.loopStart = on - start;                       // temporarily: the onset position inside the zone
            zones.push_back (std::move (z));
        }
        // one zone per pitch; thin out to roughly every 3rd semitone
        std::sort (zones.begin(), zones.end(), [] (const Z& a, const Z& b) { return a.root < b.root; });
        std::vector<Z> kept;
        for (auto& z : zones)
        {
            if (! kept.empty() && z.root == kept.back().root) continue;
            if (! kept.empty() && z.root - kept.back().root < 3 && zones.size() > 30) continue;
            kept.push_back (std::move (z));
        }
        zones = std::move (kept);
        info = juce::String ((int) zones.size()) + " notes found";
    }
    else
    {
        // single sound: a note or a texture. Cap at 30 s.
        const int len = std::min (n, (int) (30.0 * sr));
        Z z; z.s.sr = sr;
        z.s.data.setSize (2, len);
        for (int ch = 0; ch < 2; ++ch) z.s.data.copyFrom (ch, 0, d.data, ch, 0, len);
        const double midi = imp::pitchOf (m, sr, (int) (0.05 * len), (int) (0.95 * len));
        if (midi >= 12.0 && midi <= 120.0) { z.root = (int) std::lround (midi); z.fine = midi - (double) z.root; info = "single note, pitch found"; }
        else { z.root = 60; z.fine = 0; info = "single sound (no clear pitch, played as C4)"; }
        z.s.root = z.root; z.s.fine = z.fine;
        z.s.loopStart = 0;
        zones.push_back (std::move (z));
    }
    if (zones.empty()) { info = "no notes found"; bank->samples.clear(); return bank; }

    // --- sustain loops
    const bool multi = regular;
    std::vector<imp::LoopFit> fits (zones.size());
    int good = 0;
    for (size_t k = 0; k < zones.size(); ++k)
    {
        auto mz = imp::mono ([&] { Decoded t; t.sr = sr; t.data.makeCopyOf (zones[k].s.data); return t; }());
        const int onset = multi ? zones[k].s.loopStart : 0;
        fits[k] = imp::fitLoop (mz, sr, onset, (int) mz.size());
        if (fits[k].ok && fits[k].drift < 2.5) ++good;
    }
    const bool loopInst = multi ? (good * 2 >= (int) zones.size()) : false;

    for (size_t k = 0; k < zones.size(); ++k)
    {
        auto& s = zones[k].s;
        const int len = s.data.getNumSamples();
        const int onset = multi ? s.loopStart : 0;
        s.loopStart = s.loopEnd = s.xfade = 0;
        if (loopInst && fits[k].ok)
        {
            const double f0 = 440.0 * std::pow (2.0, ((double) s.root + s.fine - 69.0) / 12.0), per = sr / f0;
            const int kk = std::max (1, (int) std::lround (fits[k].len * sr / per)), L = (int) std::lround ((double) kk * per);
            s.loopStart = onset + (int) (fits[k].start * sr); s.loopEnd = s.loopStart + L;
            s.xfade = std::min ((int) (0.06 * sr), L);
        }
        else if (! multi && len > (int) (2.5 * sr))
        {
            s.loopStart = std::min ((int) (2.2 * sr), len / 3);
            s.loopEnd = len - (int) (0.25 * sr);
            s.xfade = std::min ((int) (2.0 * sr), (s.loopEnd - s.loopStart) / 3);
        }
        s.sanitiseLoop();
    }

    // --- loudness: sustain level of the notes -> about -21 dBFS, like the built-in instruments
    std::vector<double> lv;
    for (auto& z : zones)
    {
        const int len = z.s.data.getNumSamples();
        const int a = multi ? (int) (0.6 * sr) : (int) (0.1 * len), b = multi ? (int) (1.5 * sr) : (int) (0.9 * len);
        const double db = imp::rmsDb (z.s.data, a, std::min (len, b));
        if (db > -90.0) lv.push_back (db);
    }
    float g = 1.0f;
    if (! lv.empty())
    {
        std::sort (lv.begin(), lv.end());
        g = (float) std::pow (10.0, (-21.0 - lv[lv.size() / 2]) / 20.0);
        float mx = 0; for (auto& z : zones) mx = std::max (mx, z.s.data.getMagnitude (0, z.s.data.getNumSamples()));
        if (mx * g > 0.95f) g = 0.95f / mx;
    }
    for (auto& z : zones) { z.s.data.applyGain (g); z.s.fine = z.fine; bank->samples.push_back (std::move (z.s)); }
    return bank;
}

//------------------------------------------------------------------------------
// seamless loop for the nature beds
inline bool importBed (const Decoded& d, LoopSample& out)
{
    const int n = d.length();
    const double sr = d.sr;
    if (n < (int) (1.0 * sr)) return false;
    auto m = imp::mono (d);
    const double secs = (double) n / sr;
    const double X = std::min (3.0, secs * 0.15);                 // cross-fade length
    const double L = std::min (28.0, secs - X);
    if (L < 0.5) return false;
    const int Ls = (int) (L * sr), Xs = (int) (X * sr);

    // steadiest window: lowest spread of the 0.5 s level, and no sudden jumps
    const int hop = (int) (0.5 * sr);
    std::vector<double> lvl;
    for (int i = 0; i + hop <= n; i += hop) { double s = 0; for (int k = 0; k < hop; ++k) s += (double) m[(size_t) (i + k)] * m[(size_t) (i + k)]; lvl.push_back (imp::dbOf (std::sqrt (s / hop))); }
    const int k = (int) ((L + X) / 0.5);
    int bestStart = 0; double best = 1e18;
    const int guard = secs > 40.0 ? 4 : 0;                        // skip fades at either end of long files
    const int lo = guard, hi = (int) lvl.size() - k - guard;
    for (int i = lo; i <= hi; ++i)
    {
        double mean = 0; for (int j = 0; j < k; ++j) mean += lvl[(size_t) (i + j)]; mean /= k;
        double var = 0, jump = 0;
        for (int j = 0; j < k; ++j) { var += (lvl[(size_t) (i + j)] - mean) * (lvl[(size_t) (i + j)] - mean); if (j) jump = std::max (jump, std::abs (lvl[(size_t) (i + j)] - lvl[(size_t) (i + j - 1)])); }
        const double sc = std::sqrt (var / k) + 0.5 * jump;
        if (sc < best) { best = sc; bestStart = i; }
    }
    const int a = std::max (0, std::min (n - Ls - Xs, bestStart * hop));

    out.sr = sr;
    out.data.setSize (2, Ls);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = d.data.getReadPointer (ch) + a;
        float* dst = out.data.getWritePointer (ch);
        for (int i = 0; i < Ls; ++i) dst[i] = src[i];
        for (int i = 0; i < Xs; ++i)                              // head fades in against the audio that followed the loop end
        {
            const float t = 1.5707964f * (float) i / (float) (Xs - 1);
            dst[i] = src[i] * std::sin (t) + src[Ls + i] * std::cos (t);
        }
    }
    const double db = imp::rmsDb (out.data, 0, Ls);
    float g = (float) std::pow (10.0, (-22.0 - db) / 20.0);
    const float mx = out.data.getMagnitude (0, Ls);
    if (mx * g > 0.9f) g = 0.9f / std::max (mx, 1.0e-6f);
    out.data.applyGain (g);
    return true;
}

//------------------------------------------------------------------------------
// bird calls (or any short events) cut out of a recording
inline bool importChirps (const Decoded& d, std::vector<std::shared_ptr<LoopSample>>& out, int maxCount = 24)
{
    const int n = d.length();
    const double sr = d.sr;
    auto m = imp::mono (d);
    auto finish = [&] (int a, int b)
    {
        a = std::max (0, a); b = std::min (n, b);
        LoopSample s; s.sr = sr; s.data.setSize (2, b - a);
        for (int ch = 0; ch < 2; ++ch) s.data.copyFrom (ch, 0, d.data, ch, a, b - a);
        const int fi = std::min ((int) (0.05 * sr), (b - a) / 4), fo = std::min ((int) (0.3 * sr), (b - a) / 2);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = s.data.getWritePointer (ch);
            for (int i = 0; i < fi; ++i) x[i] *= (float) i / (float) fi;
            for (int i = 0; i < fo; ++i) x[b - a - 1 - i] *= 0.5f - 0.5f * std::cos (3.14159265f * (float) i / (float) fo);
        }
        const float mx = s.data.getMagnitude (0, b - a);
        if (mx < 1.0e-5f) return;
        s.data.applyGain (0.8f / mx);
        out.push_back (std::make_shared<LoopSample> (std::move (s)));
    };
    if ((double) n / sr <= 4.0) { finish (0, n); return ! out.empty(); }

    const int hop = std::max (1, (int) (0.01 * sr));
    auto env = imp::envRms (m, hop);
    std::vector<float> sorted = env; std::sort (sorted.begin(), sorted.end());
    const float floorV = std::max (sorted[sorted.size() / 5], 1.0e-6f), top = sorted.back();
    const float thr = std::max (floorV * 2.0f, top * 0.03f);
    struct Seg { double score; int a, b; };
    std::vector<Seg> segs;
    int k = 0; const int nE = (int) env.size();
    while (k < nE)
    {
        if (env[(size_t) k] > thr)
        {
            int last = k, j = k, gapCount = 0;
            while (j < nE && gapCount < 25) { if (env[(size_t) j] > thr) { last = j; gapCount = 0; } else ++gapCount; ++j; }
            const double dur = (double) (last - k + 1) * hop / sr;
            if (dur >= 0.3 && dur <= 3.5)
            {
                float mx = 0; for (int q = k; q <= last; ++q) mx = std::max (mx, env[(size_t) q]);
                segs.push_back ({ (double) mx / floorV, k * hop - (int) (0.07 * sr), (last + 1) * hop + (int) (0.15 * sr) });
            }
            k = j;
        }
        else ++k;
    }
    std::sort (segs.begin(), segs.end(), [] (const Seg& a, const Seg& b) { return a.score > b.score; });
    std::vector<Seg> chosen;
    for (auto& s : segs)
    {
        bool clash = false;
        for (auto& c : chosen) if (s.b > c.a - (int) sr && s.a < c.b + (int) sr) clash = true;
        if (! clash) chosen.push_back (s);
        if ((int) chosen.size() >= maxCount) break;
    }
    for (auto& c : chosen) finish (c.a, c.b);
    return ! out.empty();
}
} // namespace bot
