#include "DesktopLayout.h"
#include "PreviewCommands.h"
#include "SampleDetails.h"
#include "WaveformView.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace wavecrate
{
namespace
{
class DesktopLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DesktopLookAndFeel()
    {
        setDefaultSansSerifTypefaceName("Segoe UI");
        updateColours();
    }

    void updateColours()
    {
        const auto dark = juce::Desktop::getInstance().isDarkModeActive();
        setColourScheme(dark ? getDarkColourScheme() : getLightColourScheme());
        const juce::Colour chrome(dark ? 0xff252525 : 0xfff3f3f3);
        const juce::Colour surface(dark ? 0xff1c1c1c : 0xffffffff);
        const juce::Colour text(dark ? 0xffeeeeee : 0xff202020);
        const juce::Colour border(dark ? 0xff444444 : 0xffd4d4d4);
        const juce::Colour selection(dark ? 0xff264f73 : 0xffcce8ff);
        setColour(juce::ResizableWindow::backgroundColourId, chrome);
        setColour(juce::Label::textColourId, text);
        setColour(juce::ListBox::backgroundColourId, surface);
        setColour(juce::ListBox::textColourId, text);
        setColour(juce::ListBox::outlineColourId, border);
        setColour(juce::TreeView::backgroundColourId, surface);
        setColour(juce::TreeView::linesColourId, border);
        setColour(juce::TreeView::selectedItemBackgroundColourId, selection);
        setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, selection);
        setColour(juce::DirectoryContentsDisplayComponent::textColourId, text);
        setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId, text);
        setColour(juce::TextEditor::highlightColourId, selection);
        setColour(juce::TextEditor::highlightedTextColourId, text);
        setColour(juce::TextButton::buttonColourId, surface);
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0078d4));
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        setColour(juce::FileBrowserComponent::currentPathBoxBackgroundColourId, surface);
        setColour(juce::FileBrowserComponent::currentPathBoxTextColourId, text);
        setColour(juce::FileBrowserComponent::currentPathBoxArrowColourId, text);
        setColour(juce::ComboBox::backgroundColourId, surface);
        setColour(juce::ComboBox::outlineColourId, border);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, text);
        setColour(juce::Toolbar::backgroundColourId, chrome);
        setColour(juce::Toolbar::separatorColourId, border);
        setColour(juce::Toolbar::labelTextColourId, text);
        setColour(juce::Toolbar::buttonMouseOverBackgroundColourId, selection.withAlpha(0.6f));
        setColour(juce::Toolbar::buttonMouseDownBackgroundColourId, selection);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::Font(juce::FontOptions(13.0f));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(13.0f));
    }

    void drawButtonBackground(juce::Graphics& graphics, juce::Button& button,
                              const juce::Colour& background, bool over, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = background;
        if ((over || down) && button.isEnabled())
            fill = button.getToggleState() ? background.brighter(0.15f)
                                           : findColour(juce::TextEditor::highlightColourId);
        graphics.setColour(fill);
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(findColour(juce::ListBox::outlineColourId)
                               .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        graphics.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    void drawStretchableLayoutResizerBar(juce::Graphics& graphics, int width, int height,
                                         bool vertical, bool over, bool dragging) override
    {
        graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        graphics.setColour(over || dragging ? juce::Colour(0xff0078d4)
                                           : findColour(juce::ListBox::outlineColourId));
        if (vertical)
            graphics.fillRect(width / 2, 0, 1, height);
        else
            graphics.fillRect(0, height / 2, width, 1);
    }

    void layoutFileBrowserComponent(juce::FileBrowserComponent& component,
                                     juce::DirectoryContentsDisplayComponent* files,
                                     juce::FilePreviewComponent*, juce::ComboBox* path,
                                     juce::TextEditor* filename, juce::Button* up) override
    {
        auto bounds = component.getLocalBounds();
        filename->setVisible(false);
        up->setVisible(false);
        path->setBounds(bounds.removeFromTop(26));
        bounds.removeFromTop(6);
        if (auto* fileComponent = dynamic_cast<juce::Component*>(files))
            fileComponent->setBounds(bounds);
    }
};

class FolderPane final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::AsyncUpdater
{
public:
    FolderPane()
        : browser(juce::FileBrowserComponent::openMode
                  | juce::FileBrowserComponent::canSelectDirectories
                  | juce::FileBrowserComponent::useTreeView,
                  juce::File(), nullptr, nullptr)
    {
        heading.setText("Folders", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(14.0f));
        addAndMakeVisible(heading);
        openButton.setTooltip("Choose a folder (Ctrl+O)");
        addAndMakeVisible(openButton);
        addAndMakeVisible(browser);
        if (auto* tree = dynamic_cast<juce::FileTreeComponent*>(browser.getDisplayComponent()))
        {
            tree->setItemHeight(24);
            tree->setRootItemVisible(true);
            tree->directoryContentsList.addChangeListener(this);
        }
        revealRoot();
    }

    ~FolderPane() override
    {
        cancelPendingUpdate();
        browser.getDisplayComponent()->directoryContentsList.removeChangeListener(this);
    }

    void revealRoot()
    {
        rootPending = true;
        triggerAsyncUpdate();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(findColour(juce::ListBox::backgroundColourId));
        graphics.setColour(findColour(juce::ResizableWindow::backgroundColourId));
        graphics.fillRect(getLocalBounds().removeFromTop(28));
        graphics.setColour(findColour(juce::ListBox::outlineColourId));
        graphics.drawRect(getLocalBounds());
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);
        auto header = bounds.removeFromTop(22);
        openButton.setBounds(header.removeFromRight(72));
        header.removeFromRight(6);
        heading.setBounds(header);
        bounds.removeFromTop(6);
        browser.setBounds(bounds);
    }

    juce::FileBrowserComponent browser;
    juce::TextButton openButton { "Open..." };

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        if (rootPending)
            triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        auto* tree = dynamic_cast<juce::FileTreeComponent*>(browser.getDisplayComponent());
        auto* root = tree != nullptr ? tree->getRootItem() : nullptr;

        // JUCE recreates its root asynchronously, after browserRootChanged.
        // Open only that root, not every directory discovered below it.
        if (!rootPending || root == nullptr
            || root->getUniqueName() != browser.getRoot().getFullPathName())
            return;

        rootPending = false;
        root->setOpen(true);
        root->setSelected(true, true);
    }

    juce::Label heading;
    bool rootPending = false;
};

