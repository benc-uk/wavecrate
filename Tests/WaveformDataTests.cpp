#include "WaveformData.h"
#include "WaveformView.h"

#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

namespace wavecrate
{
namespace
{
class WaveformTestFiles final
{
public:
    WaveformTestFiles()
        : directory(juce::File::getCurrentWorkingDirectory().getChildFile("out")
                        .getChildFile("waveform-tests-" + juce::Uuid().toString()))
    {
        created = directory.createDirectory().wasOk();
    }

    ~WaveformTestFiles()
    {
        directory.deleteRecursively();
    }

    juce::File file(const juce::String& name) const
    {
        return directory.getChildFile(name);
    }

    bool writeWav(const juce::File& destination, int channels, int frames, bool floatingPoint,
                  const std::function<float(int, int)>& sample) const
    {
        auto stream = destination.createOutputStream();

        if (stream == nullptr || !stream->setPosition(0) || stream->truncate().failed())
            return false;

        const auto bytesPerFrame = channels * (floatingPoint ? 4 : 2);
        const auto dataBytes = frames * bytesPerFrame;

        if (!stream->write("RIFF", 4) || !stream->writeInt(36 + dataBytes)
            || !stream->write("WAVEfmt ", 8) || !stream->writeInt(16)
            || !stream->writeShort(static_cast<short>(floatingPoint ? 3 : 1))
            || !stream->writeShort(static_cast<short>(channels))
            || !stream->writeInt(48000) || !stream->writeInt(48000 * bytesPerFrame)
            || !stream->writeShort(static_cast<short>(bytesPerFrame))
            || !stream->writeShort(static_cast<short>(floatingPoint ? 32 : 16))
            || !stream->write("data", 4) || !stream->writeInt(dataBytes))
            return false;

        for (int frame = 0; frame < frames; ++frame)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto value = sample(channel, frame);
                const auto written = floatingPoint
                    ? stream->writeInt(std::bit_cast<juce::int32>(value))
                    : stream->writeShort(static_cast<short>(
                        juce::jlimit(-32768L, 32767L, std::lround(value * 32768.0f))));

                if (!written)
                    return false;
            }
        }

        stream->flush();
        return stream->getStatus().wasOk();
    }

    bool created = false;

private:
    juce::File directory;
};

bool waitForWaveform(const std::function<bool()>& predicate)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;

    while (!predicate() && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (!juce::MessageManager::getInstance()->runDispatchLoopUntil(5))
            break;
    }

    return predicate();
}

class WaveformDataTests final : public juce::UnitTest
{
public:
    WaveformDataTests() : UnitTest("Waveform data", "WaveCrate") {}

