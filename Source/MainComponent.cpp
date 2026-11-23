#include "MainComponent.h"
#include "PreviewCommands.h"

namespace wavecrate
{
MainComponent::MainComponent()
{
    setName("WaveCrate sample browser");
    setOpaque(true);
    addAndMakeVisible(desktop);
    auto& browser = desktop.getSamples();
    browser.onSelectionChanged = [this](const juce::File& file)
    {
        audioPreview.loadFile(file);
        waveformData.loadFile(file);
        desktop.updateWaveform(waveformData.getSnapshot());
        updatePreview();
    };
    browser.onPlayRequested = [this] { playSelection(); };
    browser.onTogglePlaybackRequested = [this] { togglePlayback(); };
    browser.onStopRequested = [this] { stopPlayback(); };

    desktop.onPlayRequested = [this] { playSelection(); };
    desktop.onStopRequested = [this] { stopPlayback(); };
    desktop.onSeekRequested = [this](juce::int64 frame)
    {
        audioPreview.seekToFrame(frame);
        updatePreview();
    };
    desktop.onSelectionChanged = [this](juce::Range<juce::int64> frames)
    {
        audioPreview.setRegion(frames);
        updatePreview();
    };
    desktop.onLoopToggled = [this](bool shouldLoop)
    {
        audioPreview.setLoopEnabled(shouldLoop);
        updatePreview();
        desktop.getSamples().focusList();
    };
    audioPreview.onStateChanged = [this] { updatePreview(); };
    waveformData.onStateChanged = [this]
    {
        desktop.updateWaveform(waveformData.getSnapshot());
    };
    desktop.updateWaveform(waveformData.getSnapshot());
    audioPreview.initialiseAudio();
    updatePreview();
    startTimerHz(10);
    setSize(1280, 800);
}

MainComponent::~MainComponent()
{
    stopTimer();
    auto& browser = desktop.getSamples();
    browser.onSelectionChanged = {};
    browser.onPlayRequested = {};
    browser.onTogglePlaybackRequested = {};
    browser.onStopRequested = {};
    audioPreview.onStateChanged = {};
    waveformData.onStateChanged = {};
    desktop.onPlayRequested = {};
    desktop.onStopRequested = {};
    desktop.onSeekRequested = {};
    desktop.onSelectionChanged = {};
    desktop.onLoopToggled = {};
    audioPreview.stop();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(desktop.findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    desktop.setBounds(getLocalBounds());
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCtrlDown() && (key.isKeyCode('O') || key.isKeyCode('o')))
    {
        desktop.showFolderChooser();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        stopPlayback();
        return true;
    }
    if (desktop.folderBrowserHasFocus())
        return false;
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayback();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        playSelection();
        return true;
    }
    if (!key.getModifiers().isAnyModifierKeyDown() && (key.isKeyCode('Z') || key.isKeyCode('z')))
        return desktop.zoomToSelection();
    if (!key.getModifiers().isAnyModifierKeyDown() && (key.isKeyCode('L') || key.isKeyCode('l')))
    {
        audioPreview.setLoopEnabled(!audioPreview.getSnapshot().loopEnabled);
        updatePreview();
        return true;
    }
    return false;
}

void MainComponent::timerCallback()
{
    updatePreview();
}

void MainComponent::updatePreview()
{
    desktop.updatePreview(audioPreview.getSnapshot());
}

void MainComponent::playSelection()
{
    audioPreview.play();
    updatePreview();
    desktop.getSamples().focusList();
}

void MainComponent::stopPlayback()
{
    audioPreview.stop();
    updatePreview();
    desktop.getSamples().focusList();
}

void MainComponent::togglePlayback()
{
    togglePreview(audioPreview);
    updatePreview();
    desktop.getSamples().focusList();
}
}
