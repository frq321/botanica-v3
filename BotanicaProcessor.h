#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BotanicaEngine.h"
#include <mutex>

class BotanicaProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::Thread
{
public:
    BotanicaProcessor();
    ~BotanicaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Botanica"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    // the factory presets are also offered to the host as programs
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    bot::Engine engine;                       // the editor reads engine.events / engine.uiStep for its animation

    static juce::String stepOnId (int i)  { return "s" + juce::String (i + 1); }
    static juce::String stepDegId (int i) { return "d" + juce::String (i + 1); }

    bot::Params readParams() const;

    //==========================================================================
    // your own sounds (drag and drop)
    enum class DropTarget { piano, pad, bed0, bed1, bed2, bed3, chirps };
    static constexpr int kNumCustom = 4;
    static constexpr int kNumSources = 8 + kNumCustom;          // 0 = piano / synth pad, 1..7 built in, 8.. your own

    static bool isAudioFile (const juce::File&);
    void importDrop (DropTarget, const juce::File&);            // returns at once; the work runs on a background thread
    void resetRecordings();                                     // back to the built-in loops and chirps
    juce::StringArray soundNames (bool padLayer) const;         // names for the two sound pickers
    juce::String bedName (int bed) const;                       // "" when the built-in loop is in use
    juce::String chirpName() const;
    juce::String statusText() const;                            // last message (import result, error ...)
    bool busy() const { return pendingJobs.load() > 0; }
    bool waitUntilSettled (int timeoutMs);                      // for tests: wait for imports and sound loading

    static juce::File soundsDir();
    static juce::File presetsDir();

    //==========================================================================
    // presets
    struct Factory { juce::String name; std::vector<std::pair<juce::String, float>> values; };
    static const std::vector<Factory>& factoryPresets();
    juce::StringArray userPresetNames() const;
    void loadFactoryPreset (int index);
    bool loadUserPreset (const juce::String& name);
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    juce::String currentPresetName() const { return apvts.state.getProperty ("preset", "Init").toString(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleMidi (const juce::MidiMessage&);

    // ---- background loader
    struct Job { int kind = 0; int index = 0; juce::File file; };
    enum { kInstrument, kBed, kChirps };
    void run() override;
    void doJob (const Job&);
    void refreshSources();
    std::shared_ptr<const bot::PianoBank> bankFor (int index);
    void queue (Job);
    void rebuildNature();
    void applyStateSlots();                                     // re-import the custom sounds a preset / project refers to
    void setStatus (const juce::String&);
    void retire (std::shared_ptr<const void>);
    void pushSources();                                         // audio thread: hand freshly loaded sounds to the engine

    static constexpr int kChunk = 1024;

    // published to the audio thread with std::atomic_load / atomic_store
    std::shared_ptr<const bot::PianoBank> pianoCur, padCur;
    std::shared_ptr<const bot::NatureBank> natureCur;
    const void *pushedPiano = nullptr, *pushedPad = nullptr, *pushedNature = nullptr;   // audio thread only

    mutable std::mutex mu;                                      // guards the custom-sound bookkeeping below
    std::shared_ptr<const bot::PianoBank> customBank[kNumCustom];
    juce::File customPath[kNumCustom];
    int nextCustom = 0;
    std::shared_ptr<bot::LoopSample> bedCustom[bot::NatureBank::kBeds];
    juce::File bedPath[bot::NatureBank::kBeds], chirpPath;
    std::vector<std::shared_ptr<bot::LoopSample>> chirpCustom;
    juce::String status;

    std::mutex jobMu;
    std::vector<Job> jobs;
    std::atomic<int> pendingJobs { 0 };
    std::atomic<int> customGen { 0 };
    std::atomic<int> loadedPn { -1 }, loadedPd { -1 };
    int loadedGen = -1;                                         // loader thread only
    std::vector<std::shared_ptr<const void>> retired;
    std::mutex retiredMu;
    int currentProgram = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BotanicaProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BotanicaProcessor)
};
