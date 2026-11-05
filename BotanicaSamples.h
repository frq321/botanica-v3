#pragma once
// Loads the packed piano samples ("BPAK" container of Ogg Vorbis files) into a PianoBank.
#include <juce_audio_formats/juce_audio_formats.h>
#include "BotanicaEngine.h"
#include <cstring>
#include <string>

namespace bot
{
inline std::shared_ptr<PianoBank> loadPianoPak (const void* data, size_t size)
{
    auto bank = std::make_shared<PianoBank>();
    const auto* p = static_cast<const uint8_t*> (data);
    if (size < 8 || std::memcmp (p, "BPAK", 4) != 0)
        return bank;

    uint32_t count = 0;
    std::memcpy (&count, p + 4, 4);
    size_t off = 8;
    juce::OggVorbisAudioFormat fmt;

    for (uint32_t i = 0; i < count && off + 5 <= size; ++i)
    {
        const int root = p[off];
        uint32_t sz = 0;
        std::memcpy (&sz, p + off + 1, 4);
        off += 5;
        if (off + sz > size) break;

        std::unique_ptr<juce::AudioFormatReader> reader (
            fmt.createReaderFor (new juce::MemoryInputStream (p + off, (size_t) sz, false), true));
        if (reader != nullptr && reader->lengthInSamples > 0)
        {
            PianoSample s;
            s.root = root;
            s.sr = reader->sampleRate;
            const int len = (int) reader->lengthInSamples;
            s.data.setSize (2, len);
            reader->read (&s.data, 0, len, 0, true, true);
            if (reader->numChannels == 1)
                s.data.copyFrom (1, 0, s.data, 0, 0, len);
            bank->samples.push_back (std::move (s));
        }
        off += sz;
    }

    std::sort (bank->samples.begin(), bank->samples.end(),
               [] (const PianoSample& a, const PianoSample& b) { return a.root < b.root; });
    return bank;
}

// "NPAK": recorded nature. Entries are (type 0 = loop bed 0..3, type 1 = bird chirp) as Ogg Vorbis.
inline std::shared_ptr<NatureBank> loadNaturePak (const void* data, size_t size)
{
    auto bank = std::make_shared<NatureBank>();
    const auto* p = static_cast<const uint8_t*> (data);
    if (size < 8 || std::memcmp (p, "NPAK", 4) != 0)
        return bank;

    uint32_t count = 0;
    std::memcpy (&count, p + 4, 4);
    size_t off = 8;
    juce::OggVorbisAudioFormat fmt;

    for (uint32_t i = 0; i < count && off + 6 <= size; ++i)
    {
        const int type = p[off], idx = p[off + 1];
        uint32_t sz = 0;
        std::memcpy (&sz, p + off + 2, 4);
        off += 6;
        if (off + sz > size) break;

        std::unique_ptr<juce::AudioFormatReader> reader (
            fmt.createReaderFor (new juce::MemoryInputStream (p + off, (size_t) sz, false), true));
        if (reader != nullptr && reader->lengthInSamples > 0)
        {
            auto sp = std::make_shared<LoopSample>();
            auto& s = *sp;
            s.sr = reader->sampleRate;
            const int len = (int) reader->lengthInSamples;
            s.data.setSize (2, len);
            reader->read (&s.data, 0, len, 0, true, true);
            if (reader->numChannels == 1)
                s.data.copyFrom (1, 0, s.data, 0, 0, len);
            if (type == 0 && idx < NatureBank::kBeds) bank->beds[(size_t) idx] = sp;
            else if (type == 1) bank->chirps.push_back (sp);
        }
        off += sz;
    }
    return bank;
}

// "IPAK": built-in instruments (chromatic multisamples, every 3rd note, with sustain loops).
// Indexed first so a bank is only decoded when somebody picks it.
struct InstIndex
{
    std::string name;
    struct Zone { int root; uint32_t loopStart, loopEnd, xfade, off, size; };
    std::vector<Zone> zones;
};

inline std::vector<InstIndex> indexInstPak (const void* data, size_t size)
{
    std::vector<InstIndex> out;
    const auto* p = static_cast<const uint8_t*> (data);
    if (size < 8 || std::memcmp (p, "IPAK", 4) != 0) return out;
    uint32_t n = 0; std::memcpy (&n, p + 4, 4);
    size_t off = 8;
    for (uint32_t i = 0; i < n && off + 28 <= size; ++i)
    {
        InstIndex ii;
        char nm[25] = {}; std::memcpy (nm, p + off, 24); ii.name = nm;
        uint32_t nz = 0; std::memcpy (&nz, p + off + 24, 4);
        off += 28;
        for (uint32_t z = 0; z < nz && off + 17 <= size; ++z)
        {
            InstIndex::Zone zn {};
            zn.root = p[off];
            std::memcpy (&zn.loopStart, p + off + 1, 4); std::memcpy (&zn.loopEnd, p + off + 5, 4);
            std::memcpy (&zn.xfade, p + off + 9, 4);     std::memcpy (&zn.size, p + off + 13, 4);
            off += 17; zn.off = (uint32_t) off;
            if (off + zn.size > size) break;
            off += zn.size;
            ii.zones.push_back (zn);
        }
        out.push_back (std::move (ii));
    }
    return out;
}

inline std::shared_ptr<PianoBank> decodeInst (const void* data, size_t size, const InstIndex& ii)
{
    auto bank = std::make_shared<PianoBank>();
    const auto* p = static_cast<const uint8_t*> (data);
    juce::OggVorbisAudioFormat fmt;
    for (const auto& z : ii.zones)
    {
        if ((size_t) z.off + z.size > size) break;
        std::unique_ptr<juce::AudioFormatReader> reader (
            fmt.createReaderFor (new juce::MemoryInputStream (p + z.off, (size_t) z.size, false), true));
        if (reader == nullptr || reader->lengthInSamples <= 0) continue;
        PianoSample s;
        s.root = z.root; s.sr = reader->sampleRate;
        const int len = (int) reader->lengthInSamples;
        s.data.setSize (2, len);
        reader->read (&s.data, 0, len, 0, true, true);
        if (reader->numChannels == 1) s.data.copyFrom (1, 0, s.data, 0, 0, len);
        s.loopStart = (int) z.loopStart; s.loopEnd = (int) z.loopEnd; s.xfade = (int) z.xfade;
        s.sanitiseLoop();
        bank->samples.push_back (std::move (s));
    }
    std::sort (bank->samples.begin(), bank->samples.end(), [] (const PianoSample& a, const PianoSample& b) { return a.root < b.root; });
    return bank;
}
} // namespace bot
