#include "AudioPreview.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>

namespace wavecrate
{
namespace
{
class AudioTestFiles final
{
public:
    AudioTestFiles()
        : directory(juce::File::getCurrentWorkingDirectory().getChildFile("out")
                        .getChildFile("audio-preview-tests-" + juce::Uuid().toString()))
    {
        created = directory.createDirectory().wasOk();
    }

    ~AudioTestFiles()
    {
        directory.deleteRecursively();
    }

    juce::File file(const juce::String& name) const
    {
        return directory.getChildFile(name);
    }

    bool writeWav(const juce::File& destination, int channels, double rate,
                  int bits, int frames, bool floatingPoint = false) const
    {
        std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
            .withSampleRate(rate)
            .withNumChannels(channels)
            .withBitsPerSample(bits)
            .withSampleFormat(floatingPoint ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                           : juce::AudioFormatWriterOptions::SampleFormat::integral));

        if (writer == nullptr)
            return false;

        juce::AudioBuffer<float> samples(channels, 1024);
        int written = 0;

        while (written < frames)
        {
            const auto count = juce::jmin(samples.getNumSamples(), frames - written);

            for (int channel = 0; channel < channels; ++channel)
                for (int index = 0; index < count; ++index)
                    samples.setSample(channel, index, static_cast<float>(
                        0.1 * std::sin(2.0 * juce::MathConstants<double>::pi * (220.0 + 110.0 * channel)
                                       * (written + index) / rate)));

            if (!writer->writeFromAudioSampleBuffer(samples, 0, count))
                return false;

            written += count;
        }

        return true;
    }

    bool created = false;

private:
    juce::File directory;
};

bool waitUntil(const std::function<bool()>& predicate, double timeoutMs = 5000.0)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;

    while (!predicate() && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (!juce::MessageManager::getInstance()->runDispatchLoopUntil(5))
            break;
    }

    return predicate();
}

int findChunk(const juce::MemoryBlock& bytes, const char* name)
{
    const auto* data = static_cast<const unsigned char*>(bytes.getData());
    std::size_t offset = 12;

    while (offset + 8 <= bytes.getSize())
    {
        if (std::memcmp(data + offset, name, 4) == 0)
            return static_cast<int>(offset);

        const auto size = static_cast<std::size_t>(juce::ByteOrder::littleEndianInt(data + offset + 4));

        if (size > bytes.getSize() - offset - 8)
            break;

        offset += 8 + size + (size & 1u);
    }

    return -1;
}

void putInt(juce::MemoryBlock& bytes, std::size_t offset, juce::uint32 value)
{
    auto* data = static_cast<unsigned char*>(bytes.getData());

    for (unsigned int index = 0; index < 4; ++index)
        data[offset + index] = static_cast<unsigned char>((value >> (index * 8)) & 0xffu);
}

class AudioPreviewTests final : public juce::UnitTest
{
public:
    AudioPreviewTests() : UnitTest("Asynchronous WAV preview", "WaveCrate") {}

