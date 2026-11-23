#include "AudioPreview.h"
#include "ValidatedWavReader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>

namespace wavecrate
{
namespace
{
constexpr int outputBlockSize = 512;
constexpr int readAheadSamples = 65536;
constexpr double defaultOutputRate = 48000.0;

struct WavDescription
{
    juce::int64 dataOffset = 0;
    juce::int64 dataBytes = 0;
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    unsigned int bits = 0;
    unsigned int format = 0;
};

bool chunkIs(const std::array<unsigned char, 8>& chunk, const char* name)
{
    return std::memcmp(chunk.data(), name, 4) == 0;
}

// Walk chunk boundaries without decoding metadata or trusting claimed allocation sizes.
juce::String inspectWav(juce::FileInputStream& stream, WavDescription& description,
                        const std::atomic<std::uint64_t>& selection,
                        std::uint64_t expectedSelection)
{
    const auto fileSize = stream.getTotalLength();
    std::array<unsigned char, 12> header {};

    if (fileSize < 12 || stream.read(header.data(), 12) != 12
        || std::memcmp(header.data() + 8, "WAVE", 4) != 0)
    {
        return "This is not a valid WAV header. Select another file or export it again.";
    }

    const auto rf64 = std::memcmp(header.data(), "RF64", 4) == 0;

    if (!rf64 && std::memcmp(header.data(), "RIFF", 4) != 0)
        return "Unsupported WAV container. Export a little-endian PCM or 32-bit float WAV.";

    auto containerEnd = static_cast<juce::int64>(juce::ByteOrder::littleEndianInt(header.data() + 4)) + 8;
    juce::int64 rf64DataBytes = 0;

    if (rf64)
    {
        std::array<unsigned char, 36> sizes {};

        if (stream.read(sizes.data(), 36) != 36 || std::memcmp(sizes.data(), "ds64", 4) != 0
            || juce::ByteOrder::littleEndianInt(sizes.data() + 4) < 28)
        {
            return "The RF64 size header is missing or truncated. Export the WAV again.";
        }

        const auto riffSize = juce::ByteOrder::littleEndianInt64(sizes.data() + 8);
        const auto dataSize = juce::ByteOrder::littleEndianInt64(sizes.data() + 16);
        const auto sizeChunkLength = juce::ByteOrder::littleEndianInt(sizes.data() + 4);

        if (riffSize > static_cast<juce::uint64>(fileSize - 8)
            || dataSize > static_cast<juce::uint64>(fileSize)
            || juce::ByteOrder::littleEndianInt(sizes.data() + 32) != 0
            || static_cast<juce::int64>(sizeChunkLength) > fileSize - 20)
        {
            return "Invalid or unsupported RF64 sizes. Export a standard PCM or float WAV.";
        }

        containerEnd = static_cast<juce::int64>(riffSize) + 8;
        rf64DataBytes = static_cast<juce::int64>(dataSize);
        stream.setPosition(20 + static_cast<juce::int64>(sizeChunkLength) + (sizeChunkLength & 1u));
    }

    if (containerEnd < stream.getPosition() || containerEnd > fileSize)
        return "The WAV is truncated or its RIFF size is invalid. Copy or export the complete file.";

    bool foundFormat = false;
    bool foundData = false;
    int chunkCount = 0;

    while (stream.getPosition() < containerEnd)
    {
        if (selection.load() != expectedSelection)
            return {};

        if (++chunkCount > 4096)
            return "This WAV has too many header chunks. Export a simpler PCM or float WAV.";

        std::array<unsigned char, 8> chunk {};

        if (containerEnd - stream.getPosition() < 8 || stream.read(chunk.data(), 8) != 8)
            return "A WAV chunk header is truncated. Copy or export the complete file.";

        const auto start = stream.getPosition();
        auto length = static_cast<juce::int64>(juce::ByteOrder::littleEndianInt(chunk.data() + 4));

        if (rf64 && chunkIs(chunk, "data") && length == 0xffffffffLL)
            length = rf64DataBytes;

        if (length < 0 || length > containerEnd - start)
            return "A WAV chunk extends beyond the file. Copy or export the complete file.";

        if (chunkIs(chunk, "fmt "))
        {
            if (foundFormat || foundData || length < 16)
                return "The WAV format header is invalid. Export a PCM or 32-bit float WAV.";

            std::array<unsigned char, 40> format {};
            const auto bytesToRead = static_cast<int>(std::min<juce::int64>(length, 40));

            if (stream.read(format.data(), bytesToRead) != bytesToRead)
                return "The WAV format header cannot be read. Copy or export the file again.";

            description.format = juce::ByteOrder::littleEndianShort(format.data());
            description.channels = juce::ByteOrder::littleEndianShort(format.data() + 2);
            description.sampleRate = juce::ByteOrder::littleEndianInt(format.data() + 4);
            description.bits = juce::ByteOrder::littleEndianShort(format.data() + 14);
            const auto blockAlignment = juce::ByteOrder::littleEndianShort(format.data() + 12);
            const auto byteRate = juce::ByteOrder::littleEndianInt(format.data() + 8);

            if (description.channels < 1 || description.channels > 2)
                return "Preview supports mono or stereo WAVs. Export a mono or stereo copy of this file.";

            if (description.sampleRate < 8000 || description.sampleRate > 384000)
                return "Unsupported WAV sample rate. Export at a rate from 8000 to 384000 Hz.";

            if (description.format == 0xfffe)
            {
                constexpr std::array<unsigned char, 12> guidTail
                    { 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 };
                const auto validBits = juce::ByteOrder::littleEndianShort(format.data() + 18);

                if (length < 40 || juce::ByteOrder::littleEndianShort(format.data() + 16) < 22
                    || validBits == 0 || validBits > description.bits
                    || std::memcmp(format.data() + 28, guidTail.data(), guidTail.size()) != 0)
                {
                    return "Unsupported extensible WAV header. Export a PCM or 32-bit float WAV.";
                }

                description.format = juce::ByteOrder::littleEndianInt(format.data() + 24);
            }

            const auto pcm = description.format == 1
                && (description.bits == 8 || description.bits == 16
                    || description.bits == 24 || description.bits == 32);
            const auto floatingPoint = description.format == 3 && description.bits == 32;

            if (!pcm && !floatingPoint)
                return "Unsupported WAV encoding. Use 8/16/24/32-bit PCM or 32-bit float WAV.";

            const auto bytesPerFrame = description.channels * (description.bits / 8);

            if (blockAlignment != bytesPerFrame || byteRate != description.sampleRate * bytesPerFrame)
                return "The WAV frame size or byte rate is invalid. Export the WAV again.";

            foundFormat = true;
        }
        else if (chunkIs(chunk, "data"))
        {
            if (!foundFormat || foundData)
                return "The WAV audio chunk is invalid. Export a WAV with one audio data chunk.";

            const auto bytesPerFrame = description.channels * (description.bits / 8);

            if (length == 0 || length % bytesPerFrame != 0)
                return "The WAV has no complete audio frames or is truncated. Export the WAV again.";

            description.dataOffset = start;
            description.dataBytes = length;
            foundData = true;
        }

        const auto end = start + length;
        const auto paddedEnd = end + ((end < containerEnd) ? (length & 1) : 0);

        if (!stream.setPosition(paddedEnd))
            return "The WAV cannot be read. Check drive access or copy the file to a local folder.";
    }

    if (!foundFormat || !foundData)
        return "The WAV is missing its format or audio data. Export a complete WAV file.";

    return {};
}

// JUCE sees only a validated minimal header and the original audio data. Unused
// metadata is never parsed, including untrusted embedded counts and text lengths.
class PreviewWavStream final : public juce::InputStream
{
public:
    PreviewWavStream(std::unique_ptr<juce::FileInputStream> inputStream,
                     const WavDescription& description,
                     const std::atomic<std::uint64_t>& currentSelection,
                     std::uint64_t expectedSelection, std::atomic<bool>& failureFlag)
        : input(std::move(inputStream)), dataOffset(description.dataOffset),
          dataBytes(description.dataBytes), selection(currentSelection),
          expected(expectedSelection), failed(failureFlag)
    {
        juce::MemoryOutputStream output(header, false);
        const auto rf64 = dataBytes > 0xffffffffLL - 36;
        output.write(rf64 ? "RF64" : "RIFF", 4);
        output.writeInt(rf64 ? -1 : static_cast<int>(dataBytes + 36));
        output.write("WAVE", 4);

        if (rf64)
        {
            output.write("ds64", 4);
            output.writeInt(28);
            output.writeInt64(dataBytes + 72);
            output.writeInt64(dataBytes);
            output.writeInt64(dataBytes / (description.channels * description.bits / 8));
            output.writeInt(0);
        }

        output.write("fmt ", 4);
        output.writeInt(16);
        output.writeShort(static_cast<short>(description.format));
        output.writeShort(static_cast<short>(description.channels));
        output.writeInt(static_cast<int>(description.sampleRate));
        const auto frameBytes = description.channels * description.bits / 8;
        output.writeInt(static_cast<int>(description.sampleRate * frameBytes));
        output.writeShort(static_cast<short>(frameBytes));
        output.writeShort(static_cast<short>(description.bits));
        output.write("data", 4);
        output.writeInt(rf64 ? -1 : static_cast<int>(dataBytes));
    }

