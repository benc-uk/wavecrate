#include "PreviewCommands.h"

namespace wavecrate
{
namespace
{
struct DelayedTransport
{
    AudioPreview::Snapshot snapshot;
    int plays = 0;
    int stops = 0;

    AudioPreview::Snapshot getSnapshot() const { return snapshot; }

    void play()
    {
        ++plays;
        snapshot.playRequested = true;
    }

    void stop()
    {
        ++stops;
        snapshot.playRequested = false;
        if (snapshot.state == AudioPreview::State::playing)
            snapshot.state = AudioPreview::State::ready;
    }

    void finishPreparation()
    {
        snapshot.state = snapshot.playRequested ? AudioPreview::State::playing
                                                : AudioPreview::State::ready;
        snapshot.playRequested = false;
    }
};

class PreviewCommandsTests final : public juce::UnitTest
{
public:
    PreviewCommandsTests() : UnitTest("Preview command routing", "WaveCrate") {}

    void runTest() override
    {
        testCase("A second toggle cancels delayed playback from loading or ready", [this]
        {
            for (const auto state : { AudioPreview::State::loading, AudioPreview::State::ready })
            {
                DelayedTransport transport;
                transport.snapshot.state = state;
                togglePreview(transport);
                expect(canStopPreview(transport.getSnapshot()),
                       "Stop must remain available while playback is being prepared");
                togglePreview(transport);
                transport.finishPreparation();
                expectEquals(transport.plays, 1);
                expectEquals(transport.stops, 1);
                expect(transport.snapshot.state == AudioPreview::State::ready,
                       "Completing preparation after cancellation must not start playback");
            }
        });

        testCase("Completed preparation changes toggle to Stop, then back to Play", [this]
        {
            DelayedTransport transport;
            transport.snapshot.state = AudioPreview::State::ready;
            togglePreview(transport);
            transport.finishPreparation();
            togglePreview(transport);
            expectEquals(transport.stops, 1);
            expect(!canStopPreview(transport.getSnapshot()));
            togglePreview(transport);
            transport.finishPreparation();
            expectEquals(transport.plays, 2);
            expect(transport.snapshot.state == AudioPreview::State::playing);
        });
    }
};

PreviewCommandsTests previewCommandsTests;
}
}