    void runTest() override
    {
        WaveformTestFiles files;
        beginTest("PCM and float peaks retain channel extrema with bounded output");
        expect(files.created, "Could not create waveform fixtures under out");

        if (!files.created)
            return;

        const auto mono = files.file("mono.WAV");
        const auto stereo = files.file("stereo.wav");
        constexpr std::array<float, 7> monoSamples { -1.0f, -0.5f, 0.125f, 0.5f, 0.875f, 0.0f, -0.25f };
        const auto wroteMono = files.writeWav(mono, 1, 7, false, [&](int, int frame)
        {
            return monoSamples[static_cast<std::size_t>(frame)];
        });
        const auto wroteStereo = files.writeWav(stereo, 2, 100003, true, [](int channel, int frame)
        {
            if (frame == 2)
                return std::numeric_limits<float>::quiet_NaN();
            if (frame == 3)
                return channel == 0 ? std::numeric_limits<float>::infinity()
                                    : -std::numeric_limits<float>::infinity();
            if (frame == 24)
                return channel == 0 ? 0.875f : -0.875f;
            if (frame == 25)
                return channel == 0 ? -0.75f : 0.75f;
            if (frame == 4095)
                return channel == 0 ? -1.25f : 0.9375f;
            if (frame == 4096)
                return channel == 0 ? 1.5f : -0.9375f;
            if (frame == 100002)
                return channel == 0 ? 0.625f : -0.625f;

            return channel == 0 ? 0.125f : -0.25f;
        });
        expect(wroteMono && wroteStereo, "Could not write generated WAV fixtures");

        if (!wroteMono || !wroteStereo)
            return;

        {
            int notifications = 0;
            bool messageThreadOnly = true;
            WaveformData data;
            data.onStateChanged = [&]
            {
                ++notifications;
                messageThreadOnly = messageThreadOnly
                    && juce::MessageManager::getInstance()->isThisTheMessageThread();
            };

            if (const auto summary = loadReady(data, mono))
            {
                expectEquals(summary->totalFrames, static_cast<juce::int64>(7));
                expectEquals(summary->sampleRate, 48000.0);
                expectEquals(static_cast<int>(summary->channels.size()), 1);
                expectEquals(summary->framesPerPeak, static_cast<juce::int64>(1));

                if (summary->channels.size() == 1 && summary->channels.front().size() == monoSamples.size())
                {
                    for (std::size_t index = 0; index < monoSamples.size(); ++index)
                        expectPeak(*summary, 0, index, monoSamples[index], monoSamples[index]);
                }
                else
                {
                    expect(false, "Short mono WAV must retain every frame");
                }
            }

            if (const auto summary = loadReady(data, stereo))
            {
                expectEquals(summary->totalFrames, static_cast<juce::int64>(100003));
                expectEquals(static_cast<int>(summary->channels.size()), 2);
                expect(summary->framesPerPeak > 1, "Longer WAVs must be reduced to bounded peak arrays");

                bool boundedAndFinite = summary->channels.size() == 2;

                for (const auto& channel : summary->channels)
                {
                    boundedAndFinite = boundedAndFinite && !channel.empty() && channel.size() <= 4096;
                    const auto expectedPeaks = 1 + (summary->totalFrames - 1) / summary->framesPerPeak;
                    expectEquals(static_cast<juce::int64>(channel.size()), expectedPeaks);

                    for (const auto& peak : channel)
                        boundedAndFinite = boundedAndFinite && std::isfinite(peak.minimum)
                            && std::isfinite(peak.maximum) && peak.minimum <= peak.maximum;
                }

                expect(boundedAndFinite, "Peak storage must stay bounded and non-finite samples must be sanitised");
                expectPeak(*summary, 0, 0, 0.0f, 0.875f);
                expectPeak(*summary, 1, 0, -0.875f, 0.0f);
                expectPeak(*summary, 0, 1, -0.75f, 0.125f);
                expectPeak(*summary, 1, 1, -0.25f, 0.75f);
                const auto boundaryPeak = static_cast<std::size_t>(4095 / summary->framesPerPeak);
                expectPeak(*summary, 0, boundaryPeak, -1.25f, 1.5f);
                expectPeak(*summary, 1, boundaryPeak, -0.9375f, 0.9375f);
                expectPeak(*summary, 0, 100, 0.125f, 0.125f);
                expectPeak(*summary, 1, 100, -0.25f, -0.25f);
                const auto lastPeak = static_cast<std::size_t>((summary->totalFrames - 1) / summary->framesPerPeak);
                expectPeak(*summary, 0, lastPeak, 0.125f, 0.625f);
                expectPeak(*summary, 1, lastPeak, -0.625f, -0.25f);
                savePreview(data.getSnapshot());
            }

            expect(waitForWaveform([&] { return notifications > 0; }));
            expect(messageThreadOnly, "Waveform notifications must run on the message thread");
        }

        beginTest("Cached data is reused, invalidated on size or time changes, and evicted by LRU");
        {
            WaveformData data;
            const auto original = loadReady(data, mono);
            loadReady(data, stereo);
            expect(loadReady(data, mono) == original, "Unchanged files must reuse immutable peak data");

            const auto changedTime = juce::Time(mono.getLastModificationTime().toMilliseconds() + 2000);
            expect(files.writeWav(mono, 1, 7, false, [](int, int) { return 0.25f; }));
            expect(mono.setLastModificationTime(changedTime));
            const auto changed = loadReady(data, mono);
            expect(changed != original, "A changed timestamp must invalidate same-sized cached data");

            if (changed != nullptr)
                expectPeak(*changed, 0, 0, 0.25f, 0.25f);

            expect(files.writeWav(mono, 1, 9, false, [](int, int) { return -0.5f; }));
            expect(mono.setLastModificationTime(changedTime));
            const auto resized = loadReady(data, mono);
            expect(resized != changed, "A changed size must invalidate data even when the timestamp is unchanged");

            if (resized != nullptr)
            {
                expectEquals(resized->totalFrames, static_cast<juce::int64>(9));
                expectPeak(*resized, 0, 0, -0.5f, -0.5f);
            }
        }

        {
            WaveformData data;
            std::array<juce::File, 9> samples;
            std::array<std::shared_ptr<const WaveformData::Summary>, 9> summaries;

            for (std::size_t index = 0; index < samples.size(); ++index)
            {
                samples[index] = files.file("cache-" + juce::String(static_cast<int>(index)) + ".wav");
                expect(files.writeWav(samples[index], 1, 5, false, [](int, int) { return 0.125f; }));

                if (index < 8)
                    summaries[index] = loadReady(data, samples[index]);
            }

            expect(loadReady(data, samples[0]) == summaries[0]);
            summaries[8] = loadReady(data, samples[8]);
            expect(loadReady(data, samples[0]) == summaries[0], "The most recently used entry must stay cached");
            expect(loadReady(data, samples[1]) != summaries[1], "The ninth entry must evict the least recently used summary");
        }

        beginTest("Replacement, clear, errors and destruction cannot publish stale waveforms");
        {
            WaveformData data;
            const auto old = loadReady(data, mono);
            data.loadFile(stereo);
            const auto replacing = data.getSnapshot();
            expect(replacing.file == stereo && replacing.summary != old, "Selection must immediately clear the old summary");

            for (int index = 0; index < 12; ++index)
                data.loadFile(index % 2 == 0 ? mono : stereo);

            data.loadFile(mono);
            expect(waitForWaveform([&] { return data.getSnapshot().state != WaveformData::State::loading; }));
            expect(data.getSnapshot().state == WaveformData::State::ready && data.getSnapshot().file == mono);

            data.loadFile(stereo);
            data.loadFile({});
            expect(data.getSnapshot().state == WaveformData::State::empty && data.getSnapshot().summary == nullptr);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            expect(data.getSnapshot().state == WaveformData::State::empty, "Cancelled work must not refill a cleared waveform");

            const auto malformed = files.file("invalid.wav");
            expect(malformed.replaceWithText("not a WAV"));
            data.loadFile(stereo);
            data.loadFile(malformed);
            expect(waitForWaveform([&] { return data.getSnapshot().state != WaveformData::State::loading; }));
            const auto failed = data.getSnapshot();
            expect(failed.state == WaveformData::State::error && failed.file == malformed);
            expect(failed.summary == nullptr && failed.message.isNotEmpty(), "Errors must clear peaks and explain the failure");
            loadReady(data, mono);

            const auto removed = files.file("removed.wav");
            expect(files.writeWav(removed, 1, 5, false, [](int, int) { return 0.25f; }));
            loadReady(data, removed);
            expect(removed.deleteFile());
            data.loadFile(removed);
            expect(waitForWaveform([&] { return data.getSnapshot().state != WaveformData::State::loading; }));
            expect(data.getSnapshot().state == WaveformData::State::error && data.getSnapshot().summary == nullptr,
                   "A removed file must not be served from cache");
        }

        int callbacks = 0;
        auto pending = std::make_unique<WaveformData>();
        pending->onStateChanged = [&] { ++callbacks; };
        pending->loadFile(stereo);
        pending.reset();
        const auto beforeDispatch = callbacks;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        expectEquals(callbacks, beforeDispatch, "Destruction must cancel queued owner callbacks");
    }

private:
    std::shared_ptr<const WaveformData::Summary> loadReady(WaveformData& data, const juce::File& file)
    {
        data.loadFile(file);
        expect(waitForWaveform([&] { return data.getSnapshot().state != WaveformData::State::loading; }),
               "Timed out loading " + file.getFileName());
        const auto snapshot = data.getSnapshot();
        expect(snapshot.state == WaveformData::State::ready && snapshot.file == file
                   && snapshot.summary != nullptr,
               file.getFileName() + ": " + snapshot.message);
        return snapshot.summary;
    }