    juce::int64 getTotalLength() override { return static_cast<juce::int64>(header.getSize()) + dataBytes; }
    juce::int64 getPosition() override { return position; }
    bool isExhausted() override { return position >= getTotalLength() || selection.load() != expected; }

    bool setPosition(juce::int64 newPosition) override
    {
        position = std::clamp<juce::int64>(newPosition, 0, getTotalLength());
        return position == newPosition;
    }

    int read(void* destination, int requestedBytes) override
    {
        if (requestedBytes <= 0 || selection.load() != expected)
            return 0;

        auto* target = static_cast<unsigned char*>(destination);
        const auto headerSize = static_cast<juce::int64>(header.getSize());
        auto remaining = static_cast<int>(std::min<juce::int64>(requestedBytes, getTotalLength() - position));
        int copied = 0;

        if (position < headerSize)
        {
            copied = static_cast<int>(std::min<juce::int64>(remaining, headerSize - position));
            std::memcpy(target, static_cast<const unsigned char*>(header.getData()) + position,
                        static_cast<std::size_t>(copied));
            position += copied;
            remaining -= copied;
        }

        if (remaining > 0)
        {
            if (!input->setPosition(dataOffset + position - headerSize))
            {
                failed.store(true);
                return copied;
            }

            const auto count = input->read(target + copied, remaining);

            if (count != remaining)
                failed.store(true);

            const auto readBytes = std::max(0, count);
            position += readBytes;
            copied += readBytes;
        }

        return copied;
    }

private:
    std::unique_ptr<juce::FileInputStream> input;
    juce::MemoryBlock header;
    juce::int64 dataOffset;
    juce::int64 dataBytes;
    juce::int64 position = 0;
    const std::atomic<std::uint64_t>& selection;
    std::uint64_t expected;
    std::atomic<bool>& failed;
};

juce::Range<juce::int64> clampRegion(juce::Range<juce::int64> region, juce::int64 totalFrames)
{
    auto start = std::max<juce::int64>(0, region.getStart());
    auto end = std::max(start, region.getEnd());

    if (totalFrames > 0)
    {
        start = std::min(start, totalFrames);
        end = std::min(end, totalFrames);
    }

    return start < end ? juce::Range<juce::int64>(start, end) : juce::Range<juce::int64>();
}

// Plays source frames [rangeStart, rangeEnd). The transport sees rangeEnd as the
// total length, so it stops there unless looping. Reads wrap to rangeStart while
// looping. A preloaded head of the loop range covers the read-ahead miss after a wrap.
class CachedPreviewSource final : public juce::PositionableAudioSource
{
public:
    explicit CachedPreviewSource(juce::BufferingAudioReader& input)
        : reader(input), rangeEnd(input.lengthInSamples)
    {
    }