class SampleWorkspace final : public juce::Component
{
public:
    SampleWorkspace() : divider(&layout, 1, true)
    {
        addAndMakeVisible(folders);
        addAndMakeVisible(divider);
        addAndMakeVisible(samples);
        layout.setItemLayout(0, 180, -0.6, 250);
        layout.setItemLayout(1, 5, 5, 5);
        layout.setItemLayout(2, 250, -1.0, -0.75);
    }

    void resized() override
    {
        juce::Component* panels[] { &folders, &divider, &samples };
        layout.layOutComponents(panels, 3, 0, 0, getWidth(), getHeight(), false, true);
    }

    FolderPane folders;
    SampleBrowser samples;

private:
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar divider;
};

class WaveformPane final : public juce::Component
{
public:
    enum ChannelViewItem { combinedView = 1, stereoView };
    static constexpr int metadataRowHeight = 26;

    WaveformPane()
    {
        addAndMakeVisible(display);
        metadata.setName("Sample details");
        metadata.setFont(juce::FontOptions(12.0f));
        metadata.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(metadata);
        transport.setName("Sample transport");
        transport.setStyle(juce::Toolbar::iconsWithText);
        addAndMakeVisible(transport);
        position.setName("Playback position");
        position.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(position);
        zoomOut.setTooltip("Zoom out");
        zoomIn.setTooltip("Zoom in (mouse wheel over waveform)");
        fit.setTooltip("Show the full waveform");
        zoomSelection.setTooltip("Show the selected region (Z; limited to overview resolution)");
        for (auto* button : { &zoomOut, &zoomIn, &fit, &zoomSelection })
        {
            button->setEnabled(false);
            button->setMouseClickGrabsKeyboardFocus(false);
            addAndMakeVisible(button);
        }
        zoomOut.onClick = [this] { display.zoomOut(); updateNavigationButtons(); };
        zoomIn.onClick = [this] { display.zoomIn(); updateNavigationButtons(); };
        fit.onClick = [this] { display.fitToWindow(); updateNavigationButtons(); };
        zoomSelection.onClick = [this] { display.zoomToSelection(); updateNavigationButtons(); };
        const juce::String loopHelp("Loop the selection, or the whole sample when nothing is selected (L)");
        loop.setClickingTogglesState(true);
        loop.setTooltip(loopHelp);
        loop.setDescription(loopHelp);
        loop.setEnabled(false);
        loop.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(loop);
        channelView.setName("Waveform channel view");
        channelView.setTooltip("Waveform display: combined peaks or separate stereo channels. Playback is unchanged.");
        channelView.setDescription("Choose a combined waveform or separate stereo channels.");
        channelView.addItem("View: Combined", combinedView);
        channelView.addItem("View: Stereo", stereoView);
        channelView.setSelectedId(combinedView, juce::dontSendNotification);
        channelView.onChange = [this]
        {
            if (channelView.getSelectedId() == combinedView)
                display.setChannelDisplayMode(WaveformView::ChannelDisplayMode::combined);
            else if (channelView.getSelectedId() == stereoView)
                display.setChannelDisplayMode(WaveformView::ChannelDisplayMode::separate);
        };
        addAndMakeVisible(channelView);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        auto transportBounds = bounds.removeFromBottom(44).reduced(6, 0);
        metadata.setBounds(bounds.removeFromBottom(metadataRowHeight).reduced(6, 0));
        display.setBounds(bounds.reduced(6));
        transport.setBounds(transportBounds.removeFromLeft(110));
        auto navigation = transportBounds.removeFromLeft(224).withSizeKeepingCentre(224, 26);
        zoomOut.setBounds(navigation.removeFromLeft(30));
        navigation.removeFromLeft(4);
        zoomIn.setBounds(navigation.removeFromLeft(30));
        navigation.removeFromLeft(4);
        fit.setBounds(navigation.removeFromLeft(40));
        navigation.removeFromLeft(4);
        zoomSelection.setBounds(navigation.removeFromLeft(108));
        transportBounds.removeFromLeft(6);
        loop.setBounds(transportBounds.removeFromLeft(56).withSizeKeepingCentre(56, 26));
        transportBounds.removeFromLeft(6);
        channelView.setBounds(transportBounds.removeFromLeft(132).withSizeKeepingCentre(132, 26));
        position.setBounds(transportBounds.reduced(8, 0));
    }