    void runTest() override
    {
        AudioTestFiles files;
        beginTest("Generated mono PCM and stereo float load without opening an output device");

        if (!require(files.created, "Could not create test fixtures under the working directory"))
            return;

        const auto mono = files.file("mono.WAV");
        const auto stereo = files.file("stereo-float.wav");

        if (!require(files.writeWav(mono, 1, 48000.0, 16, 4800))
            || !require(files.writeWav(stereo, 2, 44100.0, 32, 8820, true)))
            return;

        {
            AudioPreview preview;
            int notifications = 0;
            bool messageThreadOnly = true;
            preview.onStateChanged = [&]
            {
                ++notifications;
                messageThreadOnly = messageThreadOnly
                    && juce::MessageManager::getInstance()->isThisTheMessageThread();
            };

            preview.loadFile(mono);
            expectReady(preview, mono, 1, 48000.0, 0.1);
            expectEquals(preview.getSnapshot().bitDepth, 16u);
            expectEquals(preview.getSnapshot().fileSizeBytes, mono.getSize());
            preview.loadFile(stereo);
            expectEquals(preview.getSnapshot().bitDepth, 0u);
            expectEquals(preview.getSnapshot().fileSizeBytes, juce::int64 { -1 });
            expectReady(preview, stereo, 2, 44100.0, 0.2);
            expectEquals(preview.getSnapshot().bitDepth, 32u);
            expectEquals(preview.getSnapshot().fileSizeBytes, stereo.getSize());
            expect(waitUntil([&] { return notifications > 0; }));
            expect(messageThreadOnly, "State notifications must run on the message thread");

            preview.play();
            expect(waitUntil([&] { return !preview.getSnapshot().playRequested; }),
                   "The unavailable-output Play request must finish");
            const auto snapshot = preview.getSnapshot();
            expect(snapshot.state == AudioPreview::State::ready, "No output device must not be reported as playing");
            expect(snapshot.message.containsIgnoreCase("output"), snapshot.message);
        }

        beginTest("Integer PCM bit depths and short files preserve metadata");
        {
            AudioPreview preview;

            for (const auto bits : { 8, 24, 32 })
            {
                const auto sample = files.file("pcm-" + juce::String(bits) + ".wav");

                if (require(files.writeWav(sample, 1, 96000.0, bits, 17)))
                {
                    preview.loadFile(sample);
                    expectReady(preview, sample, 1, 96000.0, 17.0 / 96000.0);
                    expectEquals(preview.getSnapshot().bitDepth, static_cast<unsigned int>(bits));
                    expectEquals(preview.getSnapshot().fileSizeBytes, sample.getSize());
                }
            }
        }

        beginTest("Missing, non-WAV and malformed files fail with a usable message");
        {
            AudioPreview preview;
            expectError(preview, files.file("missing.wav"));
            const auto text = files.file("not-a-wav.txt");
            expect(text.replaceWithText("not audio"));
            expectError(preview, text);
            const auto malformed = files.file("malformed.wav");
            expect(malformed.replaceWithText("RIFFthis is not a valid recording"));
            expectError(preview, malformed);
            expectEquals(preview.getSnapshot().bitDepth, 0u);
            expectEquals(preview.getSnapshot().fileSizeBytes, juce::int64 { -1 });
            const auto directory = files.file("directory.wav");
            expect(directory.createDirectory().wasOk());
            expectError(preview, directory);

            preview.loadFile(stereo);
            expectReady(preview, stereo, 2, 44100.0, 0.2);
        }

        beginTest("Truncated headers, audio payloads and oversized chunk claims are rejected");
        {
            juce::MemoryBlock original;

            if (require(mono.loadFileAsData(original)))
            {
                AudioPreview preview;
                const auto shortHeader = files.file("short-header.wav");
                expect(shortHeader.replaceWithData(original.getData(), 20));
                expectError(preview, shortHeader);

                const auto shortData = files.file("short-data.wav");
                expect(shortData.replaceWithData(original.getData(), original.getSize() - 8));
                expectError(preview, shortData);

                auto forged = original;
                putInt(forged, 4, 0xfffffff0u);
                const auto oversizedContainer = files.file("oversized-container.wav");
                expect(oversizedContainer.replaceWithData(forged.getData(), forged.getSize()));
                expectError(preview, oversizedContainer);

                forged = original;
                const auto dataChunk = findChunk(forged, "data");

                if (require(dataChunk >= 0))
                {
                    putInt(forged, static_cast<std::size_t>(dataChunk) + 4, 0xfffffff0u);
                    const auto oversizedData = files.file("oversized-data.wav");
                    expect(oversizedData.replaceWithData(forged.getData(), forged.getSize()));
                    expectError(preview, oversizedData);

                    forged = original;
                    const auto* data = static_cast<const unsigned char*>(forged.getData());
                    putInt(forged, static_cast<std::size_t>(dataChunk) + 4,
                           juce::ByteOrder::littleEndianInt(data + dataChunk + 4) - 1);
                    const auto partialFrame = files.file("partial-frame.wav");
                    expect(partialFrame.replaceWithData(forged.getData(), forged.getSize()));
                    expectError(preview, partialFrame);
                }

                forged = original;
                const auto formatChunk = findChunk(forged, "fmt ");

                if (require(formatChunk >= 0))
                {
                    putInt(forged, static_cast<std::size_t>(formatChunk) + 4, 2);
                    const auto badFormat = files.file("short-format.wav");
                    expect(badFormat.replaceWithData(forged.getData(), forged.getSize()));
                    expectError(preview, badFormat);

                    forged = original;
                    auto* data = static_cast<unsigned char*>(forged.getData());
                    data[formatChunk + 10] = 0xff;
                    data[formatChunk + 11] = 0xff;
                    const auto badChannels = files.file("excessive-channels.wav");
                    expect(badChannels.replaceWithData(forged.getData(), forged.getSize()));
                    expectError(preview, badChannels);

                    forged = original;
                    data = static_cast<unsigned char*>(forged.getData());
                    data[formatChunk + 20] = 0;
                    data[formatChunk + 21] = 0;
                    const auto badAlignment = files.file("invalid-frame-size.wav");
                    expect(badAlignment.replaceWithData(forged.getData(), forged.getSize()));
                    expectError(preview, badAlignment);
                }
            }
        }

        beginTest("RF64 size headers are checked without allocating the claimed audio length");
        {
            juce::MemoryBlock original;

            if (require(mono.loadFileAsData(original)))
            {
                const auto dataChunk = findChunk(original, "data");

                if (require(dataChunk >= 0))
                {
                    const auto dataBytes = juce::ByteOrder::littleEndianInt(
                        static_cast<const unsigned char*>(original.getData()) + dataChunk + 4);
                    putInt(original, static_cast<std::size_t>(dataChunk) + 4, 0xffffffffu);
                    juce::MemoryOutputStream rf64;
                    rf64.write("RF64", 4);
                    rf64.writeInt(-1);
                    rf64.write("WAVEds64", 8);
                    rf64.writeInt(28);
                    rf64.writeInt64(static_cast<juce::int64>(original.getSize()) + 28);
                    rf64.writeInt64(dataBytes);
                    rf64.writeInt64(4800);
                    rf64.writeInt(0);
                    rf64.write(static_cast<const unsigned char*>(original.getData()) + 12,
                               original.getSize() - 12);
                    const auto valid = files.file("valid-rf64.wav");
                    expect(valid.replaceWithData(rf64.getData(), rf64.getDataSize()));
                    AudioPreview preview;
                    preview.loadFile(valid);
                    expectReady(preview, valid, 1, 48000.0, 0.1);

                    auto invalid = rf64.getMemoryBlock();
                    putInt(invalid, 24, 0x7fffffffu);
                    const auto oversized = files.file("oversized-rf64.wav");
                    expect(oversized.replaceWithData(invalid.getData(), invalid.getSize()));
                    expectError(preview, oversized);
                }
            }
        }

        beginTest("Multichannel WAVs are declined rather than silently mixed");
        {
            const auto surround = files.file("four-channels.wav");

            if (require(files.writeWav(surround, 4, 48000.0, 16, 480)))
            {
                AudioPreview preview;
                expectError(preview, surround);
                expect(preview.getSnapshot().message.containsIgnoreCase("mono or stereo"),
                       preview.getSnapshot().message);
            }
        }

        beginTest("Unused WAV metadata does not control allocations or readiness");
        {
            juce::MemoryBlock original;

            if (require(mono.loadFileAsData(original)))
            {
                juce::MemoryOutputStream metadata;
                metadata.write("smpl", 4);
                metadata.writeInt(36);

                for (int index = 0; index < 9; ++index)
                    metadata.writeInt(-1);

                original.append(metadata.getData(), metadata.getDataSize());
                putInt(original, 4, static_cast<juce::uint32>(original.getSize() - 8));
                const auto unusualMetadata = files.file("untrusted-metadata.wav");
                expect(unusualMetadata.replaceWithData(original.getData(), original.getSize()));
                AudioPreview preview;
                preview.loadFile(unusualMetadata);
                expectReady(preview, unusualMetadata, 1, 48000.0, 0.1);
            }
        }

        beginTest("Stopped seeks preserve source frames and the next Play keeps their position");
        {
            const auto seekable = files.file("seekable.wav");

            if (require(files.writeWav(seekable, 2, 44100.0, 16, 176400)))
            {
                AudioPreview preview;
                preview.loadFile(seekable);
                expectReady(preview, seekable, 2, 44100.0, 4.0);
                constexpr juce::int64 target = 132317;
                const auto seconds = static_cast<double>(target) / 44100.0;
                preview.seekToFrame(target);
                expectWithinAbsoluteError(preview.getSnapshot().positionSeconds, seconds, 1.0e-12,
                                          "A known stopped seek must expose its exact source-frame position");
                expectSettledAt(preview, seconds);

                preview.play();
                expectSettledAt(preview, seconds);
                expect(preview.getSnapshot().message.containsIgnoreCase("output"),
                       "Without a device Play must preserve the seek position, not claim playback");

                preview.seekToFrame(100003);
                preview.play();
                preview.stop();
                expectSettledAt(preview, 0.0);
                expect(!preview.getSnapshot().message.containsIgnoreCase("unavailable"),
                       "Stop must cancel the queued Play and seek");
            }
        }

        beginTest("Seek clamps after loading, EOF stays stopped and Play restarts from zero");
        {
            AudioPreview preview;
            preview.seekToFrame(1000);
            expect(preview.getSnapshot().state == AudioPreview::State::empty);
            preview.loadFile(stereo);
            preview.play();
            preview.seekToFrame(std::numeric_limits<juce::int64>::max());
            expectReady(preview, stereo, 2, 44100.0, 0.2);
            expectSettledAt(preview, 0.2);
            expect(!preview.getSnapshot().message.containsIgnoreCase("unavailable"),
                   "Seeking pending playback to EOF must finish stopped rather than start playback");

            preview.play();
            expectSettledAt(preview, 0.0);
            expect(preview.getSnapshot().message.containsIgnoreCase("output"));

            preview.seekToFrame(-1000);
            expectSettledAt(preview, 0.0);
            preview.seekToFrame(8820);
            expectSettledAt(preview, 0.2);
            preview.stop();
            expectSettledAt(preview, 0.0);

            expectError(preview, files.file("missing-seek.wav"));
            const auto error = preview.getSnapshot().message;
            preview.seekToFrame(1000);
            expect(preview.getSnapshot().state == AudioPreview::State::error);
            expectEquals(preview.getSnapshot().message, error);
            expectWithinAbsoluteError(preview.getSnapshot().positionSeconds, 0.0, 1.0e-12);
        }

        beginTest("Latest seek wins while loading and replacement or Stop discards pending positions");
        {
            AudioPreview preview;
            preview.loadFile(stereo);

            for (int index = 0; index < 32; ++index)
                preview.seekToFrame(index * 100);

            preview.seekToFrame(1237);
            expectReady(preview, stereo, 2, 44100.0, 0.2);
            expectSettledAt(preview, 1237.0 / 44100.0);

            preview.loadFile(mono);
            preview.seekToFrame(3201);
            preview.play();
            preview.seekToFrame(2401);
            preview.stop();
            expectReady(preview, mono, 1, 48000.0, 0.1);
            expectSettledAt(preview, 0.0);
            expect(!preview.getSnapshot().message.containsIgnoreCase("unavailable"),
                   "Stop must cancel playback and all pending seeks during loading");

            preview.seekToFrame(2101);
            preview.play();
            preview.loadFile(stereo);
            expectReady(preview, stereo, 2, 44100.0, 0.2);
            expectSettledAt(preview, 0.0);
            expect(!preview.getSnapshot().message.containsIgnoreCase("unavailable"),
                   "A replacement selection must not inherit either position or Play");
        }

        beginTest("Rapid replacement wins, clears discard stale completions and Stop cancels pending Play");
        {
            AudioPreview preview;

            for (int index = 0; index < 32; ++index)
            {
                preview.loadFile(index % 2 == 0 ? mono : files.file("missing.wav"));
                preview.play();
            }

            preview.loadFile(stereo);
            expectReady(preview, stereo, 2, 44100.0, 0.2);

            preview.loadFile(mono);
            preview.play();
            preview.stop();
            expectReady(preview, mono, 1, 48000.0, 0.1);
            expectWithinAbsoluteError(preview.getSnapshot().positionSeconds, 0.0, 0.000001);
            expect(!preview.getSnapshot().message.containsIgnoreCase("unavailable"),
                   "Stop must cancel the pending Play, not attempt playback after loading");

            preview.loadFile(stereo);
            preview.play();
            preview.loadFile(juce::File());
            juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
            auto snapshot = preview.getSnapshot();
            expect(snapshot.state == AudioPreview::State::empty, "A stale completion must not replace the cleared selection");
            expect(snapshot.file == juce::File());

            preview.loadFile(mono);
            preview.play();
            preview.loadFile(stereo);
            expectReady(preview, stereo, 2, 44100.0, 0.2);
            expect(!preview.getSnapshot().message.containsIgnoreCase("unavailable"),
                   "A pending Play must not carry over to a different selection");
        }

        beginTest("Regions clamp, clear and choose where Play starts; loop is a session preference");
        {
            const auto regionFile = files.file("region.wav");

            if (require(files.writeWav(regionFile, 2, 44100.0, 16, 44100)))
            {
                using Frames = juce::Range<juce::int64>;
                AudioPreview preview;
                preview.setRegion(Frames(10, 20));
                expect(preview.getSnapshot().region.isEmpty(), "A region needs a selected WAV");

                preview.loadFile(regionFile);
                preview.setRegion(Frames(-100, 1000000));
                expectReady(preview, regionFile, 2, 44100.0, 1.0);
                auto snapshot = preview.getSnapshot();
                expectEquals(snapshot.totalFrames, juce::int64 { 44100 });
                expect(snapshot.region == Frames(0, 44100), "A region set while loading must be clamped to the length");

                preview.setRegion(Frames(40000, 90000));
                expect(preview.getSnapshot().region == Frames(40000, 44100));
                preview.setRegion(Frames(50000, 60000));
                expect(preview.getSnapshot().region.isEmpty(), "A region beyond the end must clear");
                preview.setRegion(Frames(8820, 17640));
                preview.setRegion(Frames(500, 100));
                expect(preview.getSnapshot().region.isEmpty(), "An inverted region must clear");
                preview.setRegion(Frames(8820, 17640));
                preview.clearRegion();
                expect(preview.getSnapshot().region.isEmpty());

                preview.setRegion(Frames(8820, 17640));
                preview.seekToFrame(100);
                expectSettledAt(preview, 100.0 / 44100.0);
                preview.play();
                expectSettledAt(preview, 0.2);
                expect(preview.getSnapshot().region == Frames(8820, 17640));

                preview.seekToFrame(13230);
                expectSettledAt(preview, 0.3);
                preview.play();
                expectSettledAt(preview, 0.3);

                preview.stop();
                expectSettledAt(preview, 0.0);
                preview.play();
                expectSettledAt(preview, 0.2);

                expect(!preview.getSnapshot().loopEnabled, "Loop must default off");
                preview.setLoopEnabled(true);
                expect(preview.getSnapshot().loopEnabled);
                preview.loadFile(mono);
                expectReady(preview, mono, 1, 48000.0, 0.1);
                snapshot = preview.getSnapshot();
                expect(snapshot.region.isEmpty(), "A new selection must clear the region");
                expect(snapshot.loopEnabled, "Loop must persist across selections");
                expectEquals(snapshot.totalFrames, juce::int64 { 4800 });
                preview.setLoopEnabled(false);
                expect(!preview.getSnapshot().loopEnabled);
            }
        }

        beginTest("Destruction joins loading work, releases file handles and cancels queued notifications");
        {
            const auto disposable = files.file("disposable.wav");
            expect(files.writeWav(disposable, 2, 48000.0, 16, 48000));
            int lateCallbacks = 0;

            {
                auto preview = std::make_unique<AudioPreview>();
                preview->loadFile(disposable);
                expectReady(*preview, disposable, 2, 48000.0, 1.0);
                preview->onStateChanged = [&] { ++lateCallbacks; };
                preview->loadFile(mono);
                preview->play();
                preview.reset();
            }

            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            expectEquals(lateCallbacks, 0, "Queued callbacks must not outlive their AudioPreview");
            expect(disposable.deleteFile(), "Destruction must release the sample file handle");
        }

        beginTest("A message-thread notification may release its preview owner");
        {
            auto preview = std::make_unique<AudioPreview>();
            preview->onStateChanged = [&]
            {
                if (preview->getSnapshot().state == AudioPreview::State::ready)
                    preview.reset();
            };
            preview->loadFile(mono);
            expect(waitUntil([&] { return preview == nullptr; }),
                   "The callback should be able to destroy the preview without a deadlock or dangling callback");
        }
    }

private:
    bool require(bool condition, const juce::String& message = {})
    {
        expect(condition, message);
        return condition;
    }

