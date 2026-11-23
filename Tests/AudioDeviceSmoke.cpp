#include "AudioPreview.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace wavecrate::tests
{
namespace
{
template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + milliseconds;
    do
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        if (predicate())
            return true;
    } while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return false;
}

bool writeQuietTone(const juce::File& file)
{
    constexpr int sampleRate = 44100;
    constexpr int sampleCount = sampleRate * 4;
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    stream->write("RIFF", 4);
    stream->writeInt(36 + sampleCount * 2);
    stream->write("WAVEfmt ", 8);
    stream->writeInt(16);
    stream->writeShort(1);
    stream->writeShort(1);
    stream->writeInt(sampleRate);
    stream->writeInt(sampleRate * 2);
    stream->writeShort(2);
    stream->writeShort(16);
    stream->write("data", 4);
    stream->writeInt(sampleCount * 2);
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const auto fade = juce::jmin(1.0, sample / 220.0, (sampleCount - sample - 1) / 220.0);
        const auto phase = juce::MathConstants<double>::twoPi * 220.0 * sample / sampleRate;
        stream->writeShort(static_cast<short>(327.0 * fade * std::sin(phase)));
    }
    stream->flush();
    return stream->getStatus().wasOk();
}

Microsoft::WRL::ComPtr<IAudioMeterInformation> findOwnSessionMeter()
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(devices.GetAddressOf()))))
        return {};

    ComPtr<IMMDeviceCollection> outputs;
    if (FAILED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &outputs)))
        return {};

    UINT outputCount = 0;
    if (FAILED(outputs->GetCount(&outputCount)))
        return {};

    for (UINT output = 0; output < outputCount; ++output)
    {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(outputs->Item(output, &device))
            || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(manager.GetAddressOf())))
            || FAILED(manager->GetSessionEnumerator(&sessions)))
            continue;

        int count = 0;
        if (FAILED(sessions->GetCount(&count)))
            continue;

        for (int index = 0; index < count; ++index)
        {
            ComPtr<IAudioSessionControl> session;
            ComPtr<IAudioSessionControl2> details;
            ComPtr<IAudioMeterInformation> meter;
            DWORD process = 0;
            if (SUCCEEDED(sessions->GetSession(index, &session))
                && SUCCEEDED(session.As(&details))
                && SUCCEEDED(details->GetProcessId(&process))
                && process == GetCurrentProcessId()
                && SUCCEEDED(session.As(&meter)))
                return meter;
        }
    }
    return {};
}
}

int runAudioDeviceSmoke()
{
    const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const juce::ScopeGuard releaseCom([comResult]
    {
        if (SUCCEEDED(comResult))
            CoUninitialize();
    });

    const juce::TemporaryFile fixture(".wav");
    if (! writeQuietTone(fixture.getFile()))
    {
        std::cerr << "Could not write the test WAV." << std::endl;
        return 1;
    }

    AudioPreview preview;
    preview.initialiseAudio();
    if (! waitFor([&] { return preview.getSnapshot().audioAvailable; }, 10000))
    {
        std::cerr << "BLOCKED: " << preview.getSnapshot().deviceMessage << std::endl;
        return 77;
    }

    preview.loadFile(fixture.getFile());
    if (! waitFor([&] { return preview.getSnapshot().state == AudioPreview::State::ready; }, 10000))
    {
        std::cerr << "Could not load the test WAV: " << preview.getSnapshot().message << std::endl;
        return 1;
    }

    preview.play();
    Microsoft::WRL::ComPtr<IAudioMeterInformation> meter;
    if (! waitFor([&]
    {
        meter = findOwnSessionMeter();
        return meter != nullptr;
    }, 1500))
    {
        std::cerr << "BLOCKED: could not observe this process's Windows audio session." << std::endl;
        return 77;
    }

    float peak = 0.0f;
    if (! waitFor([&]
    {
        float value = 0.0f;
        if (SUCCEEDED(meter->GetPeakValue(&value)))
            peak = juce::jmax(peak, value);
        return peak > 0.000001f && preview.getSnapshot().positionSeconds > 0.1;
    }, 1500))
    {
        std::cerr << "No non-zero output was observed from the preview session." << std::endl;
        return 1;
    }

    preview.seekToFrame(44100 * 2);
    if (! waitFor([&]
    {
        const auto snapshot = preview.getSnapshot();
        float value = 0.0f;
        return snapshot.state == AudioPreview::State::playing && !snapshot.playRequested
            && snapshot.positionSeconds > 2.05 && snapshot.positionSeconds < 3.0
            && SUCCEEDED(meter->GetPeakValue(&value)) && value > 0.000001f;
    }, 2000))
    {
        std::cerr << "Seeking during playback did not continue at the requested position." << std::endl;
        return 1;
    }

    preview.stop();
    if (! waitFor([&]
    {
        const auto snapshot = preview.getSnapshot();
        return snapshot.state == AudioPreview::State::ready && snapshot.positionSeconds < 0.01;
    }, 2000))
    {
        std::cerr << "Stop did not rewind the preview." << std::endl;
        return 1;
    }

    preview.play();
    if (! waitFor([&] { return preview.getSnapshot().state == AudioPreview::State::playing; }, 2000)
        || ! waitFor([&] { return preview.getSnapshot().state == AudioPreview::State::ready; }, 6000))
    {
        std::cerr << "Playback did not return to ready at end of file." << std::endl;
        return 1;
    }

    std::cout << "PASS: own Windows audio session peak=" << peak
              << "; playback position advanced; playing seek continued; stop rewound; EOF returned to ready."
              << std::endl;
    return 0;
}
}
