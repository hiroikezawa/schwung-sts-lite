#include "PluginProcessor.h"

#include <cstdlib>
#include <cmath>

static void require(bool condition) {
    if (!condition) std::abort();
}

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sts-varispeed-test", ".wav");
    const uint8_t bytes[] = {
        'R','I','F','F', 44,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        0x44,0xac,0,0, 0x88,0x58,0x01,0, 2,0, 16,0,
        'd','a','t','a', 8,0,0,0,
        0,0, 0xff,0x7f, 0,0, 0,0x80
    };
    {
        auto stream = wav.createOutputStream();
        require(stream != nullptr);
        require(stream->write(bytes, sizeof(bytes)));
    }

    STSVarispeedAudioProcessor processor;
    processor.setPlayConfigDetails(0, 2, 44100.0, 16);
    processor.prepareToPlay(44100.0, 16);
    require(processor.loadSample(wav));
    require(processor.getSample()->mono.size() == 4);

    juce::AudioBuffer<float> output(2, 8);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 1), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 2);
    processor.processBlock(output, midi);
    require(std::abs(output.getSample(0, 0)) < 1.0e-7f);
    require(output.getSample(0, 1) > 0.99f);
    require(output.getSample(0, 1) == output.getSample(1, 1));

    output.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 127), 2);
    processor.processBlock(output, midi);
    require(std::abs(output.getSample(0, 2)) < 1.0e-7f);
    require(output.getSample(0, 3) > 0.5f);

    auto* root = processor.parameters.getParameter(
        STSVarispeedAudioProcessor::rootNoteId);
    root->setValueNotifyingHost(root->convertTo0to1(48.0f));

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    STSVarispeedAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getSample() != nullptr);
    require(restored.getSampleFile() == wav);
    require(restored.parameters.getRawParameterValue(
        STSVarispeedAudioProcessor::rootNoteId)->load() == 48.0f);

    require(wav.deleteFile());
    STSVarispeedAudioProcessor missing;
    missing.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(missing.getSample() == nullptr);
    require(missing.getSampleStatus() == "Sample Missing");
    missing.setPlayConfigDetails(0, 2, 44100.0, 16);
    missing.prepareToPlay(44100.0, 16);
    output.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
    missing.processBlock(output, midi);
    require(output.getMagnitude(0, output.getNumSamples()) == 0.0f);
}
