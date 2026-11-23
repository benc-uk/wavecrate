#include "SampleBrowser.h"
#include "SampleDetails.h"
#include "ValidatedWavReader.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace wavecrate
{
namespace
{
void assertMessageThread()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
}

struct SampleEntry
{
    juce::File file;
    juce::String name;
    juce::int64 size = -1;
    double duration = 0.0;
    double sampleRate = 0.0;
    unsigned int channels = 0;
    unsigned int bitDepth = 0;
    bool metadataReady = false;
    juce::String error;
};

struct ScanResult
{
    std::vector<SampleEntry> entries;
    juce::String error;
};

struct MetadataUpdate
{
    std::size_t row;
    SampleEntry entry;
};

struct ScanPublication
{
    std::optional<ScanResult> result;
    std::vector<MetadataUpdate> metadata;
    bool finished = false;
};

class FolderScanner final
{
public:
    FolderScanner()
        : worker([this](std::stop_token stop) { run(stop); })
    {
    }

    ~FolderScanner()
    {
        ++generation;
        worker.request_stop();
        wake.notify_all();
    }

    void request(const juce::File& folder)
    {
        const std::scoped_lock lock(mutex);
        pending = Request { ++generation, folder };
        completed.reset();
        metadata.clear();
        finished = false;
        wake.notify_all();
    }

    ScanPublication takeResult()
    {
        const std::scoped_lock lock(mutex);
        ScanPublication publication { std::exchange(completed, std::nullopt),
                                      std::exchange(metadata, {}), finished };
        wake.notify_all();
        return publication;
    }

private:
    struct Request
    {
        std::uint64_t generation;
        juce::File folder;
    };

    bool cancelled(const Request& request, std::stop_token stop) const
    {
        return stop.stop_requested() || request.generation != generation.load();
    }

    ScanResult scan(const Request& request, std::stop_token stop) const
    {
        ScanResult result;

        if (request.folder == juce::File())
        {
            result.error = "No folder was selected.";
            return result;
        }

        const auto fail = [&result](const std::error_code& error)
        {
            result.entries.clear();
            result.error = "Cannot read folder: " + juce::String(error.message());
        };

        try
        {
            const std::filesystem::path path(request.folder.getFullPathName().toWideCharPointer());
            std::error_code error;
            std::filesystem::directory_iterator iterator(path, error);
            const std::filesystem::directory_iterator end;

            if (error)
            {
                fail(error);
                return result;
            }

            while (iterator != end && !cancelled(request, stop))
            {
                const juce::String name(iterator->path().filename().wstring().c_str());

                if (name.endsWithIgnoreCase(".wav"))
                {
                    const auto regularFile = iterator->is_regular_file(error);

                    if (error)
                    {
                        fail(error);
                        return result;
                    }

                    if (regularFile)
                    {
                        SampleEntry entry;
                        entry.file = request.folder.getChildFile(name);
                        entry.name = name;
                        const auto size = iterator->file_size(error);

                        if (error)
                        {
                            entry.error = "Cannot read file size: " + juce::String(error.message());
                            error.clear();
                        }
                        else
                        {
                            entry.size = static_cast<juce::int64>(size);
                        }

                        result.entries.push_back(std::move(entry));
                    }
                }

                iterator.increment(error);

                if (error)
                {
                    fail(error);
                    return result;
                }
            }
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            fail(error.code());
            return result;
        }

        if (!cancelled(request, stop))
        {
            std::sort(result.entries.begin(), result.entries.end(),
                      [](const SampleEntry& left, const SampleEntry& right)
                      {
                          const auto comparison = left.name.compareIgnoreCase(right.name);
                          return comparison != 0 ? comparison < 0 : left.name.compare(right.name) < 0;
                      });
        }

        return result;
    }

    void readMetadata(SampleEntry& entry, const Request& request) const
    {
        std::atomic<bool> readFailed { false };
        juce::String error;
        juce::int64 openedSize = -1;
        const auto reader = createValidatedWavReader(entry.file, generation, request.generation,
                                                     readFailed, error, &openedSize);

        if (reader != nullptr)
        {
            entry.size = openedSize;
            entry.duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            entry.sampleRate = reader->sampleRate;
            entry.channels = reader->numChannels;
            entry.bitDepth = reader->bitsPerSample;
            entry.error.clear();
        }
        else
        {
            if (entry.error.isNotEmpty())
                entry.error += "\n";

            entry.error += error.isNotEmpty() ? error : juce::String("Cannot read WAV metadata.");
        }

        entry.metadataReady = true;
    }

    void run(std::stop_token stop)
    {
        while (!stop.stop_requested())
        {
            Request next;

            {
                std::unique_lock lock(mutex);

                if (!wake.wait(lock, stop, [this] { return pending.has_value(); }))
                    return;

                next = std::move(*pending);
                pending.reset();
            }

            auto result = scan(next, stop);
            auto publication = result;

            {
                const std::scoped_lock lock(mutex);

                if (!cancelled(next, stop))
                    completed = std::move(publication);
            }

            for (std::size_t row = 0; row < result.entries.size() && !cancelled(next, stop); ++row)
            {
                auto entry = std::move(result.entries[row]);
                readMetadata(entry, next);
                std::unique_lock lock(mutex);

                // Bound pending UI updates, including when the message thread is busy.
                if (!wake.wait(lock, stop, [this, &next, stop]
                    { return metadata.size() < 64 || cancelled(next, stop); }))
                    return;

                if (cancelled(next, stop))
                    break;

                metadata.push_back({ row, std::move(entry) });
            }

            {
                const std::scoped_lock lock(mutex);

                if (!cancelled(next, stop))
                    finished = true;
            }
        }
    }

    // The worker only exchanges data. Shutdown never waits for a message-thread callback.
    std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Request> pending;
    std::optional<ScanResult> completed;
    std::vector<MetadataUpdate> metadata;
    bool finished = false;
    std::atomic<std::uint64_t> generation { 0 };
    std::jthread worker;
};
}

