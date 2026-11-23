#include "DesktopLayout.h"
#include "WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace wavecrate
{
namespace
{
WaveformData::Snapshot navigationWaveform(juce::int64 frames = 1024,
                                         juce::int64 framesPerPeak = 16)
{
    auto summary = std::make_shared<WaveformData::Summary>();
    summary->sampleRate = 48000.0;
    summary->totalFrames = frames;
    summary->framesPerPeak = framesPerPeak;
    summary->channels.resize(2);
    for (auto& channel : summary->channels)
        channel.resize(static_cast<std::size_t>((frames - 1) / framesPerPeak + 1),
                       WaveformData::Peak { -0.5f, 0.75f });

    WaveformData::Snapshot snapshot;
    snapshot.state = WaveformData::State::ready;
    snapshot.summary = std::move(summary);
    return snapshot;
}

WaveformData::Snapshot opposingWaveform()
{
    auto snapshot = navigationWaveform(4, 1);
    auto summary = std::make_shared<WaveformData::Summary>(*snapshot.summary);
    std::fill(summary->channels[0].begin(), summary->channels[0].end(),
              WaveformData::Peak { 0.25f, 0.5f });
    std::fill(summary->channels[1].begin(), summary->channels[1].end(),
              WaveformData::Peak { -0.75f, -0.5f });
    snapshot.summary = std::move(summary);
    return snapshot;
}

void setWaveformTestColours(WaveformView& view)
{
    view.setColour(juce::ListBox::backgroundColourId, juce::Colours::white);
    view.setColour(juce::ListBox::outlineColourId, juce::Colours::white);
    view.setColour(juce::Label::textColourId, juce::Colours::black);
}

juce::Image renderWaveform(WaveformView& view)
{
    juce::Image image(juce::Image::RGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics(image);
    view.paint(graphics);
    return image;
}

bool hasWaveformAt(const juce::Image& image, int x, int y)
{
    return image.getPixelAt(x, y).getBrightness() < 0.5f;
}

bool samePixels(const juce::Image& first, const juce::Image& second)
{
    if (first.getBounds() != second.getBounds())
        return false;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            if (first.getPixelAt(x, y) != second.getPixelAt(x, y))
                return false;
    return true;
}

template <typename ComponentType>
ComponentType* findComponent(juce::Component& root, const juce::String& name = {})
{
    if (auto* component = dynamic_cast<ComponentType*>(&root);
        component != nullptr && (name.isEmpty() || component->getName() == name))
        return component;
    for (auto* child : root.getChildren())
        if (auto* component = findComponent<ComponentType>(*child, name))
            return component;
    return nullptr;
}

juce::MouseEvent navigationMouse(juce::Component& component, juce::Point<float> position,
                                 int modifiers = juce::ModifierKeys::leftButtonModifier)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), position,
             juce::ModifierKeys(modifiers), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &component, &component, juce::Time(), position, juce::Time(), 1, false };
}

juce::ScrollBar* navigationScrollBar(WaveformView& view)
{
    for (auto* child : view.getChildren())
        if (auto* scrollBar = dynamic_cast<juce::ScrollBar*>(child))
            return scrollBar;

    return nullptr;
}

void clickNavigation(WaveformView& view, const juce::MouseEvent& event)
{
    view.mouseDown(event);
    view.mouseUp(event);
}

// The 500 x 200 view plots full-range frames across x = 10 to 490.
float navigationX(juce::int64 frame, juce::int64 frames = 1024)
{
    return 10.0f + 480.0f * static_cast<float>(frame) / static_cast<float>(frames);
}

class WaveformNavigationTests final : public juce::UnitTest
{
public:
    WaveformNavigationTests() : UnitTest("Waveform navigation", "WaveCrate") {}