    void updateNavigationButtons()
    {
        zoomIn.setEnabled(display.canZoomIn());
        zoomOut.setEnabled(display.canZoomOut());
        fit.setEnabled(display.canZoomOut());
        zoomSelection.setEnabled(display.canZoomToSelection());
    }

    WaveformView display;
    juce::Label metadata;
    juce::Toolbar transport;
    juce::Label position;
    juce::TextButton zoomOut { "-" };
    juce::TextButton zoomIn { "+" };
    juce::TextButton fit { "Fit" };
    juce::TextButton zoomSelection { "Zoom selection" };
    juce::TextButton loop { "Loop" };
    juce::ComboBox channelView;
};

juce::String formatTime(double seconds)
{
    const auto whole = static_cast<juce::int64>(juce::jmax(0.0, seconds));
    return juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
}

juce::String formatPreciseTime(double seconds)
{
    const auto milliseconds = static_cast<juce::int64>(std::round(juce::jmax(0.0, seconds) * 1000.0));
    return formatTime(static_cast<double>(milliseconds / 1000)) + "."
        + juce::String(milliseconds % 1000).paddedLeft('0', 3);
}
}

struct DesktopLayout::Impl final : juce::ToolbarItemFactory,
                                   juce::FileBrowserListener,
                                   juce::DarkModeSettingListener
{
    enum Item { play = 1, stop };

    explicit Impl(DesktopLayout& component)
        : owner(component), divider(&layout, 1, false)
    {
        owner.setLookAndFeel(&lookAndFeel);
        owner.setOpaque(true);
        waveform.transport.addDefaultItems(*this);
        waveform.display.onSeekRequested = [this](juce::int64 frame)
        {
            if (owner.onSeekRequested)
                owner.onSeekRequested(frame);
        };
        waveform.display.onSelectionChanged = [this](juce::Range<juce::int64> frames)
        {
            waveform.updateNavigationButtons();
            if (owner.onSelectionChanged)
                owner.onSelectionChanged(frames);
        };
        waveform.loop.onClick = [this]
        {
            if (owner.onLoopToggled)
                owner.onLoopToggled(waveform.loop.getToggleState());
        };
        workspace.folders.openButton.onClick = [this] { owner.showFolderChooser(); };
        owner.addAndMakeVisible(waveform);
        owner.addAndMakeVisible(divider);
        owner.addAndMakeVisible(workspace);
        status.setFont(juce::FontOptions(12.0f));
        status.setText("Open a folder to browse samples.", juce::dontSendNotification);
        owner.addAndMakeVisible(status);
        shortcuts.setFont(juce::FontOptions(12.0f));
        shortcuts.setJustificationType(juce::Justification::centredRight);
        shortcuts.setText("Space: Play/Stop   Enter: Play   Esc: Stop", juce::dontSendNotification);
        owner.addAndMakeVisible(shortcuts);
        layout.setItemLayout(0, 145 + WaveformPane::metadataRowHeight, -0.6,
                             217 + WaveformPane::metadataRowHeight);
        layout.setItemLayout(1, 5, 5, 5);
        layout.setItemLayout(2, 160, -1.0, -0.7);
        workspace.folders.browser.addListener(this);
        juce::Desktop::getInstance().addDarkModeSettingListener(this);
        owner.sendLookAndFeelChange();
        displayFolder(workspace.folders.browser.getRoot());
    }

    ~Impl() override
    {
        juce::Desktop::getInstance().removeDarkModeSettingListener(this);
        workspace.folders.browser.removeListener(this);
        owner.setLookAndFeel(nullptr);
    }

    void getAllToolbarItemIds(juce::Array<int>& ids) override
    {
        ids = { play, stop };
    }

    void getDefaultItemSet(juce::Array<int>& ids) override { getAllToolbarItemIds(ids); }

    juce::ToolbarItemComponent* createItem(int item) override
    {
        juce::Path path;
        juce::String label;
        juce::String tooltip;
        auto colour = lookAndFeel.findColour(juce::Label::textColourId);
        if (item == play)
        {
            label = "Play";
            tooltip = "Preview the selected sample (Space or Enter)";
            path.addTriangle(5, 3, 20, 12, 5, 21);
            colour = juce::Colour(0xff16844b);
        }
        else if (item == stop)
        {
            label = "Stop";
            tooltip = "Stop playback (Escape)";
            path.addRectangle(4, 4, 16, 16);
        }
        else
        {
            return nullptr;
        }
        auto icon = std::make_unique<juce::DrawablePath>();
        icon->setPath(path);
        icon->setFill(colour);
        auto* button = new juce::ToolbarButton(item, label, std::move(icon), nullptr);
        button->setTooltip(tooltip);
        button->onClick = [this, item]
        {
            if (item == play && owner.onPlayRequested) owner.onPlayRequested();
            else if (item == stop && owner.onStopRequested) owner.onStopRequested();
        };
        return button;
    }

    void enableItem(int item, bool enabled)
    {
        auto& toolbar = waveform.transport;
        for (int index = 0; index < toolbar.getNumItems(); ++index)
            if (toolbar.getItemId(index) == item)
                toolbar.getItemComponent(index)->setEnabled(enabled);
    }

    void displayFolder(const juce::File& folder)
    {
        currentFolder = folder;
        workspace.samples.openFolder(folder);
    }

    void navigateTo(const juce::File& folder)
    {
        const bool sameRoot = workspace.folders.browser.getRoot() == folder;
        workspace.folders.browser.setRoot(folder);
        if (sameRoot)
        {
            workspace.folders.revealRoot();
            displayFolder(folder);
        }
    }

    void selectionChanged() override
    {
        auto& browser = workspace.folders.browser;
        const auto folder = browser.getDisplayComponent()->getSelectedFile(0);
        if (folder == juce::File())
            return;

        if (folder == currentFolder)
        {
            if (pendingFolder != juce::File())
            {
                ++navigationRevision;
                pendingFolder = {};
            }
            return;
        }

        if (pendingFolder != folder)
        {
            pendingFolder = folder;
            ++navigationRevision;
        }

        const auto root = browser.getRoot();
        const auto revision = navigationRevision;
        const juce::Component::SafePointer<DesktopLayout> safe(&owner);
        // Client callbacks may destroy the layout, so leave JUCE's tree callback first.
        juce::MessageManager::callAsync([safe, root, folder, revision]
        {
            if (safe == nullptr)
                return;
            auto& implementation = *safe->impl;
            auto& currentBrowser = implementation.workspace.folders.browser;
            if (implementation.navigationRevision == revision
                && currentBrowser.getRoot() == root
                && currentBrowser.getDisplayComponent()->getSelectedFile(0) == folder
                && implementation.currentFolder != folder)
            {
                implementation.pendingFolder = {};
                implementation.displayFolder(folder);
            }
            else if (implementation.navigationRevision == revision)
            {
                implementation.pendingFolder = {};
            }
        });
    }

    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File&) override {}
    void browserRootChanged(const juce::File& folder) override
    {
        ++navigationRevision;
        pendingFolder = {};
        workspace.folders.revealRoot();
        displayFolder(folder);
    }

    void darkModeSettingChanged() override
    {
        lookAndFeel.updateColours();
        owner.sendLookAndFeelChange();
        waveform.transport.clear();
        waveform.transport.addDefaultItems(*this);
        owner.updatePreview(lastSnapshot);
    }

    void resized()
    {
        auto bounds = owner.getLocalBounds();
        auto footer = bounds.removeFromBottom(26).reduced(6, 0);
        shortcuts.setBounds(footer.removeFromRight(280));
        status.setBounds(footer);
        bounds.reduce(4, 4);
        juce::Component* panels[] { &waveform, &divider, &workspace };
        layout.layOutComponents(panels, 3, bounds.getX(), bounds.getY(), bounds.getWidth(),
                               bounds.getHeight(), true, true);
    }

    DesktopLayout& owner;
    DesktopLookAndFeel lookAndFeel;
    WaveformPane waveform;
    SampleWorkspace workspace;
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar divider;
    juce::Label status;
    juce::Label shortcuts;
    juce::File currentFolder;
    juce::File pendingFolder;
    AudioPreview::Snapshot lastSnapshot;
    std::uint64_t navigationRevision = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