struct SampleBrowser::Impl final : juce::TableListBoxModel, private juce::Timer
{
    enum class Command
    {
        play,
        toggle,
        stop
    };

    class SampleList final : public juce::TableListBox
    {
    public:
        explicit SampleList(Impl& implementation)
            : juce::TableListBox("WAV sample list", &implementation), impl(implementation)
        {
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key.isKeyCode(juce::KeyPress::spaceKey))
            {
                impl.postCommand(Command::toggle);
                return true;
            }

            if (key.isKeyCode(juce::KeyPress::escapeKey))
            {
                impl.postCommand(Command::stop);
                return true;
            }

            return juce::ListBox::keyPressed(key);
        }

        juce::String getNameForRow(int row) override
        {
            return impl.getNameForRow(row);
        }

        juce::String getTooltipForRow(int row) override
        {
            return impl.getCellTooltip(row, 1);
        }

        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override
        {
            impl.cellDoubleClicked(row, 1, event);
        }

    private:
        Impl& impl;
    };

    explicit Impl(SampleBrowser& browser)
        : owner(browser), list(*this), tooltip(&browser)
    {
        statusLabel.setName("Folder status");
        statusLabel.setFont(juce::FontOptions(12.0f));
        setStatus("Open a folder to browse WAV files.");
        owner.addAndMakeVisible(statusLabel);

        list.setTitle("WAV sample list");
        list.setDescription("Select a WAV file. Space toggles preview, Enter plays, Escape stops.");
        list.setMultipleSelectionEnabled(false);
        list.setRowHeight(24);
        list.setWantsKeyboardFocus(true);
        list.setOutlineThickness(0);
        list.setHeaderHeight(24);
        list.setAutoSizeMenuOptionShown(false);
        const auto columnFlags = juce::TableHeaderComponent::visible
                               | juce::TableHeaderComponent::resizable;
        auto& header = list.getHeader();
        header.addColumn("Name", 1, 220, 100, 1600, columnFlags);
        header.addColumn("Duration", 2, 88, 76, 300, columnFlags);
        header.addColumn("Size", 3, 82, 64, 300, columnFlags);
        header.addColumn("Channels", 4, 76, 70, 300, columnFlags);
        header.addColumn("Sample rate", 5, 96, 90, 300, columnFlags);
        header.addColumn("Bitrate", 6, 108, 90, 300, columnFlags);
        owner.addAndMakeVisible(list);
        applyColours();
    }

    ~Impl() override
    {
        stopTimer();
        list.setModel(nullptr);
    }

    int getNumRows() override
    {
        return static_cast<int>(entries.size());
    }

    juce::File fileAt(int row) const
    {
        if (row >= 0 && static_cast<std::size_t>(row) < entries.size())
            return entries[static_cast<std::size_t>(row)].file;

        return {};
    }

    juce::String getNameForRow(int row) const
    {
        if (row >= 0 && static_cast<std::size_t>(row) < entries.size())
            return entries[static_cast<std::size_t>(row)].name;

        return {};
    }

    juce::String cellText(int row, int column) const
    {
        if (row < 0 || static_cast<std::size_t>(row) >= entries.size())
            return {};

        const auto& entry = entries[static_cast<std::size_t>(row)];

        if (column == 1)
            return entry.name;

        if (column == 3)
            return formatSampleSize(entry.size);

        if (!entry.metadataReady || entry.sampleRate <= 0.0)
            return "-";

        switch (column)
        {
            case 2: return formatSampleDuration(entry.duration);
            case 4: return juce::String(entry.channels);
            case 5: return formatSampleRate(entry.sampleRate);
            case 6: return formatSampleBitRate(entry.sampleRate, entry.channels, entry.bitDepth);
            default: return {};
        }
    }

    juce::String getCellTooltip(int row, int column) override
    {
        if (row < 0 || static_cast<std::size_t>(row) >= entries.size())
            return {};

        const auto& entry = entries[static_cast<std::size_t>(row)];
        auto text = column == 1 ? entry.file.getFullPathName() : cellText(row, column);

        if (entry.error.isNotEmpty())
            text += "\n" + entry.error;

        return text;
    }

    void paintRowBackground(juce::Graphics& graphics, int,
                            int, int, bool selected) override
    {
        if (selected)
        {
            graphics.fillAll(owner.findColour(juce::DirectoryContentsDisplayComponent::highlightColourId));
        }
    }

    void paintCell(juce::Graphics& graphics, int row, int column,
                   int width, int height, bool) override
    {
        graphics.setColour(owner.findColour(juce::ListBox::textColourId));
        graphics.setFont(juce::FontOptions(13.0f));
        graphics.drawText(cellText(row, column), 8, 0, juce::jmax(0, width - 16),
                          height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int row) override
    {
        const auto next = fileAt(row);

        if (selectedFile == next)
            return;

        selectedFile = next;
        const auto revision = ++selectionRevision;
        postSelectionChanged(revision, next);
    }

    void postSelectionChanged(std::uint64_t revision, const juce::File& next)
    {
        const juce::Component::SafePointer<SampleBrowser> safe(&owner);

        // ListBox still uses its model after this call, so client callbacks run afterwards.
        juce::MessageManager::callAsync([safe, revision, next]
        {
            if (safe == nullptr || safe->impl == nullptr
                || safe->impl->selectionRevision != revision)
                return;

            const auto callback = safe->onSelectionChanged;

            if (callback)
                callback(next);
        });
    }

    void cellDoubleClicked(int row, int, const juce::MouseEvent&) override
    {
        if (fileAt(row) == juce::File())
            return;

        list.selectRow(row);
        postCommand(Command::play);
    }

    void returnKeyPressed(int) override
    {
        postCommand(Command::play);
    }

    void backgroundClicked(const juce::MouseEvent&) override
    {
        list.deselectAllRows();
    }

    void postCommand(Command command)
    {
        if (command != Command::stop && selectedFile == juce::File())
            return;

        const auto revision = selectionRevision;
        const juce::Component::SafePointer<SampleBrowser> safe(&owner);

        juce::MessageManager::callAsync([safe, command, revision]
        {
            if (safe == nullptr || safe->impl == nullptr)
                return;

            if (command != Command::stop && safe->impl->selectionRevision != revision)
                return;

            std::function<void()> callback;

            switch (command)
            {
                case Command::play:   callback = safe->onPlayRequested; break;
                case Command::toggle: callback = safe->onTogglePlaybackRequested; break;
                case Command::stop:   callback = safe->onStopRequested; break;
            }

            if (callback)
                callback();
        });
    }

    void selectRow(int row)
    {
        const auto next = fileAt(row);
        juce::SparseSet<int> selection;

        if (next != juce::File())
            selection.addRange({ row, row + 1 });

        list.setSelectedRows(selection, juce::dontSendNotification);

        if (next != juce::File())
            list.scrollToEnsureRowIsOnscreen(row);

        if (selectedFile == next)
            return;

        selectedFile = next;
        ++selectionRevision;

        const auto callback = owner.onSelectionChanged;

        if (callback)
            callback(next);
    }

    void openFolder(const juce::File& folder)
    {
        ++selectionRevision;
        ++scanRevision;
        selectedFile = {};
        list.setSelectedRows({}, juce::dontSendNotification);
        entries.clear();
        list.updateContent();
        currentFolder = folder;
        loading = true;
        setStatus("Loading WAV files...");
        scanner.request(folder);
        startTimerHz(30);

        const auto callback = owner.onSelectionChanged;

        if (callback)
            callback(juce::File());
    }

    void setStatus(const juce::String& text, bool error = false)
    {
        hasError = error;
        statusLabel.setText(text, juce::dontSendNotification);
        statusLabel.setTooltip(text);
        statusLabel.setColour(juce::Label::textColourId, error ? juce::Colour(0xffd13438)
            : owner.findColour(juce::Label::textColourId).withAlpha(0.7f));
    }

    void applyColours()
    {
        list.setColour(juce::ListBox::backgroundColourId, owner.findColour(juce::ListBox::backgroundColourId));
        setStatus(statusLabel.getText(), hasError);
        list.repaint();
    }

    void timerCallback() override
    {
        auto publication = scanner.takeResult();

        if (publication.result.has_value())
        {
            loading = false;
            entries = std::move(publication.result->entries);
            list.updateContent();

            if (publication.result->error.isNotEmpty())
                setStatus(publication.result->error, true);
            else if (entries.empty())
                setStatus("No WAV files in this folder.");
            else
                setStatus(juce::String(getNumRows()) + (entries.size() == 1 ? " WAV file" : " WAV files"));
        }

        for (auto& update : publication.metadata)
        {
            if (update.row < entries.size())
                entries[update.row] = std::move(update.entry);
        }

        if (publication.result.has_value() || !publication.metadata.empty())
            list.repaint();

        if (publication.finished)
            stopTimer();
    }

    void resized()
    {
        auto bounds = owner.getLocalBounds().reduced(1);
        statusLabel.setBounds(bounds.removeFromBottom(22).reduced(5, 0));
        list.setBounds(bounds);
    }

    SampleBrowser& owner;
    std::vector<SampleEntry> entries;
    juce::File currentFolder;
    juce::File selectedFile;
    std::uint64_t selectionRevision = 0;
    std::uint64_t scanRevision = 0;
    bool loading = false;
    bool hasError = false;
    FolderScanner scanner;
    juce::Label statusLabel;
    SampleList list;
    juce::TooltipWindow tooltip;
};

