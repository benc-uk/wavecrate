#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

namespace wavecrate
{
class WaveformData final
{
public:
    enum class State { empty, loading, ready, error };

    struct Peak
    {
        float minimum = 0.0f;
        float maximum = 0.0f;
    };

    struct Summary
    {
        double sampleRate = 0.0;
        juce::int64 totalFrames = 0;
        juce::int64 framesPerPeak = 1;
        std::vector<std::vector<Peak>> channels;
    };

    struct Snapshot
    {
        State state = State::empty;
        juce::File file;
        juce::String message;
        std::shared_ptr<const Summary> summary;
    };

    WaveformData();
    ~WaveformData();

    void loadFile(const juce::File& file);
    Snapshot getSnapshot() const;

    std::function<void()> onStateChanged;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformData)
};
}