DesktopLayout::DesktopLayout()
{
    impl = std::make_unique<Impl>(*this);
}
DesktopLayout::~DesktopLayout() = default;
SampleBrowser& DesktopLayout::getSamples() { return impl->workspace.samples; }

void DesktopLayout::showFolderChooser()
{
    if (impl->chooser != nullptr)
        return;
    impl->chooser = std::make_unique<juce::FileChooser>("Open a folder of WAV files",
                                                       impl->currentFolder, juce::String(),
                                                       true, false, this);
    const juce::Component::SafePointer<DesktopLayout> safe(this);
    impl->workspace.folders.openButton.setEnabled(false);
    impl->chooser->launchAsync(juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectDirectories,
                               [safe](const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;
        const auto folder = chooser.getResult();
        safe->impl->workspace.folders.openButton.setEnabled(true);
        if (folder != juce::File())
            safe->impl->navigateTo(folder);
        juce::MessageManager::callAsync([safe]
        {
            if (safe != nullptr)
                safe->impl->chooser.reset();
        });
    });
}

bool DesktopLayout::folderBrowserHasFocus() const
{
    return impl->workspace.folders.browser.hasKeyboardFocus(true);
}

bool DesktopLayout::zoomToSelection()
{
    auto& display = impl->waveform.display;
    if (!display.canZoomToSelection())
        return false;

    display.zoomToSelection();
    impl->waveform.updateNavigationButtons();
    return true;
}