    void prepareToPlay(int, double) override {}
    void releaseResources() override {}
    void setNextReadPosition(juce::int64 next) override { position = next; }
    juce::int64 getNextReadPosition() const override { return position; }
    juce::int64 getTotalLength() const override { return rangeEnd; }
    bool isLooping() const override { return looping.load(); }
    void setLooping(bool shouldLoop) override { looping.store(shouldLoop); }

    juce::int64 getRangeStart() const { return rangeStart; }
    bool hasWrapped() const { return wrapped.load(); }

    // Only call while no audio callback can reference this source.
    void setRange(juce::int64 start, juce::int64 end)
    {
        rangeStart = start;
        rangeEnd = end;
        wrapped.store(false);
        starved.store(false);
    }

    bool hasHead(juce::int64 start, int frames) const
    {
        return headStart == start && headFrames == frames;
    }

    // Only call while no audio callback can reference this source.
    juce::AudioBuffer<float>& beginHead(int frames)
    {
        headFrames = 0;
        head.setSize(static_cast<int>(reader.numChannels), std::max(1, frames), false, false, true);
        return head;
    }

    void commitHead(juce::int64 start, int frames)
    {
        headStart = start;
        headFrames = frames;
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();
        const auto loopLength = rangeEnd - rangeStart;
        const auto loop = looping.load() && loopLength > 0;
        int done = 0;

        while (done < info.numSamples)
        {
            if (loop && position >= rangeEnd)
            {
                position = rangeStart + (position - rangeStart) % loopLength;
                wrapped.store(true);
            }

            const auto remaining = info.numSamples - done;

            if (position < rangeStart)
            {
                const auto gap = static_cast<int>(std::min<juce::int64>(rangeStart - position, remaining));
                position += gap;
                done += gap;
                continue;
            }

            if (position >= rangeEnd)
            {
                position += remaining;
                break;
            }

            auto count = static_cast<int>(std::min<juce::int64>(rangeEnd - position, remaining));
            const auto headEnd = headStart + headFrames;
            const auto inHead = position >= headStart && position < headEnd;

            if (inHead)
                count = static_cast<int>(std::min<juce::int64>(count, headEnd - position));

            const auto offset = info.startSample + done;

            if (!reader.read(info.buffer, offset, count, position, true, true))
            {
                if (inHead)
                {
                    copyFromHead(*info.buffer, offset, count, static_cast<int>(position - headStart));
                }
                else
                {
                    starved.store(true);
                    info.buffer->clear(offset, count);
                }
            }

            for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            {
                auto* samples = info.buffer->getWritePointer(channel, offset);

                for (int index = 0; index < count; ++index)
                    samples[index] = std::isfinite(samples[index]) ? std::clamp(samples[index], -1.0f, 1.0f) : 0.0f;
            }

            position += count;
            done += count;
        }
    }

