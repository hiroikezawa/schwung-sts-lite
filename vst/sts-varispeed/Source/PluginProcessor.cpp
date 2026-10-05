#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <atomic>
#include <cmath>
#include <limits>

STSVarispeedAudioProcessor::STSVarispeedAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output",
                                                   juce::AudioChannelSet::stereo(),
                                                   true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    sts_playback_engine_reset(&playback);
}

juce::AudioProcessorValueTreeState::ParameterLayout
STSVarispeedAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    result.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(rootNoteId, 1), "Root Note", 0, 127, 60));
    result.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(pitchOffsetId, 1), "Pitch Offset", -48, 24, 0));
    result.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(gainId, 1), "Gain",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.01f), 0.0f));
    return { result.begin(), result.end() };
}

void STSVarispeedAudioProcessor::prepareToPlay(double sampleRate, int) {
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    audioSample = currentSample.load(std::memory_order_acquire);
    audioGeneration = sampleGeneration.load(std::memory_order_acquire);
    sts_playback_engine_reset(&playback);
}

void STSVarispeedAudioProcessor::releaseResources() {
    sts_playback_engine_reset(&playback);
}

bool STSVarispeedAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void STSVarispeedAudioProcessor::trigger(
    int midiNote, const SampleData* sample) {
    const int root = juce::roundToInt(parameters.getRawParameterValue(rootNoteId)->load());
    const float offset = parameters.getRawParameterValue(pitchOffsetId)->load();
    const double semitones = static_cast<double>(midiNote - root) + offset;
    sts_playback_engine_trigger(&playback, semitones,
                                sample ? sample->sourceRate : 0.0,
                                hostSampleRate, sample != nullptr);
}

void STSVarispeedAudioProcessor::renderRange(
    juce::AudioBuffer<float>& buffer, int start, int end,
    const SampleData* sample, float gain) {
    if (!sample || sample->mono.empty() || start >= end)
        return;

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    const auto frames = static_cast<uint32_t>(
        juce::jmin<size_t>(sample->mono.size(), UINT32_MAX));

    for (int frame = start; frame < end; ++frame) {
        float value = sts_playback_engine_next(&playback, sample->mono.data(), frames);
        value = juce::jlimit(-1.0f, 1.0f, value) * gain;
        left[frame] = value;
        if (right != nullptr)
            right[frame] = value;
    }
}

void STSVarispeedAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    audioReaders.fetch_add(1, std::memory_order_acquire);
    const SampleData* sample = currentSample.load(std::memory_order_acquire);
    const uint64_t generation = sampleGeneration.load(std::memory_order_acquire);
    if (sample != audioSample || generation != audioGeneration) {
        audioSample = sample;
        sts_playback_engine_reset(&playback);
        audioGeneration = generation;
    }
    const float gainDb = parameters.getRawParameterValue(gainId)->load();
    const float gain = gainDb <= -59.99f ? 0.0f : juce::Decibels::decibelsToGain(gainDb);

    int cursor = 0;
    for (const auto metadata : midi) {
        const int eventFrame = juce::jlimit(0, buffer.getNumSamples(),
                                            metadata.samplePosition);
        renderRange(buffer, cursor, eventFrame, sample, gain);
        if (metadata.getMessage().isNoteOn())
            trigger(metadata.getMessage().getNoteNumber(), sample);
        cursor = eventFrame;
    }
    renderRange(buffer, cursor, buffer.getNumSamples(), sample, gain);
    audioReaders.fetch_sub(1, std::memory_order_release);
}