SampleBrowser::SampleBrowser()
{
    assertMessageThread();
    setName("Sample folder browser");
    setOpaque(true);
    impl = std::make_unique<Impl>(*this);
}

SampleBrowser::~SampleBrowser()
{
    assertMessageThread();
    impl.reset();
}

void SampleBrowser::openFolder(const juce::File& folder)
{
    assertMessageThread();
    impl->openFolder(folder);
}

juce::File SampleBrowser::getCurrentFolder() const
{
    assertMessageThread();
    return impl->currentFolder;
}

juce::File SampleBrowser::getSelectedFile() const
{
    assertMessageThread();
    return impl->selectedFile;
}

int SampleBrowser::getNumFiles() const
{
    assertMessageThread();
    return impl->getNumRows();
}

bool SampleBrowser::isLoading() const
{
    assertMessageThread();
    return impl->loading;
}

juce::String SampleBrowser::getStatusText() const
{
    assertMessageThread();
    return impl->statusLabel.getText();
}

void SampleBrowser::selectRow(int row)
{
    assertMessageThread();
    impl->selectRow(row);
}

void SampleBrowser::focusList()
{
    assertMessageThread();
    impl->list.grabKeyboardFocus();
}

void SampleBrowser::paint(juce::Graphics& graphics)
{
    graphics.fillAll(findColour(juce::ListBox::backgroundColourId));
    graphics.setColour(findColour(juce::ResizableWindow::backgroundColourId));
    graphics.fillRect(getLocalBounds().reduced(1).removeFromTop(24));
    graphics.setColour(findColour(juce::ListBox::outlineColourId));
    graphics.drawRect(getLocalBounds());
}

void SampleBrowser::resized()
{
    if (impl != nullptr)
        impl->resized();
}

void SampleBrowser::lookAndFeelChanged()
{
    if (impl != nullptr)
        impl->applyColours();
}
}
