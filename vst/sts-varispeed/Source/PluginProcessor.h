#pragma once

#include <JuceHeader.h>

extern "C" {
#include "sts_playback_engine.h"
}

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class STSVarispeedAudioProcessor final : public juce::AudioProcessor,
                                         public juce::ChangeBroadcaster {
public:
    struct SampleData {
        std::vector<float> mono;
        double sourceRate = 0.0;
        juce::File file;
    };

    STSVarispeedAudioProcessor();
    ~STSVarispeedAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool loadSample(const juce::File& file);
    void reloadSample();
    std::shared_ptr<const SampleData> getSample() const;
    juce::File getSampleFile() const;
    juce::String getSampleStatus() const;
    static juce::String midiNoteName(int note);

    juce::AudioProcessorValueTreeState parameters;
    static constexpr const char* rootNoteId = "root_note";
    static constexpr const char* pitchOffsetId = "pitch_offset";
    static constexpr const char* gainId = "gain";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void renderRange(juce::AudioBuffer<float>&, int start, int end,
                     const SampleData*, float gain);
    void trigger(int midiNote, const SampleData*);
    bool restoreSample(const juce::File&);
    void setStatus(juce::String);
    void publishSample(std::shared_ptr<const SampleData>);

    mutable juce::CriticalSection sampleLock;
    std::shared_ptr<const SampleData> ownedSample;
    std::atomic<const SampleData*> currentSample { nullptr };
    mutable juce::CriticalSection metadataLock;
    juce::File sampleFile;
    juce::String sampleStatus { "No Sample" };
    sts_playback_engine_t playback {};
    double hostSampleRate = 44100.0;
    std::atomic<uint64_t> sampleGeneration { 0 };
    const SampleData* audioSample = nullptr;
    std::atomic<unsigned> audioReaders { 0 };
    uint64_t audioGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(STSVarispeedAudioProcessor)
};
