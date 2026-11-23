#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace wavecrate
{
inline juce::String formatSampleDuration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return "-";

    const auto milliseconds = static_cast<juce::int64>(std::round(seconds * 1000.0));
    return juce::String(milliseconds / 60000) + ":"
        + juce::String((milliseconds / 1000) % 60).paddedLeft('0', 2) + "."
        + juce::String(milliseconds % 1000).paddedLeft('0', 3);
}

inline juce::String formatSampleSize(juce::int64 bytes)
{
    return bytes >= 0 ? juce::File::descriptionOfSizeInBytes(bytes) : juce::String("-");
}

inline juce::String formatSampleRate(double sampleRate)
{
    return sampleRate > 0.0 && std::isfinite(sampleRate)
        ? juce::String(sampleRate / 1000.0, 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + " kHz"
        : juce::String("-");
}

inline juce::String formatSampleBitRate(double sampleRate, unsigned int channels, unsigned int bitDepth)
{
    return sampleRate > 0.0 && std::isfinite(sampleRate) && channels > 0 && bitDepth > 0
        ? juce::String(sampleRate * channels * bitDepth / 1000.0, 1) + " kb/s"
        : juce::String("-");
}
}