    std::atomic<bool> starved { false };

private:
    void copyFromHead(juce::AudioBuffer<float>& destination, int offset, int count, int headOffset) const
    {
        const auto headChannels = head.getNumChannels();

        for (int channel = 0; channel < destination.getNumChannels(); ++channel)
        {
            if (channel < 2 && headChannels > 0)
                destination.copyFrom(channel, offset, head, std::min(channel, headChannels - 1), headOffset, count);
            else
                destination.clear(channel, offset, count);
        }
    }

    juce::BufferingAudioReader& reader;
    juce::int64 position = 0;
    juce::int64 rangeStart = 0;
    juce::int64 rangeEnd = 0;
    std::atomic<bool> looping { false };
    std::atomic<bool> wrapped { false };
    juce::AudioBuffer<float> head;
    juce::int64 headStart = 0;
    int headFrames = 0;
};

struct PreparedPreview
{
    std::uint64_t selection = 0;
    juce::int64 fileSizeBytes = -1;
    unsigned int sourceBitDepth = 0;
    juce::int64 startFrame = 0;
    juce::int64 outputFramesPlayed = 0;
    double outputRate = defaultOutputRate;
    std::atomic<bool> readFailed { false };
    std::unique_ptr<juce::BufferingAudioReader> reader;
    std::unique_ptr<CachedPreviewSource> source;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> output { 2, outputBlockSize };

    ~PreparedPreview()
    {
        transport.setSource(nullptr);
    }

    double positionFrame() const
    {
        auto frame = static_cast<double>(startFrame)
            + static_cast<double>(outputFramesPlayed) * reader->sampleRate / outputRate;
        const auto loopStart = static_cast<double>(source->getRangeStart());
        const auto loopEnd = static_cast<double>(source->getTotalLength());
        const auto loopLength = loopEnd - loopStart;

        if ((source->isLooping() || source->hasWrapped()) && loopLength > 0.0 && frame >= loopEnd)
            frame = loopStart + std::fmod(frame - loopStart, loopLength);

        return frame;
    }

    double positionSeconds() const
    {
        return positionFrame() / reader->sampleRate;
    }
};

class SharedOutputDeviceManager final : public juce::AudioDeviceManager
{
public:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        if (auto* type = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared))
            types.add(type);
    }
};
}

std::unique_ptr<juce::AudioFormatReader> createValidatedWavReader(
    const juce::File& file, const std::atomic<std::uint64_t>& selection,
    std::uint64_t expectedSelection, std::atomic<bool>& readFailed, juce::String& error,
    juce::int64* fileSizeBytes)
{
    error.clear();
    if (fileSizeBytes != nullptr)
        *fileSizeBytes = -1;

    if (selection.load() != expectedSelection)
        return {};

    if (!file.hasFileExtension(".wav"))
    {
        error = "Only .wav files can be previewed. Select a PCM or 32-bit float WAV.";
        return {};
    }

    auto input = std::make_unique<juce::FileInputStream>(file);

    if (!input->openedOk())
    {
        error = "Cannot open this WAV. Check that the file and drive are available and readable. "
            + input->getStatus().getErrorMessage();
        return {};
    }

    const auto sourceFileSize = input->getTotalLength();
    WavDescription description;
    error = inspectWav(*input, description, selection, expectedSelection);

    if (error.isNotEmpty() || selection.load() != expectedSelection)
        return {};

    auto stream = std::make_unique<PreviewWavStream>(std::move(input), description,
                                                   selection, expectedSelection, readFailed);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> decoded(format.createReaderFor(stream.release(), true));

    if (decoded == nullptr || decoded->lengthInSamples <= 0 || readFailed.load())
    {
        error = "The WAV audio cannot be decoded. Export a PCM or 32-bit float copy.";
        return {};
    }

    if (fileSizeBytes != nullptr)
        *fileSizeBytes = sourceFileSize;
    return decoded;
}

struct AudioPreview::Impl final : juce::AudioIODeviceCallback, juce::AsyncUpdater, juce::Timer
{
    explicit Impl(AudioPreview& preview) : owner(preview)
    {
        snapshot.deviceMessage = "Audio output has not been initialised.";
        readAheadStarted = readAhead.startThread();

        try
        {
            loader = std::thread([this] { runLoader(); });
        }
        catch (const std::exception&)
        {
            loaderAvailable = false;
        }

        startTimerHz(30);
    }

    ~Impl() override
    {
        stopTimer();

        {
            const std::lock_guard lock(mutex);
            shuttingDown = true;
            active = nullptr;
            ++selection;
            ++revision;
        }

        changed.notify_one();
        devices.removeAudioCallback(this);
        devices.closeAudioDevice();

        // Neither background thread takes a MessageManagerLock or waits for a UI callback.
        if (loader.joinable())
            loader.join();

        readAhead.stopThread(-1);
        cancelPendingUpdate();
    }

