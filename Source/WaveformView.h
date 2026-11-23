#pragma once

#include "WaveformData.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wavecrate
{
class WaveformView final : public juce::Component
{
public:
    enum class ChannelDisplayMode { combined, separate };

    WaveformView();
    ~WaveformView() override;

    void setWaveform(const WaveformData::Snapshot& snapshot);
    void setChannelDisplayMode(ChannelDisplayMode mode);
    void setPlaybackPosition(double seconds);
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void zoomToSelection();
    bool canZoomIn() const;
    bool canZoomOut() const;
    bool canZoomToSelection() const;
    juce::Range<juce::int64> getVisibleFrameRange() const;
    // Selection in source frames [start, end). Empty means no selection.
    // These setters do not invoke onSelectionChanged.
    void setSelection(juce::Range<juce::int64> frames);
    void clearSelection();
    juce::Range<juce::int64> getSelection() const;
    void setLoopEnabled(bool shouldLoop);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    std::function<void(juce::int64)> onSeekRequested;
    std::function<void(juce::Range<juce::int64>)> onSelectionChanged;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
}