    void runTest() override
    {
        beginTest("Zoom bounds, fit and summary replacement preserve source-frame ranges");
        {
            WaveformView view;
            view.setSize(500, 200);
            const auto snapshot = navigationWaveform();
            view.setWaveform(snapshot);
            expectRange(view, 0, 1024);
            expect(view.canZoomIn() && !view.canZoomOut());
            view.zoomIn();
            expectRange(view, 256, 768);
            view.setWaveform(snapshot);
            view.setSize(700, 240);
            view.setPlaybackPosition(100.0);
            expectRange(view, 256, 768);
            for (int index = 0; index < 12; ++index)
                view.zoomIn();
            expectEquals(view.getVisibleFrameRange().getLength(), juce::int64(16));
            expect(!view.canZoomIn() && view.canZoomOut());
            for (int index = 0; index < 12; ++index)
                view.zoomOut();
            expectRange(view, 0, 1024);
            view.zoomIn();
            view.fitToWindow();
            expectRange(view, 0, 1024);
            view.zoomIn();
            view.setWaveform(navigationWaveform(7, 16));
            expectRange(view, 0, 7);
            expect(!view.canZoomIn() && !view.canZoomOut());
            constexpr juce::int64 largeFrames = (juce::int64(1) << 40) + 7;
            view.setWaveform(navigationWaveform(largeFrames, (largeFrames - 1) / 64 + 1));
            view.zoomIn();
            expectEquals(view.getVisibleFrameRange().getLength(), largeFrames / 2);
            view.fitToWindow();
            expectRange(view, 0, largeFrames);
            view.setWaveform({});
            expectRange(view, 0, 0);
            expect(!view.canZoomIn() && !view.canZoomOut());
        }

        beginTest("Zoom to selection preserves regions and respects overview resolution");
        {
            WaveformView view;
            view.setSize(500, 200);
            int selectionCount = 0;
            int seekCount = 0;
            view.onSelectionChanged = [&](juce::Range<juce::int64>) { ++selectionCount; };
            view.onSeekRequested = [&](juce::int64) { ++seekCount; };
            expect(!view.canZoomToSelection());
            view.zoomToSelection();
            expectRange(view, 0, 0);
            view.setWaveform(navigationWaveform());
            view.zoomIn();
            expect(!view.canZoomToSelection());
            view.zoomToSelection();
            expectRange(view, 256, 768);
            view.setSelection({ 64, 192 });
            expect(view.canZoomToSelection());
            view.zoomToSelection();
            expectRange(view, 64, 192);
            expectSelection(view, 64, 192);
            view.zoomToSelection();
            expectRange(view, 64, 192);
            view.fitToWindow();
            expectRange(view, 0, 1024);
            expectSelection(view, 64, 192);

            view.setSelection({ 500, 501 });
            view.zoomToSelection();
            expectRange(view, 493, 509);
            expectSelection(view, 500, 501);
            expect(!view.canZoomIn());
            view.setSelection({ 0, 1 });
            view.zoomToSelection();
            expectRange(view, 0, 16);
            view.setSelection({ 1023, 1024 });
            view.zoomToSelection();
            expectRange(view, 1008, 1024);
            view.setSelection({ 0, 1024 });
            view.zoomToSelection();
            expectRange(view, 0, 1024);

            constexpr juce::int64 largeFrames = (juce::int64(1) << 40) + 7;
            const auto large = navigationWaveform(largeFrames, (largeFrames - 1) / 64 + 1);
            view.setWaveform(large);
            view.setSelection({ largeFrames - 1000, largeFrames });
            view.zoomToSelection();
            expectRange(view, largeFrames - large.summary->framesPerPeak, largeFrames);
            expectSelection(view, largeFrames - 1000, largeFrames);
            view.setWaveform({});
            expect(!view.canZoomToSelection());
            view.zoomToSelection();
            expectRange(view, 0, 0);
            expectEquals(selectionCount, 0);
            expectEquals(seekCount, 0);
        }

        beginTest("Right drag pans from the press position without seeking or changing selection");
        {
            WaveformView view;
            view.setSize(500, 200);
            view.setWaveform(navigationWaveform());
            view.setSelection({ 300, 700 });
            int selectionCount = 0;
            int seekCount = 0;
            view.onSelectionChanged = [&](juce::Range<juce::int64>) { ++selectionCount; };
            view.onSeekRequested = [&](juce::int64) { ++seekCount; };
            const auto rightButton = juce::ModifierKeys::rightButtonModifier;
            view.mouseDown(navigationMouse(view, { 250.0f, 100.0f }, rightButton));
            view.mouseDrag(navigationMouse(view, { 370.0f, 100.0f }, rightButton));
            view.mouseUp(navigationMouse(view, { 370.0f, 100.0f }, 0));
            expectRange(view, 0, 1024);

            view.zoomIn();
            view.mouseDown(navigationMouse(view, { 250.0f, 100.0f }, rightButton));
            expect(view.getMouseCursor() == juce::MouseCursor::DraggingHandCursor);
            view.mouseDrag(navigationMouse(view, { 370.0f, 100.0f }, rightButton));
            expectRange(view, 128, 640);
            view.mouseDrag(navigationMouse(view, { 370.0f, 180.0f }, rightButton));
            expectRange(view, 128, 640);
            view.mouseDrag(navigationMouse(view, { 130.0f, 100.0f }, rightButton));
            expectRange(view, 384, 896);
            view.mouseDrag(navigationMouse(view, { 1250.0f, 100.0f }, rightButton));
            expectRange(view, 0, 512);
            view.mouseDrag(navigationMouse(view, { -750.0f, 100.0f }, rightButton));
            expectRange(view, 512, 1024);
            auto* scrollBar = navigationScrollBar(view);
            expect(scrollBar != nullptr);
            if (scrollBar != nullptr)
            {
                expectEquals(scrollBar->getCurrentRangeStart(), 512.0);
                expectEquals(scrollBar->getCurrentRangeSize(), 512.0);
            }
            view.mouseDrag(navigationMouse(view, { 370.0f, 100.0f }, 0));
            expectRange(view, 512, 1024);
            view.mouseUp(navigationMouse(view, { -750.0f, 100.0f }, 0));
            expect(view.getMouseCursor() == juce::MouseCursor::NormalCursor);
            view.mouseDrag(navigationMouse(view, { 250.0f, 100.0f }, rightButton));
            expectRange(view, 512, 1024);
            expectSelection(view, 300, 700);

            view.mouseDown(navigationMouse(view, { 0.0f, 0.0f }, rightButton));
            view.mouseDrag(navigationMouse(view, { 370.0f, 100.0f }, rightButton));
            view.mouseUp(navigationMouse(view, { 370.0f, 100.0f }, 0));
            expectRange(view, 512, 1024);
            view.mouseDown(navigationMouse(view, { 250.0f, 100.0f }, rightButton));
            view.setWaveform(navigationWaveform(128, 16));
            view.mouseDrag(navigationMouse(view, { 370.0f, 100.0f }, rightButton));
            view.mouseUp(navigationMouse(view, { 370.0f, 100.0f }, 0));
            expectRange(view, 0, 128);
            expectEquals(selectionCount, 0);
            expectEquals(seekCount, 0);
        }

        beginTest("Wheel anchor, panning and scrollbar use one clamped viewport");
        {
            WaveformView view;
            view.setSize(500, 200);
            view.setWaveform(navigationWaveform());
            auto* scrollBar = navigationScrollBar(view);
            expect(scrollBar != nullptr);
            if (scrollBar == nullptr)
                return;
            expect(!scrollBar->isVisible() && !scrollBar->isEnabled());
            const juce::Point<float> pointer(view.getWidth() * 0.25f, view.getHeight() * 0.5f);
            juce::int64 clickedFrame = -1;
            view.onSeekRequested = [&](juce::int64 frame) { clickedFrame = frame; };
            clickNavigation(view, navigationMouse(view, pointer));
            const auto anchorFrame = clickedFrame;
            juce::MouseWheelDetails wheel {};
            wheel.deltaY = 0.25f;
            view.mouseWheelMove(navigationMouse(view, pointer, 0), wheel);
            expectEquals(view.getVisibleFrameRange().getLength(), juce::int64(512));
            clickNavigation(view, navigationMouse(view, pointer));
            expect(std::abs(clickedFrame - anchorFrame) <= 1, "Wheel zoom must retain the pointer's frame");
            expect(scrollBar->isVisible() && scrollBar->isEnabled());
            const auto start = view.getVisibleFrameRange().getStart();
            wheel.deltaY = -0.25f;
            view.mouseWheelMove(navigationMouse(view, pointer, juce::ModifierKeys::shiftModifier), wheel);
            expectEquals(view.getVisibleFrameRange().getStart(), start + 128);
            expectEquals(view.getVisibleFrameRange().getLength(), juce::int64(512));
            wheel.deltaX = -0.25f;
            wheel.deltaY = 0.01f;
            view.mouseWheelMove(navigationMouse(view, pointer, 0), wheel);
            expectEquals(view.getVisibleFrameRange().getStart(), start + 256);
            scrollBar->scrollToBottom(juce::sendNotificationSync);
            expectRange(view, 512, 1024);
            wheel.deltaX = -10.0f;
            view.mouseWheelMove(navigationMouse(view, pointer, 0), wheel);
            expectRange(view, 512, 1024);
            wheel.deltaX = 10.0f;
            view.mouseWheelMove(navigationMouse(view, pointer, 0), wheel);
            expectRange(view, 0, 512);
            expectEquals(scrollBar->getCurrentRangeStart(), 0.0);
            expectEquals(scrollBar->getCurrentRangeSize(), 512.0);
            scrollBar->setCurrentRangeStart(128.0);
            view.setWaveform(navigationWaveform(128, 16));
            scrollBar->handleUpdateNowIfNeeded();
            expectRange(view, 0, 128);
            expect(!scrollBar->isVisible() && !scrollBar->isEnabled());
            view.zoomIn();
            scrollBar->setCurrentRangeStart(0.0);
        }

        beginTest("Clicks map the visible range and ignore non-plot or unavailable waveforms");
        {
            WaveformView view;
            view.setSize(500, 200);
            view.setWaveform(navigationWaveform());
            const auto centre = view.getLocalBounds().toFloat().getCentre();
            int seekCount = 0;
            juce::int64 clickedFrame = -1;
            view.onSeekRequested = [&](juce::int64 frame)
            {
                ++seekCount;
                clickedFrame = frame;
            };
            clickNavigation(view, navigationMouse(view, centre));
            expectEquals(clickedFrame, juce::int64(512));
            view.zoomIn();
            auto* scrollBar = navigationScrollBar(view);
            expect(scrollBar != nullptr);
            if (scrollBar == nullptr)
                return;
            scrollBar->scrollToBottom(juce::sendNotificationSync);
            clickNavigation(view, navigationMouse(view, centre));
            expectEquals(clickedFrame, juce::int64(768));
            expectEquals(seekCount, 2);
            clickNavigation(view, navigationMouse(view, centre, juce::ModifierKeys::rightButtonModifier));
            clickNavigation(view, navigationMouse(view, { 0.0f, 0.0f }));
            clickNavigation(view, navigationMouse(view, scrollBar->getBounds().toFloat().getCentre()));
            clickNavigation(view, navigationMouse(*scrollBar, scrollBar->getLocalBounds().toFloat().getCentre())
                                      .getEventRelativeTo(&view));
            expectEquals(seekCount, 2);
            for (auto state : { WaveformData::State::empty, WaveformData::State::loading,
                                WaveformData::State::error, WaveformData::State::ready })
            {
                WaveformData::Snapshot unavailable;
                unavailable.state = state;
                view.setWaveform(unavailable);
                clickNavigation(view, navigationMouse(view, centre));
                expectEquals(seekCount, 2);
            }
            auto invalid = navigationWaveform();
            invalid.summary = std::make_shared<WaveformData::Summary>();
            view.setWaveform(invalid);
            clickNavigation(view, navigationMouse(view, centre));
            expectEquals(seekCount, 2);
            view.setWaveform(navigationWaveform(1, 16));
            clickNavigation(view, navigationMouse(view, centre));
            expectEquals(clickedFrame, juce::int64(0));
            expectEquals(seekCount, 3);
        }

        beginTest("Dragging creates a normalised, clamped selection reported once on release");
        {
            WaveformView view;
            view.setSize(500, 200);
            view.setWaveform(navigationWaveform());
            int seekCount = 0;
            int selectionCount = 0;
            juce::Range<juce::int64> reported(-1, -1);
            view.onSeekRequested = [&](juce::int64) { ++seekCount; };
            view.onSelectionChanged = [&](juce::Range<juce::int64> frames)
            {
                ++selectionCount;
                reported = frames;
            };
            const auto y = 100.0f;
            view.mouseDown(navigationMouse(view, { navigationX(768), y }));
            view.mouseDrag(navigationMouse(view, { navigationX(768) + 2.0f, y }));
            expect(view.getSelection().isEmpty(), "Movement under the click distance must not select");
            view.mouseDrag(navigationMouse(view, { navigationX(256), y }));
            expectSelection(view, 256, 768);
            view.mouseDrag(navigationMouse(view, { -40.0f, y }));
            expectSelection(view, 0, 768);
            view.mouseDrag(navigationMouse(view, { 600.0f, y }));
            expectSelection(view, 768, 1024);
            view.mouseDrag(navigationMouse(view, { navigationX(768), y }));
            expectSelection(view, 768, 769);
            expectEquals(selectionCount, 0);
            view.mouseUp(navigationMouse(view, { navigationX(768), y }));
            expectEquals(selectionCount, 1);
            expectEquals(seekCount, 0);
            expect(reported == juce::Range<juce::int64>(768, 769));

            view.mouseDown(navigationMouse(view, { navigationX(512), y }));
            view.mouseDrag(navigationMouse(view, { 57.0625f, y }));
            expectSelection(view, 100, 512);
            view.mouseDrag(navigationMouse(view, { 57.15625f, y }));
            expectSelection(view, 101, 512);
            view.mouseUp(navigationMouse(view, { 57.15625f, y }));
            expectEquals(selectionCount, 2);
            expect(reported == juce::Range<juce::int64>(101, 512));
        }

        beginTest("Dragging near a selection boundary adjusts it and boundaries may cross");
        {
            WaveformView view;
            view.setSize(500, 200);
            view.setWaveform(navigationWaveform());
            int selectionCount = 0;
            juce::Range<juce::int64> reported;
            view.onSelectionChanged = [&](juce::Range<juce::int64> frames)
            {
                ++selectionCount;
                reported = frames;
            };
            view.setSelection({ 256, 768 });
            expectEquals(selectionCount, 0);
            const auto y = 100.0f;
            view.mouseMove(navigationMouse(view, { navigationX(256) + 3.0f, y }, 0));
            expect(view.getMouseCursor() == juce::MouseCursor::LeftRightResizeCursor);
            view.mouseMove(navigationMouse(view, { navigationX(512), y }, 0));
            expect(view.getMouseCursor() == juce::MouseCursor::NormalCursor);

            view.mouseDown(navigationMouse(view, { navigationX(256) + 3.0f, y }));
            view.mouseDrag(navigationMouse(view, { navigationX(512), y }));
            expectSelection(view, 512, 768);
            view.mouseDrag(navigationMouse(view, { navigationX(1024), y }));
            expectSelection(view, 768, 1024);
            view.mouseUp(navigationMouse(view, { navigationX(1024), y }));
            expectEquals(selectionCount, 1);
            expect(reported == juce::Range<juce::int64>(768, 1024));

            view.mouseDown(navigationMouse(view, { navigationX(1024) - 4.0f, y }));
            view.mouseDrag(navigationMouse(view, { navigationX(896), y }));
            expectSelection(view, 768, 896);
            view.mouseUp(navigationMouse(view, { navigationX(896), y }));
            expectEquals(selectionCount, 2);
            expect(reported == juce::Range<juce::int64>(768, 896));
        }

        beginTest("Clicks seek and clear an existing selection");
        {
            WaveformView view;
            view.setSize(500, 200);
            view.setWaveform(navigationWaveform());
            int seekCount = 0;
            int selectionCount = 0;
            juce::int64 clickedFrame = -1;
            juce::Range<juce::int64> reported(-1, -1);
            view.onSeekRequested = [&](juce::int64 frame)
            {
                ++seekCount;
                clickedFrame = frame;
            };
            view.onSelectionChanged = [&](juce::Range<juce::int64> frames)
            {
                ++selectionCount;
                reported = frames;
            };
            const auto y = 100.0f;
            view.setSelection({ 256, 768 });
            clickNavigation(view, navigationMouse(view, { navigationX(512), y }));
            expect(view.getSelection().isEmpty());
            expectEquals(selectionCount, 1);
            expect(reported.isEmpty());
            expectEquals(seekCount, 1);
            expectEquals(clickedFrame, juce::int64(512));

            view.mouseDown(navigationMouse(view, { navigationX(256), y }));
            view.mouseDrag(navigationMouse(view, { navigationX(256) + 2.0f, y + 1.0f }));
            view.mouseUp(navigationMouse(view, { navigationX(256) + 2.0f, y + 1.0f }));
            expectEquals(seekCount, 2);
            expectEquals(clickedFrame, juce::int64(256));
            expectEquals(selectionCount, 1);
        }

        beginTest("Selections are clamped and new waveforms clear them without callbacks");
        {
            WaveformView view;
            view.setSize(500, 200);
            int selectionCount = 0;
            view.onSelectionChanged = [&](juce::Range<juce::int64>) { ++selectionCount; };
            view.setSelection({ 0, 10 });
            expect(view.getSelection().isEmpty(), "A selection needs a valid waveform");
            const auto snapshot = navigationWaveform();
            view.setWaveform(snapshot);
            view.setSelection({ -10, 2000 });
            expectSelection(view, 0, 1024);
            view.setSelection({ 2000, 3000 });
            expect(view.getSelection().isEmpty());
            view.setSelection({ 5, 5 });
            expect(view.getSelection().isEmpty());
            view.setSelection({ 100, 200 });
            view.setWaveform(snapshot);
            view.zoomIn();
            expectSelection(view, 100, 200);
            view.setWaveform(navigationWaveform());
            expect(view.getSelection().isEmpty());
            view.setSelection({ 100, 200 });
            WaveformData::Snapshot loading;
            loading.state = WaveformData::State::loading;
            view.setWaveform(loading);
            expect(view.getSelection().isEmpty());
            view.setWaveform(snapshot);
            view.setSelection({ 100, 200 });
            view.clearSelection();
            expect(view.getSelection().isEmpty());

            const auto y = 100.0f;
            view.mouseDown(navigationMouse(view, { navigationX(256), y }));
            view.mouseDrag(navigationMouse(view, { navigationX(768), y }));
            expectSelection(view, 256, 768);
            view.setWaveform(navigationWaveform());
            view.mouseUp(navigationMouse(view, { navigationX(768), y }));
            expect(view.getSelection().isEmpty());
            expectEquals(selectionCount, 0);
        }

        beginTest("Combined and separate views retain both channels and leave mono unchanged");
        {
            WaveformView view;
            view.setSize(500, 200);
            setWaveformTestColours(view);
            view.setWaveform(opposingWaveform());
            view.setPlaybackPosition(-1.0);
            const auto combined = renderWaveform(view);
            expect(hasWaveformAt(combined, 250, 60), "Combined view must retain the positive channel");
            expect(hasWaveformAt(combined, 250, 165), "Combined view must retain the negative channel");
            expect(!hasWaveformAt(combined, 250, 35));
            expect(!hasWaveformAt(combined, 250, 180));

            view.setChannelDisplayMode(WaveformView::ChannelDisplayMode::separate);
            const auto separate = renderWaveform(view);
            expect(hasWaveformAt(separate, 250, 35), "The first channel must have its own lane");
            expect(hasWaveformAt(separate, 250, 175), "The second channel must have its own lane");
            expect(!hasWaveformAt(separate, 250, 60));
            expect(!hasWaveformAt(separate, 250, 165));

            view.setChannelDisplayMode(WaveformView::ChannelDisplayMode::combined);
            expect(samePixels(combined, renderWaveform(view)), "Switching back must rebuild the combined view");
            view.zoomIn();
            view.setSelection({ 1, 3 });
            int callbacks = 0;
            view.onSeekRequested = [&](juce::int64) { ++callbacks; };
            view.onSelectionChanged = [&](juce::Range<juce::int64>) { ++callbacks; };
            view.setChannelDisplayMode(WaveformView::ChannelDisplayMode::separate);
            expectRange(view, 1, 3);
            expectSelection(view, 1, 3);
            expectEquals(callbacks, 0);

            view.setWaveform(opposingWaveform());
            view.setPlaybackPosition(-1.0);
            expect(hasWaveformAt(renderWaveform(view), 250, 35),
                   "The display mode must survive a new sample");
            auto mono = opposingWaveform();
            auto monoSummary = std::make_shared<WaveformData::Summary>(*mono.summary);
            monoSummary->channels.resize(1);
            mono.summary = std::move(monoSummary);
            view.setWaveform(mono);
            view.setPlaybackPosition(-1.0);
            const auto monoSeparate = renderWaveform(view);
            view.setChannelDisplayMode(WaveformView::ChannelDisplayMode::combined);
            expect(samePixels(monoSeparate, renderWaveform(view)),
                   "Changing the mode must not alter mono waveforms");
        }

        beginTest("Combined view retains a narrow peak from the second channel");
        {
            auto snapshot = navigationWaveform(4096, 1);
            auto summary = std::make_shared<WaveformData::Summary>(*snapshot.summary);
            for (auto& channel : summary->channels)
                std::fill(channel.begin(), channel.end(), WaveformData::Peak {});
            summary->channels[1][2048] = { -1.0f, -1.0f };
            snapshot.summary = std::move(summary);

            WaveformView view;
            view.setSize(60, 200);
            setWaveformTestColours(view);
            view.setWaveform(snapshot);
            view.setPlaybackPosition(-1.0);
            const auto image = renderWaveform(view);
            expect(hasWaveformAt(image, 30, 170), "A right-channel spike must remain visible when reduced");
            expect(!hasWaveformAt(image, 29, 170));
        }

        beginTest("Sample details use ready preview metadata and retain full-sample duration");
        {
            DesktopLayout layout;
            layout.setSize(1280, 800);
            auto* details = findComponent<juce::Label>(layout, "Sample details");
            auto* position = findComponent<juce::Label>(layout, "Playback position");
            expect(details != nullptr && position != nullptr);
            if (details == nullptr || position == nullptr)
                return;

            int callbacks = 0;
            layout.onPlayRequested = [&] { ++callbacks; };
            layout.onStopRequested = [&] { ++callbacks; };
            layout.onSeekRequested = [&](juce::int64) { ++callbacks; };
            layout.onSelectionChanged = [&](juce::Range<juce::int64>) { ++callbacks; };
            layout.onLoopToggled = [&](bool) { ++callbacks; };
            AudioPreview::Snapshot snapshot;
            snapshot.state = AudioPreview::State::ready;
            snapshot.durationSeconds = 125.125;
            snapshot.positionSeconds = 1.0;
            snapshot.sampleRate = 44100.0;
            snapshot.channels = 2;
            snapshot.bitDepth = 24;
            snapshot.fileSizeBytes = (juce::int64(1) << 33) + 123;
            snapshot.region = { 44100, 88200 };
            snapshot.loopEnabled = true;
            layout.updatePreview(snapshot);
            const auto expected = juce::String("Duration: 2:05.125   Size: ")
                + juce::File::descriptionOfSizeInBytes(snapshot.fileSizeBytes)
                + "   Channels: 2   Sample rate: 44.1 kHz   Bit depth: 24 bit"
                  "   Audio bitrate: 2116.8 kb/s";
            expectEquals(details->getText(), expected);
            expectEquals(details->getTooltip(), expected);
            expectEquals(details->getDescription(), expected);
            expect(position->getText().contains("0:01 / 2:05"));
            expect(position->getText().contains("Selection 0:01.000-0:02.000"));
            expect(position->getText().contains("Loop: selection"));

            snapshot.state = AudioPreview::State::playing;
            snapshot.positionSeconds = 1.5;
            layout.updatePreview(snapshot);
            expectEquals(details->getText(), expected);

            snapshot.sampleRate = 48000.0;
            snapshot.channels = 1;
            snapshot.bitDepth = 16;
            snapshot.durationSeconds = 0.375;
            snapshot.fileSizeBytes = 1024;
            snapshot.region = {};
            layout.updatePreview(snapshot);
            expectEquals(details->getText(),
                         juce::String("Duration: 0:00.375   Size: ")
                         + juce::File::descriptionOfSizeInBytes(1024)
                         + "   Channels: 1   Sample rate: 48 kHz   Bit depth: 16 bit"
                           "   Audio bitrate: 768.0 kb/s");
            expect(position->getText().contains("Loop: whole sample"));

            snapshot.sampleRate = 8000.0;
            snapshot.bitDepth = 8;
            snapshot.fileSizeBytes = 0;
            layout.updatePreview(snapshot);
            expect(details->getText().contains("Size: " + juce::File::descriptionOfSizeInBytes(0)));
            expect(details->getText().contains("Sample rate: 8 kHz"));
            expect(details->getText().contains("Audio bitrate: 64.0 kb/s"));
            expectEquals(callbacks, 0);
        }

        beginTest("Loading, empty and error previews clear stale sample details");
        {
            DesktopLayout layout;
            auto* details = findComponent<juce::Label>(layout, "Sample details");
            expect(details != nullptr);
            if (details == nullptr)
                return;
            expect(details->getText().isEmpty());
            AudioPreview::Snapshot snapshot;
            snapshot.durationSeconds = 12.0;
            snapshot.sampleRate = 96000.0;
            snapshot.channels = 2;
            snapshot.bitDepth = 32;
            snapshot.fileSizeBytes = 123456;
            for (const auto state : { AudioPreview::State::loading, AudioPreview::State::empty,
                                      AudioPreview::State::error })
            {
                snapshot.state = AudioPreview::State::ready;
                layout.updatePreview(snapshot);
                expect(details->getText().contains("Audio bitrate: 6144.0 kb/s"));
                snapshot.state = state;
                layout.updatePreview(snapshot);
                expect(details->getText().isEmpty());
                expect(details->getTooltip().isEmpty());
                expect(details->getDescription().isEmpty());
            }
            snapshot.state = AudioPreview::State::ready;
            snapshot.durationSeconds = std::numeric_limits<double>::quiet_NaN();
            snapshot.sampleRate = std::numeric_limits<double>::infinity();
            snapshot.channels = 0;
            snapshot.bitDepth = 0;
            snapshot.fileSizeBytes = -1;
            layout.updatePreview(snapshot);
            expectEquals(details->getText(),
                         juce::String("Duration: -   Size: -   Channels: -   Sample rate: -"
                                      "   Bit depth: -   Audio bitrate: -"));
            snapshot.sampleRate = 48000.0;
            snapshot.channels = 2;
            layout.updatePreview(snapshot);
            expect(details->getText().contains("Sample rate: 48 kHz"));
            expect(details->getText().contains("Audio bitrate: -"));
        }

        beginTest("The dedicated details row preserves waveform space and keeps controls separate");
        {
            DesktopLayout layout;
            auto* details = findComponent<juce::Label>(layout, "Sample details");
            auto* position = findComponent<juce::Label>(layout, "Playback position");
            auto* view = findComponent<WaveformView>(layout);
            expect(details != nullptr && position != nullptr && view != nullptr);
            if (details == nullptr || position == nullptr || view == nullptr)
                return;
            for (const auto size : { juce::Point<int>(720, 480), juce::Point<int>(1280, 800) })
            {
                layout.setSize(size.x, size.y);
                auto* pane = details->getParentComponent();
                expect(pane == position->getParentComponent() && pane == view->getParentComponent());
                expect(details->getY() >= view->getBottom());
                expect(details->getBottom() <= position->getY());
                expectEquals(details->getHeight(), 26);
                expect(view->getHeight() >= 89, "The pane must retain its minimum waveform drawing height");
                if (size.x == 1280)
                    expect(view->getHeight() >= 145, "The default waveform height must remain close to 161 px");
                expect(position->getWidth() >= juce::GlyphArrangement::getStringWidth(
                           position->getFont(), "--:-- / --:--") + 10.0f);
                for (auto* child : pane->getChildren())
                {
                    expect(pane->getLocalBounds().contains(child->getBounds()),
                           child->getName() + " must fit inside the waveform pane");
                    expect(child->getWidth() > 0 && child->getHeight() > 0);
                    for (auto* other : pane->getChildren())
                        if (child != other)
                            expect(!child->getBounds().intersects(other->getBounds()),
                                   child->getName() + " must not overlap " + other->getName());
                }
            }
            auto* pane = details->getParentComponent();
            pane->setSize(pane->getWidth(), 171);
            expectEquals(view->getHeight(), 89);
            expect(details->getY() >= view->getBottom());
            expect(details->getBottom() <= position->getY());
        }

        beginTest("The waveform view menu switches modes without changing playback or selection");
        {
            DesktopLayout layout;
            layout.setSize(720, 480);
            auto* menu = findComponent<juce::ComboBox>(layout, "Waveform channel view");
            auto* view = findComponent<WaveformView>(layout);
            auto* position = findComponent<juce::Label>(layout, "Playback position");
            expect(menu != nullptr && view != nullptr && position != nullptr);
            if (menu == nullptr || view == nullptr || position == nullptr)
                return;
            expect(menu->isVisible() && menu->isEnabled());
            expectEquals(menu->getNumItems(), 2);
            if (menu->getNumItems() != 2)
                return;
            expectEquals(menu->getItemText(0), juce::String("View: Combined"));
            expectEquals(menu->getItemText(1), juce::String("View: Stereo"));
            expectEquals(menu->getText(), juce::String("View: Combined"));
            expect(menu->getParentComponent()->getLocalBounds().contains(menu->getBounds()));
            expect(menu->getParentComponent() == position->getParentComponent());
            expect(!menu->getBounds().intersects(position->getBounds()));
            expect(position->getWidth() >= juce::GlyphArrangement::getStringWidth(
                       position->getFont(), "--:-- / --:--"));

            int callbacks = 0;
            layout.onPlayRequested = [&] { ++callbacks; };
            layout.onStopRequested = [&] { ++callbacks; };
            layout.onSeekRequested = [&](juce::int64) { ++callbacks; };
            layout.onSelectionChanged = [&](juce::Range<juce::int64>) { ++callbacks; };
            layout.onLoopToggled = [&](bool) { ++callbacks; };
            menu->setSelectedId(menu->getItemId(1), juce::sendNotificationSync);
            layout.updateWaveform(opposingWaveform());
            setWaveformTestColours(*view);
            view->setPlaybackPosition(-1.0);
            const auto x = view->getWidth() / 2;
            const auto y = view->getHeight() * 3 / 10;
            expect(!hasWaveformAt(renderWaveform(*view), x, y), "Stereo menu choice must show separate lanes");
            menu->setSelectedId(menu->getItemId(0), juce::sendNotificationSync);
            expect(hasWaveformAt(renderWaveform(*view), x, y), "Combined menu choice must show one lane");

            view->zoomIn();
            view->setSelection({ 1, 3 });
            menu->setSelectedId(menu->getItemId(1), juce::sendNotificationSync);
            expectRange(*view, 1, 3);
            expectSelection(*view, 1, 3);
            layout.updateWaveform({});
            layout.updateWaveform(opposingWaveform());
            view->setPlaybackPosition(-1.0);
            expectEquals(menu->getText(), juce::String("View: Stereo"));
            expect(!hasWaveformAt(renderWaveform(*view), x, y), "The menu choice must survive sample changes");
            expectEquals(callbacks, 0);
        }
    }

private:
    void expectSelection(const WaveformView& view, juce::int64 start, juce::int64 end)
    {
        expectEquals(view.getSelection().getStart(), start);
        expectEquals(view.getSelection().getEnd(), end);
    }

    void expectRange(const WaveformView& view, juce::int64 start, juce::int64 end)
    {
        expectEquals(view.getVisibleFrameRange().getStart(), start);
        expectEquals(view.getVisibleFrameRange().getEnd(), end);
    }
};

static WaveformNavigationTests waveformNavigationTests;
}
}