    void initialiseAudio()
    {
        if (!callbackRegistered)
        {
            devices.addAudioCallback(this);
            callbackRegistered = true;
        }

        const auto error = devices.initialiseWithDefaultDevices(0, 2);

        {
            const std::lock_guard lock(mutex);

            if (error.isNotEmpty() || devices.getCurrentAudioDevice() == nullptr)
            {
                snapshot.audioAvailable = false;
                snapshot.deviceMessage = "Audio output unavailable. Check Windows sound settings and restart WaveCrate.";

                if (error.isNotEmpty())
                    snapshot.deviceMessage += " " + error;
            }
        }

        triggerAsyncUpdate();
    }

    void loadFile(const juce::File& file)
    {
        {
            const std::lock_guard lock(mutex);
            active = nullptr;
            ++selection;
            ++revision;
            requestedFile = file;
            requestedPlay = false;
            requestedFrame = 0;
            totalFrames = 0;
            requestedRegion = {};
            restartAtEnd = false;
            const auto available = snapshot.audioAvailable;
            const auto deviceMessage = snapshot.deviceMessage;
            snapshot = {};
            snapshot.audioAvailable = available;
            snapshot.deviceMessage = deviceMessage;
            snapshot.file = file;
            snapshot.state = file == juce::File() ? State::empty : State::loading;
            snapshot.message = file == juce::File() ? juce::String() : "Loading WAV...";

            if (file != juce::File() && !loaderAvailable)
            {
                snapshot.state = State::error;
                snapshot.message = "The preview worker could not start. Restart WaveCrate.";
            }
        }

        changed.notify_one();
        triggerAsyncUpdate();
    }

    void play()
    {
        {
            const std::lock_guard lock(mutex);

            if (snapshot.state == State::empty || snapshot.state == State::error)
                return;

            if (snapshot.state == State::playing)
            {
                if (active != nullptr && active->transport.isPlaying())
                    return;

                snapshot.state = State::ready;
                requestedFrame = 0;
            }

            if (!requestedRegion.isEmpty() && totalFrames > 0)
            {
                if (!requestedRegion.contains(requestedFrame))
                    requestedFrame = requestedRegion.getStart();
            }
            else if (totalFrames > 0 && requestedFrame >= totalFrames)
            {
                requestedFrame = 0;
            }

            active = nullptr;
            requestedPlay = true;
            restartAtEnd = true;
            snapshot.positionSeconds = snapshot.sampleRate > 0.0
                ? static_cast<double>(requestedFrame) / snapshot.sampleRate : 0.0;
            ++revision;
        }

        changed.notify_one();
        triggerAsyncUpdate();
    }

    void seekToFrame(juce::int64 frame)
    {
        {
            const std::lock_guard lock(mutex);

            if (snapshot.state == State::empty || snapshot.state == State::error)
                return;

            requestedPlay = requestedPlay
                || (active != nullptr && snapshot.state == State::playing
                    && active->transport.isPlaying());
            active = nullptr;
            requestedFrame = totalFrames > 0
                ? std::clamp<juce::int64>(frame, 0, totalFrames)
                : std::max<juce::int64>(0, frame);
            restartAtEnd = false;
            snapshot.positionSeconds = snapshot.sampleRate > 0.0
                ? static_cast<double>(requestedFrame) / snapshot.sampleRate : 0.0;

            if (snapshot.state != State::loading)
            {
                snapshot.state = State::ready;
                snapshot.message = "Seeking...";
            }

            ++revision;
        }

        changed.notify_one();
        triggerAsyncUpdate();
    }

    void stop()
    {
        {
            const std::lock_guard lock(mutex);
            active = nullptr;
            requestedPlay = false;
            requestedFrame = 0;
            restartAtEnd = false;
            snapshot.positionSeconds = 0.0;

            if (snapshot.state == State::playing || snapshot.state == State::ready)
            {
                snapshot.state = State::ready;
                snapshot.message = "Rewinding...";
            }

            ++revision;
        }

        changed.notify_one();
        triggerAsyncUpdate();
    }

    void setRegion(juce::Range<juce::int64> frames)
    {
        bool restart = false;

        {
            const std::lock_guard lock(mutex);

            if (snapshot.state == State::empty || snapshot.state == State::error)
                return;

            const auto previous = requestedRegion;
            requestedRegion = clampRegion(frames, totalFrames);
            const auto activePlaying = active != nullptr && snapshot.state == State::playing
                && active->transport.isPlaying();

            if ((requestedPlay || activePlaying) && requestedRegion != previous)
            {
                if (!requestedRegion.isEmpty())
                {
                    requestedFrame = requestedRegion.getStart();
                }
                else if (activePlaying)
                {
                    const auto frame = static_cast<juce::int64>(active->positionFrame());
                    requestedFrame = totalFrames > 0 ? std::clamp<juce::int64>(frame, 0, totalFrames)
                                                     : std::max<juce::int64>(0, frame);
                }

                active = nullptr;
                requestedPlay = true;
                restartAtEnd = false;
                snapshot.positionSeconds = snapshot.sampleRate > 0.0
                    ? static_cast<double>(requestedFrame) / snapshot.sampleRate : 0.0;

                if (snapshot.state != State::loading)
                {
                    snapshot.state = State::ready;
                    snapshot.message = "Seeking...";
                }

                ++revision;
                restart = true;
            }
        }

        if (restart)
            changed.notify_one();

        triggerAsyncUpdate();
    }

