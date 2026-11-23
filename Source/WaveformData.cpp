#include "WaveformData.h"
#include "ValidatedWavReader.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace wavecrate
{
namespace
{
constexpr juce::int64 maximumPeaksPerChannel = 4096;
constexpr int readBufferFrames = 4096;
constexpr std::size_t maximumCachedSummaries = 8;

struct CacheKey
{
    juce::File file;
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type modified;

    bool operator==(const CacheKey&) const = default;
};

std::optional<CacheKey> readCacheKey(const juce::File& file, juce::String& error)
{
    const std::filesystem::path path(file.getFullPathName().toWideCharPointer());
    std::error_code status;

    if (!std::filesystem::is_regular_file(path, status) || status)
    {
        error = "Cannot read this WAV. Check that the file and drive are available.";
        return {};
    }

    const auto bytes = std::filesystem::file_size(path, status);

    if (status)
    {
        error = "Cannot read the WAV size. Check drive access and select the file again.";
        return {};
    }

    const auto modified = std::filesystem::last_write_time(path, status);

    if (status)
    {
        error = "Cannot read the WAV modification time. Check drive access and select the file again.";
        return {};
    }

    return CacheKey { file, bytes, modified };
}
}

struct WaveformData::Impl final : juce::AsyncUpdater
{
    explicit Impl(WaveformData& data) : owner(data)
    {
        try
        {
            worker = std::thread([this] { run(); });
        }
        catch (const std::exception&)
        {
            workerAvailable = false;
        }
    }

    ~Impl() override
    {
        {
            const std::lock_guard lock(mutex);
            shuttingDown = true;
            ++selection;
        }

        changed.notify_one();

        // The worker never waits for the message thread. Its reader and referenced
        // atomics remain alive until cancellation has completed.
        if (worker.joinable())
            worker.join();

        cancelPendingUpdate();
    }

    void loadFile(const juce::File& file)
    {
        {
            const std::lock_guard lock(mutex);
            ++selection;
            requestedFile = file;
            snapshot = {};
            snapshot.file = file;
            snapshot.state = file == juce::File() ? State::empty : State::loading;
            snapshot.message = file == juce::File() ? juce::String() : "Loading waveform...";

            if (file != juce::File() && !workerAvailable)
            {
                snapshot.state = State::error;
                snapshot.message = "The waveform worker could not start. Restart WaveCrate.";
            }
        }

        changed.notify_one();
        triggerAsyncUpdate();
    }

    Snapshot getSnapshot() const
    {
        const std::lock_guard lock(mutex);
        return snapshot;
    }

private:
    struct Request
    {
        juce::File file;
        std::uint64_t selection = 0;
    };

    struct CacheEntry
    {
        CacheKey key;
        std::shared_ptr<const Summary> summary;
    };

    bool cancelled(const Request& request) const
    {
        return selection.load() != request.selection;
    }

    std::shared_ptr<const Summary> generate(const Request& request, juce::String& error)
    {
        std::atomic<bool> readFailed { false };
        auto reader = createValidatedWavReader(request.file, selection, request.selection, readFailed, error);

        if (reader == nullptr || cancelled(request))
            return {};

        auto result = std::make_shared<Summary>();
        result->sampleRate = reader->sampleRate;
        result->totalFrames = reader->lengthInSamples;
        result->framesPerPeak = 1 + (result->totalFrames - 1) / maximumPeaksPerChannel;
        const auto peakCount = static_cast<std::size_t>(
            1 + (result->totalFrames - 1) / result->framesPerPeak);
        const Peak initial { std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity() };
        result->channels.resize(reader->numChannels, std::vector<Peak>(peakCount, initial));
        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), readBufferFrames);

        for (juce::int64 position = 0; position < result->totalFrames;)
        {
            if (cancelled(request))
                return {};

            const auto count = static_cast<int>(
                std::min<juce::int64>(readBufferFrames, result->totalFrames - position));

            if (!reader->read(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), position, count)
                || readFailed.load())
            {
                error = "The WAV became unreadable or was truncated. Check the drive and select it again.";
                return {};
            }

            for (int offset = 0; offset < count;)
            {
                const auto frame = position + offset;
                const auto peakIndex = static_cast<std::size_t>(frame / result->framesPerPeak);
                const auto segment = static_cast<int>(std::min<juce::int64>(
                    count - offset, result->framesPerPeak - frame % result->framesPerPeak));

                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    auto& peak = result->channels[static_cast<std::size_t>(channel)][peakIndex];
                    const auto* samples = buffer.getReadPointer(channel, offset);

                    for (int index = 0; index < segment; ++index)
                    {
                        const auto value = std::isfinite(samples[index]) ? samples[index] : 0.0f;
                        peak.minimum = std::min(peak.minimum, value);
                        peak.maximum = std::max(peak.maximum, value);
                    }
                }

                offset += segment;
            }

            position += count;
        }

        return cancelled(request) ? std::shared_ptr<const Summary>() : result;
    }

    std::shared_ptr<const Summary> load(const Request& request, juce::String& error)
    {
        if (cancelled(request))
            return {};

        const auto key = readCacheKey(request.file, error);
        cache.remove_if([&](const CacheEntry& entry)
        {
            return entry.key.file == request.file && (!key || entry.key != *key);
        });

        if (!key || cancelled(request))
            return {};

        const auto cached = std::find_if(cache.begin(), cache.end(), [&](const CacheEntry& entry)
        {
            return entry.key == *key;
        });

        if (cached != cache.end())
        {
            cache.splice(cache.begin(), cache, cached);
            return cache.front().summary;
        }

        auto result = generate(request, error);

        if (result == nullptr || cancelled(request))
            return {};

        const auto after = readCacheKey(request.file, error);

        if (!after || *after != *key)
        {
            if (error.isEmpty())
                error = "The WAV changed while its waveform was loading. Select it again.";

            return {};
        }

        if (cancelled(request))
            return {};

        if (cache.size() >= maximumCachedSummaries)
            cache.pop_back();

        cache.push_front({ *key, result });
        return result;
    }

    void run()
    {
        std::uint64_t processedSelection = 0;

        for (;;)
        {
            Request request;

            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [this, processedSelection]
                {
                    return shuttingDown || selection.load() != processedSelection;
                });

                if (shuttingDown)
                    return;

                request = { requestedFile, selection.load() };
            }

            processedSelection = request.selection;

            if (request.file == juce::File())
                continue;

            juce::String error;
            std::shared_ptr<const Summary> result;

            try
            {
                result = load(request, error);
            }
            catch (const std::exception&)
            {
                error = "The waveform could not be generated safely. Select another file or restart WaveCrate.";
            }

            {
                const std::lock_guard lock(mutex);

                if (cancelled(request))
                    continue;

                snapshot.summary = std::move(result);
                snapshot.state = snapshot.summary != nullptr ? State::ready : State::error;
                snapshot.message = snapshot.summary != nullptr ? juce::String()
                    : error.isNotEmpty() ? error : "The waveform could not be read. Select another WAV.";
            }

            triggerAsyncUpdate();
        }
    }

    void handleAsyncUpdate() override
    {
        const auto callback = owner.onStateChanged;

        if (callback)
            callback();
    }

    WaveformData& owner;
    mutable std::mutex mutex;
    std::condition_variable changed;
    Snapshot snapshot;
    juce::File requestedFile;
    std::atomic<std::uint64_t> selection { 0 };
    bool shuttingDown = false;
    bool workerAvailable = true;
    std::list<CacheEntry> cache;
    std::thread worker;
};

WaveformData::WaveformData() : impl(std::make_unique<Impl>(*this)) {}
WaveformData::~WaveformData() = default;
void WaveformData::loadFile(const juce::File& file) { impl->loadFile(file); }
WaveformData::Snapshot WaveformData::getSnapshot() const { return impl->getSnapshot(); }
}
