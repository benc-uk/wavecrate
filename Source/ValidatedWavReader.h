#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace wavecrate
{
// Call off the message/audio threads. Both atomics must outlive the returned
// reader. A changed selection cancels reads; readFailed also detects short reads
// that the WAV decoder may otherwise replace with silence.
std::unique_ptr<juce::AudioFormatReader> createValidatedWavReader(
    const juce::File& file, const std::atomic<std::uint64_t>& selection,
    std::uint64_t expectedSelection, std::atomic<bool>& readFailed, juce::String& error,
    juce::int64* fileSizeBytes = nullptr);
}
