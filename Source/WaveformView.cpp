#include "WaveformView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace wavecrate
{
namespace
{
bool isValidSummary(const WaveformData::Summary& summary)
{
    if (!std::isfinite(summary.sampleRate) || summary.sampleRate <= 0.0
        || summary.totalFrames <= 0 || summary.framesPerPeak <= 0
        || summary.channels.empty() || summary.channels.size() > 2)
        return false;

    const auto expectedPeaks = (summary.totalFrames - 1) / summary.framesPerPeak + 1;
    return std::all_of(summary.channels.begin(), summary.channels.end(),
                       [expectedPeaks](const auto& peaks)
                       {
                           return peaks.size() == static_cast<std::size_t>(expectedPeaks);
                       });
}

void addPeakRange(juce::Path& path, const std::vector<WaveformData::Peak>& peaks,
                  const std::vector<WaveformData::Peak>* additionalPeaks,
                  std::size_t first, std::size_t end, float x, float width,
                  float centre, float amplitude, float laneTop, float laneBottom)
{
    auto minimum = std::numeric_limits<float>::infinity();
    auto maximum = -std::numeric_limits<float>::infinity();
    const auto includePeak = [&](const WaveformData::Peak& peak)
    {
        if (!std::isfinite(peak.minimum) || !std::isfinite(peak.maximum))
            return;

        minimum = std::min(minimum, std::min(peak.minimum, peak.maximum));
        maximum = std::max(maximum, std::max(peak.minimum, peak.maximum));
    };
    for (auto index = first; index < end; ++index)
    {
        includePeak(peaks[index]);
        if (additionalPeaks != nullptr)
            includePeak((*additionalPeaks)[index]);
    }

    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return;

    const auto top = centre - std::clamp(maximum, -1.0f, 1.0f) * amplitude;
    const auto bottom = centre - std::clamp(minimum, -1.0f, 1.0f) * amplitude;
    const auto visibleTop = std::max(laneTop, top - 0.5f);
    const auto visibleBottom = std::min(laneBottom, bottom + 0.5f);
    if (width > 0.0f && visibleBottom > visibleTop)
        path.addRectangle(x, visibleTop, width, visibleBottom - visibleTop);
}

juce::int64 boundedFrame(double frame, juce::int64 maximum)
{
    if (frame <= 0.0)
        return 0;
    if (frame >= static_cast<double>(maximum))
        return maximum;

    return static_cast<juce::int64>(frame);
}

constexpr float boundaryGrabDistance = 5.0f;
constexpr float clickDistance = 3.0f;
}

struct WaveformView::Impl final : juce::ScrollBar::Listener
{
    explicit Impl(WaveformView& view) : owner(view)
    {
        scrollBar.setAutoHide(false);
        scrollBar.setWantsKeyboardFocus(false);
        scrollBar.setTitle("Waveform position");
        scrollBar.addListener(this);
        owner.addChildComponent(scrollBar);
    }

    ~Impl() override
    {
        scrollBar.cancelPendingUpdate();
        scrollBar.removeListener(this);
    }

    juce::int64 minimumLength() const
    {
        return validSummary ? std::min(summary->totalFrames, summary->framesPerPeak) : 0;
    }

    void update()
    {
        const auto canPan = validSummary && visibleRange.getLength() < summary->totalFrames;
        scrollBar.cancelPendingUpdate();
        scrollBar.setRangeLimits(0.0, validSummary ? static_cast<double>(summary->totalFrames) : 0.0,
                                 juce::dontSendNotification);
        scrollBar.setCurrentRange(static_cast<double>(visibleRange.getStart()),
                                  static_cast<double>(visibleRange.getLength()),
                                  juce::dontSendNotification);
        scrollBar.setSingleStepSize(std::max(1.0, static_cast<double>(visibleRange.getLength()) * 0.1));
        scrollBar.setEnabled(canPan);

        auto bounds = owner.getLocalBounds();
        const auto scrollHeight = owner.getLookAndFeel().getDefaultScrollbarWidth();
        const auto showScrollBar = canPan && bounds.getWidth() > 20
            && bounds.getHeight() > scrollHeight + 16;
        scrollBar.setVisible(showScrollBar);
        if (showScrollBar)
            scrollBar.setBounds(bounds.removeFromBottom(scrollHeight).reduced(1, 0));

        rebuild(bounds);
        owner.repaint();
    }

    void setRange(juce::int64 start, juce::int64 length)
    {
        if (!validSummary)
            return;

        length = std::clamp(length, minimumLength(), summary->totalFrames);
        start = std::clamp(start, juce::int64(0), summary->totalFrames - length);
        const juce::Range<juce::int64> nextRange(start, start + length);
        if (visibleRange != nextRange)
        {
            visibleRange = nextRange;
            update();
        }
    }

    void zoomTo(juce::int64 length, double anchor)
    {
        if (!validSummary)
            return;

        length = std::clamp(length, minimumLength(), summary->totalFrames);
        const auto start = static_cast<double>(visibleRange.getStart())
            + anchor * static_cast<double>(visibleRange.getLength() - length);
        setRange(boundedFrame(std::round(start), summary->totalFrames - length), length);
    }

    enum class DragMode
    {
        none,
        create,
        adjust,
        pan
    };

    enum class Boundary
    {
        none,
        start,
        end
    };

    double visibleFraction(float x) const
    {
        return static_cast<double>(x - area.getX()) / static_cast<double>(area.getWidth());
    }

    // Seek target: the frame under the pointer.
    juce::int64 frameAt(float x) const
    {
        const auto length = visibleRange.getLength();
        return visibleRange.getStart()
            + boundedFrame(std::floor(visibleFraction(x) * static_cast<double>(length)), length - 1);
    }

    // Selection boundary: the nearest frame boundary, clamped to the visible range.
    juce::int64 boundaryAt(float x) const
    {
        const auto length = visibleRange.getLength();
        const auto fraction = std::clamp(visibleFraction(x), 0.0, 1.0);
        return visibleRange.getStart()
            + boundedFrame(std::round(fraction * static_cast<double>(length)), length);
    }

    float xForBoundary(juce::int64 frame) const
    {
        const auto fraction = static_cast<double>(frame - visibleRange.getStart())
            / static_cast<double>(visibleRange.getLength());
        return area.getX() + static_cast<float>(fraction) * area.getWidth();
    }

    bool isBoundaryVisible(juce::int64 frame) const
    {
        return frame >= visibleRange.getStart() && frame <= visibleRange.getEnd();
    }

    Boundary boundaryNear(juce::Point<float> position) const
    {
        if (!validSummary || channelCount == 0 || selection.isEmpty()
            || !area.contains(position))
            return Boundary::none;

        const auto infinity = std::numeric_limits<float>::infinity();
        const auto startDistance = isBoundaryVisible(selection.getStart())
            ? std::abs(position.x - xForBoundary(selection.getStart())) : infinity;
        const auto endDistance = isBoundaryVisible(selection.getEnd())
            ? std::abs(position.x - xForBoundary(selection.getEnd())) : infinity;
        if (std::min(startDistance, endDistance) > boundaryGrabDistance)
            return Boundary::none;

        if (endDistance < startDistance
            || (endDistance == startDistance && position.x >= xForBoundary(selection.getEnd())))
            return Boundary::end;

        return Boundary::start;
    }

    juce::Range<juce::int64> rangeBetween(juce::int64 first, juce::int64 second) const
    {
        auto start = std::min(first, second);
        auto end = std::max(first, second);
        if (start == end)
        {
            if (end < summary->totalFrames)
                ++end;
            else
                --start;
        }
        return { start, end };
    }

    void cancelDrag()
    {
        dragMode = DragMode::none;
        dragStarted = false;
        owner.setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override
    {
        if (validSummary && std::isfinite(newRangeStart))
            setRange(boundedFrame(std::round(newRangeStart),
                                  summary->totalFrames - visibleRange.getLength()),
                     visibleRange.getLength());
    }

    void rebuild(juce::Rectangle<int> bounds)
    {
        if (geometrySummary == summary.get() && geometryBounds == bounds
            && geometryRange == visibleRange && geometryMode == channelDisplayMode)
            return;

        geometrySummary = summary.get();
        geometryBounds = bounds;
        geometryRange = visibleRange;
        geometryMode = channelDisplayMode;
        for (auto& path : paths)
            path.clear();

        channelCount = 0;
        area = bounds.toFloat().reduced(10.0f, 8.0f);
        if (!validSummary || area.getWidth() < 1.0f || area.getHeight() < 1.0f)
            return;

        const auto combineChannels = channelDisplayMode == ChannelDisplayMode::combined
            && summary->channels.size() == 2;
        channelCount = combineChannels ? 1 : static_cast<int>(summary->channels.size());
        const auto* additionalPeaks = combineChannels ? &summary->channels[1] : nullptr;
        const auto gap = channelCount == 2 ? std::min(8.0f, area.getHeight() * 0.15f)
                                           : 0.0f;
        const auto laneHeight = (area.getHeight() - gap) / static_cast<float>(channelCount);
        const auto amplitude = std::max(0.0f, (laneHeight - 1.0f) * 0.5f);
        const auto startFrame = static_cast<double>(visibleRange.getStart());
        const auto visibleFrames = static_cast<double>(visibleRange.getLength());
        const auto framesPerPeak = static_cast<double>(summary->framesPerPeak);
        const auto peakCount = summary->channels.front().size();
        const auto firstVisiblePeak = static_cast<std::size_t>(
            visibleRange.getStart() / summary->framesPerPeak);
        const auto endVisiblePeak = static_cast<std::size_t>(
            (visibleRange.getEnd() - 1) / summary->framesPerPeak + 1);
        const auto visiblePeakCount = endVisiblePeak - firstVisiblePeak;
        const auto columnCount = std::min(visiblePeakCount,
                                          static_cast<std::size_t>(area.getWidth()));

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto channelIndex = static_cast<std::size_t>(channel);
            const auto& peaks = summary->channels[channelIndex];
            auto& path = paths[channelIndex];
            const auto laneTop = area.getY() + static_cast<float>(channel) * (laneHeight + gap);
            const auto laneBottom = laneTop + laneHeight;
            const auto centre = laneTop + laneHeight * 0.5f;
            centres[channelIndex] = centre;

            for (std::size_t column = 0; column < columnCount; ++column)
            {
                std::size_t first = firstVisiblePeak + column;
                std::size_t end = first + 1;
                double left = 0.0;
                double right = 0.0;

                if (columnCount == visiblePeakCount)
                {
                    left = std::max(0.0, (static_cast<double>(first) * framesPerPeak
                                          - startFrame) / visibleFrames);
                    right = std::min(1.0, (static_cast<double>(end) * framesPerPeak
                                           - startFrame) / visibleFrames);
                }
                else
                {
                    left = static_cast<double>(column) / static_cast<double>(columnCount);
                    right = static_cast<double>(column + 1) / static_cast<double>(columnCount);

                    // Include every overlapping bin so narrow views retain brief spikes.
                    first = static_cast<std::size_t>(std::min(
                        static_cast<double>(peakCount - 1),
                        std::floor((startFrame + left * visibleFrames) / framesPerPeak)));
                    end = static_cast<std::size_t>(std::min(
                        static_cast<double>(peakCount),
                        std::ceil((startFrame + right * visibleFrames) / framesPerPeak)));
                    end = std::max(first + 1, end);
                    if (column + 1 == columnCount)
                        end = endVisiblePeak;
                }

                const auto x = area.getX() + static_cast<float>(left) * area.getWidth();
                const auto nextX = area.getX() + static_cast<float>(right) * area.getWidth();
                addPeakRange(path, peaks, additionalPeaks, first, end, x, nextX - x, centre,
                             amplitude, laneTop, laneBottom);
            }
        }
    }

    WaveformView& owner;
    juce::ScrollBar scrollBar { false };
    std::shared_ptr<const WaveformData::Summary> summary;
    juce::Range<juce::int64> visibleRange;
    const WaveformData::Summary* geometrySummary = nullptr;
    juce::Rectangle<int> geometryBounds;
    juce::Range<juce::int64> geometryRange;
    ChannelDisplayMode channelDisplayMode = ChannelDisplayMode::combined;
    ChannelDisplayMode geometryMode = ChannelDisplayMode::combined;
    juce::String message { "Select a sample to view its waveform." };
    std::array<juce::Path, 2> paths;
    std::array<float, 2> centres {};
    juce::Rectangle<float> area;
    int channelCount = 0;
    bool validSummary = false;
    double playbackSeconds = 0.0;
    juce::Range<juce::int64> selection;
    bool loopEnabled = false;
    DragMode dragMode = DragMode::none;
    bool dragStarted = false;
    juce::Point<float> pressPosition;
    juce::int64 dragAnchor = 0;
    juce::Range<juce::int64> panRange;
    float panWidth = 0.0f;
};

WaveformView::WaveformView()
    : impl(std::make_unique<Impl>(*this))
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(true, true);
}

