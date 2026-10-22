// Loads the installed VST3 bundles through JUCE's VST3 host, the way a DAW does, and exercises them.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace
{
struct Head : juce::AudioPlayHead
{
    juce::int64 t = 0;
    double sr = 48000.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (200.0);
        p.setTimeInSamples (t);
        p.setPpqPosition ((double) t / (sr * 60.0 / 200.0));
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

int fails = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    fails += ok ? 0 : 1;
}

void exercise (const juce::String& path)
{
    std::cout << "== " << path << "\n";
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> types;
    fmt.findAllTypesForFile (types, path);
    check (types.size() == 1, "bundle exposes one plug-in (" + juce::String (types.size()) + ")");
    if (types.isEmpty())
        return;
    const auto& d = *types[0];
    std::cout << "   name=" << d.name << " instrument=" << (d.isInstrument ? "yes" : "no")
              << " ins=" << d.numInputChannels << " outs=" << d.numOutputChannels << "\n";

    juce::String err;
    auto inst = fmt.createInstanceFromDescription (d, 48000.0, 512, err);
    check (inst != nullptr, "instance created " + err);
    if (inst == nullptr)
        return;

    Head head;
    inst->setPlayHead (&head);
    std::cout << "   buses: in=" << inst->getBusCount (true) << " out=" << inst->getBusCount (false)
              << " totalIn=" << inst->getTotalNumInputChannels() << " totalOut=" << inst->getTotalNumOutputChannels() << "\n";

    inst->prepareToPlay (48000.0, 512);
    std::cout << "   latency=" << inst->getLatencySamples() << "\n";
    const int ch = juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (ch, 512);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int b = 0; b < 400; ++b)
    {
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < 512; ++i)
                buf.setSample (c, i, 0.3f * std::sin ((float) (head.t + i) * 0.03f));
        inst->processBlock (buf, midi);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < 512; ++i)
                finite = finite && std::isfinite (buf.getSample (c, i));
        head.t += 512;
    }
    check (finite, "400 blocks processed, output finite");

    // Ableton deactivates buses (e.g. while swapping sets); the plug-in must allow it.
    inst->releaseResources();
    bool busesOk = true;
    for (int dir = 0; dir < 2; ++dir)
        for (int b = 0; b < inst->getBusCount (dir == 0); ++b)
            if (auto* bus = inst->getBus (dir == 0, b))
                busesOk = busesOk && bus->enable (false) && bus->enable (true);
    check (busesOk, "every bus can be deactivated and re-activated");
    inst->prepareToPlay (48000.0, 512);
    inst->processBlock (buf, midi);

    juce::MemoryBlock state;
    inst->getStateInformation (state);
    inst->setStateInformation (state.getData(), (int) state.getSize());
    check (state.getSize() > 100, "state save / restore (" + juce::String ((int) state.getSize()) + " bytes)");

    if (inst->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        check (ed != nullptr && ed->getWidth() > 100, "editor opens (" + (ed ? juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) : juce::String ("none")) + ")");
        if (ed != nullptr)
            inst->editorBeingDeleted (ed.get());
    }
    inst->releaseResources();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    for (int i = 1; i < argc; ++i)
        exercise (juce::String (argv[i]));
    std::cout << (fails == 0 ? "ALL PASSED\n" : "SOME CHECKS FAILED\n");
    return fails;
}