void DesktopLayout::updateWaveform(const WaveformData::Snapshot& snapshot)
{
    impl->waveform.display.setWaveform(snapshot);
    impl->waveform.updateNavigationButtons();
}

void DesktopLayout::updatePreview(const AudioPreview::Snapshot& snapshot)
{
    impl->lastSnapshot = snapshot;
    impl->waveform.display.setPlaybackPosition(snapshot.positionSeconds);
    impl->waveform.updateNavigationButtons();
    juce::String metadata;
    if (snapshot.state == AudioPreview::State::ready || snapshot.state == AudioPreview::State::playing)
    {
        metadata = "Duration: " + formatSampleDuration(snapshot.durationSeconds)
            + "   Size: " + formatSampleSize(snapshot.fileSizeBytes)
            + "   Channels: " + (snapshot.channels > 0 ? juce::String(snapshot.channels) : juce::String("-"))
            + "   Sample rate: " + formatSampleRate(snapshot.sampleRate)
            + "   Bit depth: " + (snapshot.bitDepth > 0 ? juce::String(snapshot.bitDepth) + " bit" : juce::String("-"))
            + "   Audio bitrate: " + formatSampleBitRate(snapshot.sampleRate, snapshot.channels, snapshot.bitDepth);
    }
    impl->waveform.metadata.setText(metadata, juce::dontSendNotification);
    impl->waveform.metadata.setTooltip(metadata);
    impl->waveform.metadata.setDescription(metadata);
    auto positionText = snapshot.durationSeconds > 0
        ? formatTime(snapshot.positionSeconds) + " / " + formatTime(snapshot.durationSeconds)
        : juce::String("--:-- / --:--");
    const auto region = snapshot.region;
    if (snapshot.durationSeconds > 0 && snapshot.sampleRate > 0 && !region.isEmpty())
    {
        const auto start = static_cast<double>(region.getStart()) / snapshot.sampleRate;
        const auto end = static_cast<double>(region.getEnd()) / snapshot.sampleRate;
        positionText += "  Selection " + formatPreciseTime(start) + "-" + formatPreciseTime(end)
            + " (" + juce::String(end - start, 3) + " s)";
    }
    if (snapshot.durationSeconds > 0 && snapshot.loopEnabled)
        positionText += region.isEmpty() ? "  Loop: whole sample" : "  Loop: selection";
    impl->waveform.position.setText(positionText, juce::dontSendNotification);
    impl->waveform.position.setTooltip(positionText);
    impl->waveform.loop.setEnabled(snapshot.state != AudioPreview::State::empty
                                   && snapshot.state != AudioPreview::State::error);
    impl->waveform.loop.setToggleState(snapshot.loopEnabled, juce::dontSendNotification);
    impl->waveform.display.setLoopEnabled(snapshot.loopEnabled);
    auto message = snapshot.message;
    if (!snapshot.audioAvailable && snapshot.deviceMessage.isNotEmpty())
        message += (message.isEmpty() ? "" : "  ") + snapshot.deviceMessage;
    if (message.isEmpty())
        message = "Select a sample to preview.";
    impl->status.setText(message, juce::dontSendNotification);
    impl->status.setTooltip(message);
    impl->status.setColour(juce::Label::textColourId, snapshot.state == AudioPreview::State::error
        ? juce::Colour(0xffd13438) : findColour(juce::Label::textColourId));
    impl->enableItem(Impl::play, snapshot.audioAvailable && !snapshot.playRequested
        && (snapshot.state == AudioPreview::State::ready || snapshot.state == AudioPreview::State::loading));
    impl->enableItem(Impl::stop, canStopPreview(snapshot));
}

void DesktopLayout::paint(juce::Graphics& graphics)
{
    graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void DesktopLayout::resized()
{
    if (impl != nullptr)
        impl->resized();
}
}
