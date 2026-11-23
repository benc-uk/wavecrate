#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>
#include <memory>

namespace wavecrate
{
class AudioPreview final
{
public:
    enum class State
    {
        empty,
        loading,
        ready,
        playing,
        error
    };

    struct Snapshot
    {
        State state = State::empty;
        juce::File file;
        juce::String message;
        juce::String deviceMessage;
        bool audioAvailable = false;
        bool playRequested = false;
        double durationSeconds = 0.0;
        double positionSeconds = 0.0;
        double sampleRate = 0.0;
        unsigned int channels = 0;
        unsigned int bitDepth = 0;
        juce::int64 fileSizeBytes = -1;
        juce::int64 totalFrames = 0;
        // Source frames [start, end). Empty when no region is set.
        juce::Range<juce::int64> region;
        bool loopEnabled = false;
    };

    AudioPreview();
    ~AudioPreview();

    void initialiseAudio();
    void loadFile(const juce::File& file);
    void play();
    void stop();
    void seekToFrame(juce::int64 frame);
    // Restricts playback to source frames [start, end). An empty range clears it.
    void setRegion(juce::Range<juce::int64> frames);
    void clearRegion();
    // Loops the region when one is set, otherwise the whole sample. Defaults off.
    void setLoopEnabled(bool shouldLoop);
    Snapshot getSnapshot() const;

    std::function<void()> onStateChanged;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPreview)
};
}