bool STSVarispeedAudioProcessor::loadSample(const juce::File& file) {
    if (!file.existsAsFile() || !file.hasFileExtension("wav")) {
        setStatus(file.existsAsFile() ? "WAV files only" : "Sample Missing");
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(
        file.createInputStream().release(), true));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels < 1
        || reader->numChannels > 2 || reader->sampleRate <= 0.0
        || (reader->bitsPerSample != 16 && reader->bitsPerSample != 24
            && reader->bitsPerSample != 32)
        || reader->lengthInSamples > std::numeric_limits<int>::max()) {
        setStatus("Unsupported WAV");
        return false;
    }

    juce::AudioBuffer<float> decoded(static_cast<int>(reader->numChannels),
                                     static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&decoded, 0, decoded.getNumSamples(), 0, true, true)) {
        setStatus("Could not read WAV");
        return false;
    }

    auto fresh = std::make_shared<SampleData>();
    fresh->mono.resize(static_cast<size_t>(decoded.getNumSamples()));
    fresh->sourceRate = reader->sampleRate;
    fresh->file = file;
    const float channelScale = 1.0f / static_cast<float>(decoded.getNumChannels());
    for (int frame = 0; frame < decoded.getNumSamples(); ++frame) {
        float value = 0.0f;
        for (int channel = 0; channel < decoded.getNumChannels(); ++channel)
            value += decoded.getSample(channel, frame);
        value *= channelScale;
        fresh->mono[static_cast<size_t>(frame)] = std::isfinite(value) ? value : 0.0f;
    }

    publishSample(std::shared_ptr<const SampleData>(std::move(fresh)));
    {
        const juce::ScopedLock lock(metadataLock);
        sampleFile = file;
        sampleStatus = file.getFileName();
    }
    sendChangeMessage();
    return true;
}

bool STSVarispeedAudioProcessor::restoreSample(const juce::File& file) {
    {
        const juce::ScopedLock lock(metadataLock);
        sampleFile = file;
    }
    if (loadSample(file))
        return true;
    publishSample({});
    setStatus("Sample Missing");
    return false;
}

void STSVarispeedAudioProcessor::reloadSample() {
    restoreSample(getSampleFile());
}

std::shared_ptr<const STSVarispeedAudioProcessor::SampleData>
STSVarispeedAudioProcessor::getSample() const {
    const juce::ScopedLock lock(sampleLock);
    return ownedSample;
}

juce::File STSVarispeedAudioProcessor::getSampleFile() const {
    const juce::ScopedLock lock(metadataLock);
    return sampleFile;
}

juce::String STSVarispeedAudioProcessor::getSampleStatus() const {
    const juce::ScopedLock lock(metadataLock);
    return sampleStatus;
}

void STSVarispeedAudioProcessor::publishSample(
    std::shared_ptr<const SampleData> sample) {
    std::shared_ptr<const SampleData> retired;
    {
        const juce::ScopedLock lock(sampleLock);
        retired = std::move(ownedSample);
        ownedSample = std::move(sample);
        currentSample.store(ownedSample.get(), std::memory_order_release);
        sampleGeneration.fetch_add(1, std::memory_order_release);
    }
    while (retired && audioReaders.load(std::memory_order_acquire) != 0)
        juce::Thread::yield();
    retired.reset();
}

void STSVarispeedAudioProcessor::setStatus(juce::String status) {
    {
        const juce::ScopedLock lock(metadataLock);
        sampleStatus = std::move(status);
    }
    sendChangeMessage();
}

juce::String STSVarispeedAudioProcessor::midiNoteName(int note) {
    return juce::MidiMessage::getMidiNoteName(juce::jlimit(0, 127, note),
                                              true, true, 3);
}

void STSVarispeedAudioProcessor::getStateInformation(juce::MemoryBlock& output) {
    auto state = parameters.copyState();
    state.setProperty("sample_path", getSampleFile().getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, output);
}

void STSVarispeedAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
            parameters.replaceState(state);
            const auto path = state.getProperty("sample_path").toString();
            if (path.isNotEmpty())
                restoreSample(juce::File(path));
        }
    }
}

juce::AudioProcessorEditor* STSVarispeedAudioProcessor::createEditor() {
    return new STSVarispeedAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new STSVarispeedAudioProcessor();
}