WaveformView::~WaveformView() = default;

void WaveformView::setWaveform(const WaveformData::Snapshot& snapshot)
{
    const auto nextSummary = snapshot.state == WaveformData::State::ready
                                 ? snapshot.summary : nullptr;
    if (impl->summary != nextSummary)
    {
        impl->summary = nextSummary;
        impl->validSummary = nextSummary != nullptr && isValidSummary(*nextSummary);
        impl->visibleRange = { 0, impl->validSummary ? nextSummary->totalFrames : 0 };
        impl->playbackSeconds = 0.0;
        impl->selection = {};
        impl->cancelDrag();
        impl->geometrySummary = nullptr;
        impl->update();
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    switch (snapshot.state)
    {
        case WaveformData::State::empty:
            impl->message = "Select a sample to view its waveform.";
            break;
        case WaveformData::State::loading:
            impl->message = "Loading waveform...";
            break;
        case WaveformData::State::ready:
            impl->message = impl->validSummary ? juce::String() : "Waveform unavailable.";
            break;
        case WaveformData::State::error:
            impl->message = snapshot.message.isNotEmpty() ? snapshot.message
                                                         : "Waveform unavailable.";
            break;
    }

    repaint();
}

void WaveformView::setChannelDisplayMode(ChannelDisplayMode mode)
{
    if (impl->channelDisplayMode != mode)
    {
        impl->channelDisplayMode = mode;
        impl->update();
    }
}

void WaveformView::setPlaybackPosition(double seconds)
{
    const auto nextPosition = std::isfinite(seconds) && seconds >= 0.0 ? seconds : -1.0;
    if (impl->playbackSeconds != nextPosition)
    {
        impl->playbackSeconds = nextPosition;
        repaint();
    }
}

void WaveformView::zoomIn()
{
    if (canZoomIn())
        impl->zoomTo(impl->visibleRange.getLength() / 2, 0.5);
}

void WaveformView::zoomOut()
{
    if (canZoomOut())
    {
        const auto length = impl->visibleRange.getLength();
        impl->zoomTo(length >= impl->summary->totalFrames - length
                         ? impl->summary->totalFrames : length * 2, 0.5);
    }
}

void WaveformView::fitToWindow()
{
    if (impl->validSummary)
        impl->setRange(0, impl->summary->totalFrames);
}

void WaveformView::zoomToSelection()
{
    if (!canZoomToSelection())
        return;

    const auto length = std::max(impl->selection.getLength(), impl->minimumLength());
    const auto start = impl->selection.getStart()
        - (length - impl->selection.getLength()) / 2;
    impl->setRange(start, length);
}

bool WaveformView::canZoomIn() const
{
    return impl->validSummary && impl->visibleRange.getLength() > impl->minimumLength();
}

bool WaveformView::canZoomOut() const
{
    return impl->validSummary && impl->visibleRange.getLength() < impl->summary->totalFrames;
}

bool WaveformView::canZoomToSelection() const
{
    return impl->validSummary && !impl->selection.isEmpty();
}

juce::Range<juce::int64> WaveformView::getVisibleFrameRange() const
{
    return impl->visibleRange;
}

void WaveformView::setSelection(juce::Range<juce::int64> frames)
{
    auto next = impl->validSummary
        ? frames.getIntersectionWith({ 0, impl->summary->totalFrames })
        : juce::Range<juce::int64>();
    if (next.isEmpty())
        next = {};

    impl->cancelDrag();
    if (impl->selection != next)
    {
        impl->selection = next;
        repaint();
    }
}

void WaveformView::clearSelection()
{
    setSelection({});
}

juce::Range<juce::int64> WaveformView::getSelection() const
{
    return impl->selection;
}

void WaveformView::setLoopEnabled(bool shouldLoop)
{
    if (impl->loopEnabled != shouldLoop)
    {
        impl->loopEnabled = shouldLoop;
        repaint();
    }
}

void WaveformView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(findColour(juce::ListBox::backgroundColourId));
    graphics.setColour(findColour(juce::ListBox::outlineColourId));
    graphics.drawRect(getLocalBounds());

    if (impl->message.isNotEmpty())
    {
        const auto textBounds = getLocalBounds().reduced(12, 6);
        if (!textBounds.isEmpty())
        {
            graphics.setColour(findColour(juce::Label::textColourId));
            graphics.setFont(juce::FontOptions(13.0f));
            graphics.drawFittedText(impl->message, textBounds, juce::Justification::centred, 1);
        }
        return;
    }

    for (int channel = 0; channel < impl->channelCount; ++channel)
    {
        const auto index = static_cast<std::size_t>(channel);
        graphics.setColour(findColour(juce::ListBox::outlineColourId));
        graphics.drawLine(impl->area.getX(), impl->centres[index],
                          impl->area.getRight(), impl->centres[index], 1.0f);
        graphics.setColour(findColour(juce::Label::textColourId).withMultipliedAlpha(0.85f));
        graphics.fillPath(impl->paths[index]);
    }

    const auto& selection = impl->selection;
    if (impl->channelCount > 0 && !selection.isEmpty())
    {
        const auto& area = impl->area;
        const auto visible = selection.getIntersectionWith(impl->visibleRange);
        if (!visible.isEmpty())
        {
            const auto left = impl->xForBoundary(visible.getStart());
            const auto right = impl->xForBoundary(visible.getEnd());
            graphics.setColour(findColour(juce::TextEditor::highlightColourId).withAlpha(0.45f));
            graphics.fillRect(juce::Rectangle<float>(left, area.getY(), std::max(1.0f, right - left),
                                                     area.getHeight()));
        }

        graphics.setColour(juce::Colour(0xff0078d4));
        for (const auto frame : { selection.getStart(), selection.getEnd() })
        {
            if (!impl->isBoundaryVisible(frame))
                continue;

            const auto x = impl->xForBoundary(frame);
            graphics.drawLine(x, area.getY(), x, area.getBottom(), 1.5f);
            graphics.fillRect(juce::Rectangle<float>(x - 4.0f, area.getY(), 8.0f, 6.0f));
        }
    }

    if (impl->channelCount > 0 && impl->playbackSeconds >= 0.0)
    {
        const auto frame = impl->playbackSeconds * impl->summary->sampleRate;
        const auto start = static_cast<double>(impl->visibleRange.getStart());
        const auto end = static_cast<double>(impl->visibleRange.getEnd());
        if (std::isfinite(frame) && frame >= start && frame < end)
        {
            const auto fraction = (frame - start)
                / static_cast<double>(impl->visibleRange.getLength());
            const auto x = impl->area.getX() + static_cast<float>(fraction) * impl->area.getWidth();
            graphics.setColour(findColour(juce::TextEditor::highlightColourId));
            graphics.drawLine(x, impl->area.getY(), x, impl->area.getBottom(), 1.0f);
        }
    }

    if (impl->channelCount > 0 && impl->loopEnabled)
    {
        const juce::String text = selection.isEmpty() ? "Loop whole sample" : "Loop selection";
        const juce::Font font(juce::FontOptions(11.0f));
        const auto width = juce::GlyphArrangement::getStringWidth(font, text) + 12.0f;
        const juce::Rectangle<float> badge(impl->area.getRight() - width - 4.0f,
                                           impl->area.getY() + 4.0f, width, 18.0f);
        if (badge.getX() >= impl->area.getX() && badge.getBottom() <= impl->area.getBottom())
        {
            graphics.setColour(findColour(juce::ListBox::backgroundColourId).withAlpha(0.85f));
            graphics.fillRoundedRectangle(badge, 3.0f);
            graphics.setColour(juce::Colour(0xff0078d4));
            graphics.drawRoundedRectangle(badge, 3.0f, 1.0f);
            graphics.setColour(findColour(juce::Label::textColourId));
            graphics.setFont(font);
            graphics.drawText(text, badge, juce::Justification::centred, false);
        }
    }
}

