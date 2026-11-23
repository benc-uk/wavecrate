#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace wavecrate
{
class SampleBrowser final : public juce::Component
{
public:
    SampleBrowser();
    ~SampleBrowser() override;

    void openFolder(const juce::File& folder);
    juce::File getCurrentFolder() const;
    juce::File getSelectedFile() const;
    int getNumFiles() const;
    // Folder discovery only; header metadata continues to arrive in the background.
    bool isLoading() const;
    juce::String getStatusText() const;
    void selectRow(int row);
    void focusList();

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void lookAndFeelChanged() override;

    std::function<void(const juce::File&)> onSelectionChanged;
    std::function<void()> onPlayRequested;
    std::function<void()> onTogglePlaybackRequested;
    std::function<void()> onStopRequested;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBrowser)
};
}
