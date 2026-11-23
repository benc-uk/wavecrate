#pragma once

#include "AudioPreview.h"
#include "SampleBrowser.h"
#include "WaveformData.h"

#include <functional>

namespace wavecrate
{
class DesktopLayout final : public juce::Component
{
public:
    DesktopLayout();
    ~DesktopLayout() override;

    SampleBrowser& getSamples();
    void showFolderChooser();
    bool folderBrowserHasFocus() const;
    bool zoomToSelection();
    void updatePreview(const AudioPreview::Snapshot& snapshot);
    void updateWaveform(const WaveformData::Snapshot& snapshot);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

    std::function<void()> onPlayRequested;
    std::function<void()> onStopRequested;
    std::function<void(juce::int64)> onSeekRequested;
    std::function<void(juce::Range<juce::int64>)> onSelectionChanged;
    std::function<void(bool)> onLoopToggled;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