void WaveformView::resized()
{
    impl->update();
}

void WaveformView::mouseDown(const juce::MouseEvent& event)
{
    impl->cancelDrag();
    if (event.eventComponent != this || event.originalComponent != this
        || !impl->validSummary || !impl->area.contains(event.position))
        return;

    if (event.mods.isRightButtonDown() && !event.mods.isLeftButtonDown())
    {
        if (canZoomOut())
        {
            impl->dragMode = Impl::DragMode::pan;
            impl->pressPosition = event.position;
            impl->panRange = impl->visibleRange;
            impl->panWidth = impl->area.getWidth();
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        return;
    }

    if (!event.mods.isLeftButtonDown() || event.mods.isPopupMenu())
        return;

    impl->pressPosition = event.position;
    const auto boundary = impl->boundaryNear(event.position);
    if (boundary == Impl::Boundary::none)
    {
        impl->dragMode = Impl::DragMode::create;
        impl->dragAnchor = impl->boundaryAt(event.position.x);
    }
    else
    {
        impl->dragMode = Impl::DragMode::adjust;
        impl->dragAnchor = boundary == Impl::Boundary::start ? impl->selection.getEnd()
                                                             : impl->selection.getStart();
    }
}

void WaveformView::mouseDrag(const juce::MouseEvent& event)
{
    if (impl->dragMode == Impl::DragMode::none || !impl->validSummary)
        return;

    if (impl->dragMode == Impl::DragMode::pan)
    {
        if (!event.mods.isRightButtonDown())
            return;

        const auto displacement = static_cast<double>(event.position.x - impl->pressPosition.x)
            / static_cast<double>(impl->panWidth);
        const auto start = static_cast<double>(impl->panRange.getStart())
            - displacement * static_cast<double>(impl->panRange.getLength());
        impl->setRange(boundedFrame(std::round(start),
                                    impl->summary->totalFrames - impl->panRange.getLength()),
                       impl->panRange.getLength());
        return;
    }

    if (!impl->dragStarted)
    {
        if (event.position.getDistanceFrom(impl->pressPosition) < clickDistance)
            return;

        impl->dragStarted = true;
    }

    const auto next = impl->rangeBetween(impl->dragAnchor, impl->boundaryAt(event.position.x));
    if (impl->selection != next)
    {
        impl->selection = next;
        repaint();
    }
}

void WaveformView::mouseUp(const juce::MouseEvent&)
{
    const auto mode = impl->dragMode;
    const auto dragged = impl->dragStarted;
    impl->cancelDrag();
    if (mode == Impl::DragMode::none || mode == Impl::DragMode::pan || !impl->validSummary)
        return;

    auto selectionCallback = onSelectionChanged;
    if (dragged)
    {
        if (selectionCallback)
            selectionCallback(impl->selection);
        return;
    }

    const auto frame = impl->frameAt(impl->pressPosition.x);
    auto seekCallback = onSeekRequested;
    if (!impl->selection.isEmpty())
    {
        impl->selection = {};
        repaint();
        if (selectionCallback)
            selectionCallback({});
    }
    if (seekCallback)
        seekCallback(frame);
}

void WaveformView::mouseMove(const juce::MouseEvent& event)
{
    const auto nearBoundary = event.eventComponent == this && event.originalComponent == this
        && impl->boundaryNear(event.position) != Impl::Boundary::none;
    setMouseCursor(nearBoundary ? juce::MouseCursor::LeftRightResizeCursor
                                : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseWheelMove(const juce::MouseEvent& event,
                                  const juce::MouseWheelDetails& wheel)
{
    if (event.eventComponent != this || event.originalComponent != this
        || !impl->validSummary || !impl->area.contains(event.position)
        || !std::isfinite(wheel.deltaX) || !std::isfinite(wheel.deltaY))
        return;

    const auto horizontal = std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (event.mods.isShiftDown() || horizontal)
    {
        const auto delta = static_cast<double>(horizontal ? wheel.deltaX : wheel.deltaY);
        const auto start = static_cast<double>(impl->visibleRange.getStart())
            - delta * static_cast<double>(impl->visibleRange.getLength());
        impl->setRange(boundedFrame(std::round(start),
                                    impl->summary->totalFrames - impl->visibleRange.getLength()),
                       impl->visibleRange.getLength());
        return;
    }

    if (wheel.deltaY != 0.0f)
    {
        const auto factor = std::exp2(std::clamp(-4.0 * static_cast<double>(wheel.deltaY),
                                                -4.0, 4.0));
        const auto length = boundedFrame(
            std::round(static_cast<double>(impl->visibleRange.getLength()) * factor),
            impl->summary->totalFrames);
        const auto anchor = static_cast<double>(event.position.x - impl->area.getX())
            / static_cast<double>(impl->area.getWidth());
        impl->zoomTo(length, anchor);
    }
}
}