    void expectReady(AudioPreview& preview, const juce::File& file, unsigned int channels,
                     double rate, double duration)
    {
        expect(waitUntil([&]
        {
            const auto snapshot = preview.getSnapshot();
            return snapshot.state != AudioPreview::State::loading;
        }), "Timed out waiting for asynchronous WAV preparation");

        const auto snapshot = preview.getSnapshot();
        expect(snapshot.state == AudioPreview::State::ready, snapshot.message);
        expect(snapshot.file == file, "A different selection completed");
        expectEquals(snapshot.channels, channels);
        expectWithinAbsoluteError(snapshot.sampleRate, rate, 0.001);
        expectWithinAbsoluteError(snapshot.durationSeconds, duration, 0.000001);
        expect(!snapshot.audioAvailable, "The metadata tests must not open physical audio hardware");
    }

    void expectError(AudioPreview& preview, const juce::File& file)
    {
        preview.loadFile(file);
        expect(waitUntil([&] { return preview.getSnapshot().state != AudioPreview::State::loading; }),
               "Timed out rejecting invalid input");
        const auto snapshot = preview.getSnapshot();
        expect(snapshot.state == AudioPreview::State::error, "Invalid input must not become ready: " + file.getFileName());
        expect(snapshot.message.isNotEmpty(), "File failures must have a visible explanation");
        expect(snapshot.file == file);
    }

    void expectSettledAt(AudioPreview& preview, double seconds)
    {
        expect(waitUntil([&]
        {
            const auto snapshot = preview.getSnapshot();
            return snapshot.state != AudioPreview::State::loading
                && !snapshot.playRequested && snapshot.message != "Seeking..."
                && snapshot.message != "Rewinding...";
        }), "Timed out waiting for the seek or playback command to finish");

        const auto snapshot = preview.getSnapshot();
        expect(snapshot.state == AudioPreview::State::ready, snapshot.message);
        expect(!snapshot.playRequested, "No Play request should remain after preparation");
        expect(!snapshot.audioAvailable, "Seek unit tests must not open physical audio hardware");
        expectWithinAbsoluteError(snapshot.positionSeconds, seconds, 1.0e-12);
    }
};

AudioPreviewTests audioPreviewTests;
}
}