    void setLoopEnabled(bool shouldLoop)
    {
        {
            const std::lock_guard lock(mutex);
            loopEnabled = shouldLoop;

            if (active != nullptr)
                active->source->setLooping(shouldLoop);
        }

        triggerAsyncUpdate();
    }

    Snapshot getSnapshot() const
    {
        const std::lock_guard lock(mutex);
        auto result = snapshot;
        result.playRequested = requestedPlay;
        result.totalFrames = totalFrames;
        result.region = requestedRegion;
        result.loopEnabled = loopEnabled;

        if (requestedPlay)
            result.message = "Preparing playback. Press Stop to cancel.";

        if (active != nullptr && result.state == State::playing)
        {
            result.positionSeconds = std::clamp(active->positionSeconds(), 0.0, result.durationSeconds);

            if (!active->transport.isPlaying())
            {
                result.state = State::ready;
                result.positionSeconds = std::clamp(
                    static_cast<double>(active->source->getTotalLength()) / active->reader->sampleRate,
                    0.0, result.durationSeconds);
                result.message = finishedMessage();
            }
            else
            {
                result.message = playingMessage();
            }
        }

        return result;
    }

private:
    juce::String playingMessage() const
    {
        const auto hasRegion = !requestedRegion.isEmpty();

        if (loopEnabled)
            return hasRegion ? "Looping selection." : "Looping whole sample.";

        return hasRegion ? "Playing selection." : "Playing.";
    }

    juce::String finishedMessage() const
    {
        return requestedRegion.isEmpty() ? "Finished. Press Play to preview from the start."
                                         : "Finished. Press Play to preview the selection again.";
    }

    struct Request
    {
        juce::File file;
        std::uint64_t selection = 0;
        std::uint64_t revision = 0;
        double sampleRate = defaultOutputRate;
        bool play = false;
        bool audioAvailable = false;
        juce::int64 frame = 0;
        bool restartAtEnd = false;
        juce::Range<juce::int64> region;
    };

    bool superseded(const Request& request) const
    {
        return revision.load() != request.revision;
    }

    std::unique_ptr<PreparedPreview> openFile(const Request& request, juce::String& error)
    {
        if (!readAheadStarted)
        {
            error = "The audio read-ahead worker could not start. Restart WaveCrate.";
            return {};
        }

        auto prepared = std::make_unique<PreparedPreview>();
        prepared->selection = request.selection;
        auto decoded = createValidatedWavReader(request.file, selection, request.selection,
                                                prepared->readFailed, error, &prepared->fileSizeBytes);

        if (decoded == nullptr)
            return {};

        prepared->sourceBitDepth = decoded->bitsPerSample;
        prepared->reader = std::make_unique<juce::BufferingAudioReader>(decoded.release(), readAhead, readAheadSamples);
        prepared->reader->setReadTimeout(0);
        prepared->source = std::make_unique<CachedPreviewSource>(*prepared->reader);
        return prepared;
    }

    bool readFrames(PreparedPreview& prepared, const Request& request, juce::AudioBuffer<float>& buffer,
                    int frames, juce::int64 start, juce::String& error)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;

