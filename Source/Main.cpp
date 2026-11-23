#include "MainComponent.h"

#include <memory>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace wavecrate
{
class MainWindow final : public juce::DocumentWindow,
                         private juce::DarkModeSettingListener
{
public:
    MainWindow()
        : DocumentWindow(JUCE_APPLICATION_NAME_STRING,
                         juce::Colour(0xff15191f),
                         juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(720, 480, 16384, 16384);
        setContentOwned(new MainComponent(), true);
        auto initialWidth = getWidth();
        auto initialHeight = getHeight();
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = display->userBounds.toNearestInt();
            initialWidth = juce::jmin(initialWidth, area.getWidth());
            initialHeight = juce::jmin(initialHeight, area.getHeight());
        }
        centreWithSize(initialWidth, initialHeight);
        setVisible(true);
        juce::Desktop::getInstance().addDarkModeSettingListener(this);
        darkModeSettingChanged();
    }

    ~MainWindow() override
    {
        juce::Desktop::getInstance().removeDarkModeSettingListener(this);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    void darkModeSettingChanged() override
    {
        if (auto* peer = getPeer())
        {
            const BOOL dark = juce::Desktop::getInstance().isDarkModeActive() ? TRUE : FALSE;
            DwmSetWindowAttribute(static_cast<HWND>(peer->getNativeHandle()),
                                  DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return JUCE_APPLICATION_NAME_STRING;
    }

    const juce::String getApplicationVersion() override
    {
        return JUCE_APPLICATION_VERSION_STRING;
    }

    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setMinimised(false);
            mainWindow->toFront(true);
        }
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION(wavecrate::Application)
