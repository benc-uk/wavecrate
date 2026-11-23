#pragma once

#include "AudioPreview.h"
#include "DesktopLayout.h"
#include "WaveformData.h"

namespace wavecrate
{
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void updatePreview();
    void playSelection();
    void stopPlayback();
    void togglePlayback();

    AudioPreview audioPreview;
    WaveformData waveformData;
    DesktopLayout desktop;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
}