        while (!superseded(request))
        {
            if (prepared.readFailed.load())
            {
                error = "The WAV became unreadable or was truncated. Check the drive and select the file again.";
                return false;
            }

            if (frames == 0
                || (prepared.reader->read(&buffer, 0, frames, start, true, true)
                    && !prepared.readFailed.load()))
                return true;

            if (juce::Time::getMillisecondCounterHiRes() >= deadline)
            {
                error = "Timed out reading the WAV. Copy it to a local folder and try again.";
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        return false;
    }

    bool prepare(PreparedPreview& prepared, const Request& request, juce::String& error)
    {
        // No callback can reference this voice while a command is being prepared.
        // Preparing an empty transport first sets the device rate before JUCE creates
        // its resampler, so its callback buffer is sized for the corrected ratio.
        prepared.transport.setSource(nullptr);
        prepared.transport.prepareToPlay(outputBlockSize, request.sampleRate);
        prepared.transport.setSource(prepared.source.get(), 0, nullptr, prepared.reader->sampleRate, 2);
        prepared.transport.setGain(0.5f);

        const auto length = prepared.reader->lengthInSamples;
        const auto region = clampRegion(request.region, length);
        const auto rangeStart = region.isEmpty() ? juce::int64 { 0 } : region.getStart();
        const auto rangeEnd = region.isEmpty() ? length : region.getEnd();
        prepared.source->setRange(rangeStart, rangeEnd);

        // Looping may be enabled during playback, so always keep the loop start cached.
        const auto headFrames = static_cast<int>(std::min<juce::int64>(rangeEnd - rangeStart, readAheadSamples));

        if (!prepared.source->hasHead(rangeStart, headFrames))
        {
            auto& head = prepared.source->beginHead(headFrames);

            if (!readFrames(prepared, request, head, headFrames, rangeStart, error))
                return false;

            prepared.source->commitHead(rangeStart, headFrames);
        }

        prepared.startFrame = std::clamp<juce::int64>(request.frame, 0, length);

        if (request.play)
        {
            if (!region.isEmpty())
            {
                if (!region.contains(prepared.startFrame))
                    prepared.startFrame = rangeStart;
            }
            else if (request.restartAtEnd && prepared.startFrame == length)
            {
                prepared.startFrame = 0;
            }
        }

        // setSource recreated and flushed the resampler. Set its input directly to
        // avoid a source -> output -> source frame conversion truncating the seek.
        prepared.source->setNextReadPosition(prepared.startFrame);
        prepared.outputFramesPlayed = 0;
        prepared.outputRate = request.sampleRate;

        const auto warmFrames = static_cast<int>(std::min<juce::int64>(
            std::max<juce::int64>(0, rangeEnd - prepared.startFrame),
            std::max(4096, static_cast<int>(std::ceil(outputBlockSize * prepared.reader->sampleRate / request.sampleRate)) + 64)));
        juce::AudioBuffer<float> warmBuffer(2, std::max(1, warmFrames));
        return readFrames(prepared, request, warmBuffer, warmFrames, prepared.startFrame, error);
    }

    void runLoader()
    {
        std::unique_ptr<PreparedPreview> prepared;
        std::uint64_t processedRevision = 0;

        for (;;)
        {
            Request request;

            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [this, processedRevision]
                {
                    return shuttingDown || revision.load() != processedRevision;
                });

                if (shuttingDown)
                    break;

                request = { requestedFile, selection.load(), revision.load(),
                            outputRate, requestedPlay, snapshot.audioAvailable, requestedFrame, restartAtEnd,
                            requestedRegion };
            }

            processedRevision = request.revision;
            juce::String error;

            try
            {
                if (prepared != nullptr && prepared->selection != request.selection)
                    prepared.reset();

                if (request.file == juce::File())
                    continue;

                if (prepared == nullptr)
                    prepared = openFile(request, error);

                if (superseded(request))
                    continue;

                const auto ready = prepared != nullptr && prepare(*prepared, request, error);

                {
                    const std::lock_guard lock(mutex);

                    if (superseded(request))
                        continue;

                    if (!ready)
                    {
                        snapshot.state = State::error;
                        snapshot.message = error.isNotEmpty() ? error : "The WAV could not be prepared. Select another file.";
                        requestedPlay = false;
                    }
                    else
                    {
                        snapshot.sampleRate = prepared->reader->sampleRate;
                        snapshot.channels = prepared->reader->numChannels;
                        snapshot.bitDepth = prepared->sourceBitDepth;
                        snapshot.fileSizeBytes = prepared->fileSizeBytes;
                        totalFrames = prepared->reader->lengthInSamples;
                        requestedRegion = clampRegion(requestedRegion, totalFrames);
                        requestedFrame = prepared->startFrame;
                        snapshot.durationSeconds = static_cast<double>(totalFrames) / snapshot.sampleRate;
                        snapshot.positionSeconds = prepared->positionSeconds();
                        const auto atEnd = requestedFrame == totalFrames;
                        const auto start = request.play && !atEnd
                            && request.audioAvailable && snapshot.audioAvailable;
                        prepared->source->setLooping(loopEnabled);

                        if (start)
                            prepared->transport.start();

                        active = prepared.get();
                        snapshot.state = start ? State::playing : State::ready;
                        snapshot.message = start ? playingMessage()
                            : atEnd ? "Finished. Press Play to preview from the start."
                            : request.play && !snapshot.audioAvailable
                                ? "WAV ready, but audio output is unavailable. Check Windows sound settings."
                                : "Ready. Press Play to preview.";
                        requestedPlay = false;
                    }
                }
            }
            catch (const std::exception&)
            {
                const std::lock_guard lock(mutex);

                if (!superseded(request))
                {
                    active = nullptr;
                    snapshot.state = State::error;
                    snapshot.message = "The WAV could not be prepared safely. Select another file or restart WaveCrate.";
                    requestedPlay = false;
                }
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

    void timerCallback() override
    {
        bool stateChanged = false;

        {
            const std::lock_guard lock(mutex);

            if (active != nullptr && snapshot.state == State::playing)
            {
                if (active->readFailed.load() || active->source->starved.load())
                {
                    snapshot.state = State::error;
                    snapshot.message = "Audio data became unavailable. Check the drive or copy the WAV locally, then select it again.";
                    active = nullptr;
                    stateChanged = true;
                }
                else if (!active->transport.isPlaying())
                {
                    snapshot.state = State::ready;
                    requestedFrame = std::clamp<juce::int64>(active->source->getTotalLength(), 0, totalFrames);
                    snapshot.positionSeconds = snapshot.sampleRate > 0.0
                        ? static_cast<double>(requestedFrame) / snapshot.sampleRate : snapshot.durationSeconds;
                    snapshot.message = finishedMessage();
                    active = nullptr;
                    stateChanged = true;
                }
            }
        }

        if (stateChanged)
            triggerAsyncUpdate();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        {
            const std::lock_guard lock(mutex);
            active = nullptr;
            requestedPlay = false;
            requestedFrame = 0;
            restartAtEnd = false;
            outputRate = device->getCurrentSampleRate();
            snapshot.audioAvailable = outputRate >= 8000.0 && outputRate <= 384000.0
                && device->getActiveOutputChannels().countNumberOfSetBits() > 0;
            snapshot.deviceMessage = snapshot.audioAvailable ? juce::String()
                : "The audio output format is unsupported. Choose a standard Windows output sample rate.";

            if (!snapshot.audioAvailable)
                outputRate = defaultOutputRate;

            if (snapshot.state == State::playing)
                snapshot.state = State::ready;

            snapshot.positionSeconds = 0.0;
            ++revision;
        }

        changed.notify_one();
        triggerAsyncUpdate();
    }

    void audioDeviceStopped() override
    {
        audioDeviceError("The output device stopped or was disconnected. Check Windows sound settings and restart WaveCrate.");
    }

    void audioDeviceError(const juce::String& message) override
    {
        {
            const std::lock_guard lock(mutex);
            active = nullptr;
            requestedPlay = false;
            requestedFrame = 0;
            restartAtEnd = false;
            snapshot.audioAvailable = false;
            snapshot.deviceMessage = "Audio output unavailable. " + message;
            snapshot.positionSeconds = 0.0;

            if (snapshot.state == State::playing)
                snapshot.state = State::ready;

            ++revision;
        }

        changed.notify_one();
        triggerAsyncUpdate();
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        for (int channel = 0; channel < numOutputs; ++channel)
            if (outputs[channel] != nullptr)
                juce::FloatVectorOperations::clear(outputs[channel], numSamples);

        const std::unique_lock lock(mutex, std::try_to_lock);

        if (!lock.owns_lock() || active == nullptr || snapshot.state != State::playing
            || !snapshot.audioAvailable || numOutputs <= 0)
            return;

        for (int offset = 0; offset < numSamples; offset += outputBlockSize)
        {
            if (!active->transport.isPlaying())
                break;

            const auto count = std::min(outputBlockSize, numSamples - offset);
            juce::AudioSourceChannelInfo info(&active->output, 0, count);
            active->transport.getNextAudioBlock(info);

            if (active->readFailed.load() || active->source->starved.load())
                return;

            active->outputFramesPlayed += count;

            for (int channel = 0; channel < std::min(numOutputs, 2); ++channel)
            {
                if (outputs[channel] == nullptr)
                    continue;

                for (int index = 0; index < count; ++index)
                {
                    const auto value = numOutputs == 1
                        ? 0.5f * (active->output.getSample(0, index) + active->output.getSample(1, index))
                        : active->output.getSample(channel, index);
                    outputs[channel][offset + index] = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
                }
            }

            if (!active->transport.isPlaying())
                break;
        }
    }

    AudioPreview& owner;
    mutable std::mutex mutex;
    std::condition_variable changed;
    Snapshot snapshot;
    juce::File requestedFile;
    std::atomic<std::uint64_t> selection { 0 };
    std::atomic<std::uint64_t> revision { 0 };
    double outputRate = defaultOutputRate;
    juce::int64 requestedFrame = 0;
    juce::int64 totalFrames = 0;
    juce::Range<juce::int64> requestedRegion;
    bool loopEnabled = false;
    bool requestedPlay = false;
    bool restartAtEnd = false;
    bool shuttingDown = false;
    bool loaderAvailable = true;
    bool readAheadStarted = false;
    bool callbackRegistered = false;
    PreparedPreview* active = nullptr;
    SharedOutputDeviceManager devices;
    juce::TimeSliceThread readAhead { "WaveCrate audio read-ahead" };
    std::thread loader;
};

AudioPreview::AudioPreview() : impl(std::make_unique<Impl>(*this)) {}
AudioPreview::~AudioPreview() = default;
void AudioPreview::initialiseAudio() { impl->initialiseAudio(); }
void AudioPreview::loadFile(const juce::File& file) { impl->loadFile(file); }
void AudioPreview::play() { impl->play(); }
void AudioPreview::stop() { impl->stop(); }
void AudioPreview::seekToFrame(juce::int64 frame) { impl->seekToFrame(frame); }
void AudioPreview::setRegion(juce::Range<juce::int64> frames) { impl->setRegion(frames); }
void AudioPreview::clearRegion() { impl->setRegion({}); }
void AudioPreview::setLoopEnabled(bool shouldLoop) { impl->setLoopEnabled(shouldLoop); }
AudioPreview::Snapshot AudioPreview::getSnapshot() const { return impl->getSnapshot(); }
}