    void expectPeak(const WaveformData::Summary& summary, std::size_t channel, std::size_t index,
                    float minimum, float maximum)
    {
        if (channel >= summary.channels.size() || index >= summary.channels[channel].size())
        {
            expect(false, "Expected peak is missing");
            return;
        }

        expectWithinAbsoluteError(summary.channels[channel][index].minimum, minimum, 0.0001f);
        expectWithinAbsoluteError(summary.channels[channel][index].maximum, maximum, 0.0001f);
    }

    void savePreview(const WaveformData::Snapshot& snapshot)
    {
        WaveformView view;
        view.setBounds(0, 0, 720, 220);
        view.setVisible(true);
        view.setWaveform(snapshot);
        juce::Image image(juce::Image::RGB, 720, 220, true);
        {
            juce::Graphics graphics(image);
            view.paintEntireComponent(graphics, true);
        }

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile("out")
                                     .getChildFile("tests").getChildFile("m2-waveform-preview.png");
        expect(destination.getParentDirectory().createDirectory().wasOk());
        auto output = destination.createOutputStream();

        if (output == nullptr || !output->setPosition(0) || output->truncate().failed())
        {
            expect(false, "Could not create the waveform preview image");
            return;
        }

        juce::PNGImageFormat png;
        expect(png.writeImageToStream(image, *output), "Could not write the waveform preview image");
        output->flush();
        expect(output->getStatus().wasOk());
    }
};

static WaveformDataTests waveformDataTests;
}
}
