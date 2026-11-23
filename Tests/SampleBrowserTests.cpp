#include "SampleBrowser.h"
#include "SampleDetails.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace wavecrate
{
namespace
{
class BrowserTestFolder final
{
public:
    BrowserTestFolder()
        : root(juce::File::getCurrentWorkingDirectory().getChildFile("out")
                   .getChildFile("tests").getChildFile("browser-" + juce::Uuid().toString()))
    {
        created = root.createDirectory().wasOk();
    }

    ~BrowserTestFolder()
    {
        if (created)
            root.deleteRecursively();
    }

    const juce::File root;
    bool created = false;
};

bool waitForBrowser(const std::function<bool()>& ready)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;

    while (!ready() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    return ready();
}

juce::ListBox* sampleList(SampleBrowser& browser)
{
    for (auto* child : browser.getChildren())
    {
        if (auto* list = dynamic_cast<juce::ListBox*>(child))
            return list;
    }

    return nullptr;
}

bool writeBrowserWav(const juce::File& file, int channels, double rate, int bits, int frames)
{
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(rate).withNumChannels(channels).withBitsPerSample(bits));

    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> samples(channels, frames);
    samples.clear();
    return writer->writeFromAudioSampleBuffer(samples, 0, frames);
}

class SampleBrowserTests final : public juce::UnitTest
{
public:
    SampleBrowserTests()
        : juce::UnitTest("Sample folder browser", "WaveCrate")
    {
    }

    void runTest() override
    {
        testCase("Virtualised resizable columns show validated mono/stereo metadata and malformed errors", [this]
        {
            BrowserTestFolder fixture;
            expect(fixture.created);

            if (!fixture.created)
                return;

            const auto mono = fixture.root.getChildFile("a-mono.wav");
            const auto stereo = fixture.root.getChildFile("b-stereo.wav");
            const auto malformed = fixture.root.getChildFile("c-malformed.wav");
            expect(writeBrowserWav(mono, 1, 48000.0, 16, 4800));
            expect(writeBrowserWav(stereo, 2, 44100.0, 24, 8820));
            expect(malformed.replaceWithText("not a WAV header"));

            for (int index = 0; index < 90; ++index)
                expect(fixture.root.getChildFile("z-" + juce::String(index) + ".wav")
                    .replaceWithText("not audio"));

            SampleBrowser browser;
            browser.setSize(640, 240);
            browser.openFolder(fixture.root);
            expect(waitForBrowser([&] { return !browser.isLoading(); }));
            auto* table = dynamic_cast<juce::TableListBox*>(sampleList(browser));
            expect(table != nullptr);

            if (table == nullptr)
                return;

            expectEquals(browser.getNumFiles(), 93);
            auto& header = table->getHeader();
            expectEquals(header.getNumColumns(true), 6);
            const juce::StringArray titles { "Name", "Duration", "Size", "Channels", "Sample rate", "Bitrate" };

            for (int column = 1; column <= titles.size(); ++column)
            {
                expectEquals(header.getColumnName(column), titles[column - 1]);
                expectEquals(header.getColumnIdOfIndex(column - 1, true), column);
            }

            header.setColumnWidth(1, 260);
            expectEquals(header.getColumnWidth(1), 260);
            expect(table->getComponentForRowNumber(92) == nullptr, "Offscreen rows must remain virtualised");
            // Header resize notifications update the content and scroll range asynchronously.
            expect(waitForBrowser([&]
            {
                return table->getViewport()->getViewedComponent()->getWidth() == header.getTotalWidth()
                    && table->getHorizontalScrollBar().getMaximumRangeLimit() == header.getTotalWidth()
                    && table->getHorizontalScrollBar().isVisible();
            }), "Resized columns must produce a ready horizontal scroll range");
            table->scrollToEnsureColumnIsOnscreen(6);
            expect(waitForBrowser([&] { return table->getViewport()->getViewPositionX() > 0; }),
                   "Narrow panes must scroll horizontally");

            auto* model = table->getTableListBoxModel();
            expect(model != nullptr);

            if (model == nullptr)
                return;

            expect(waitForBrowser([&]
            {
                return model->getCellTooltip(0, 5) == "48 kHz"
                    && model->getCellTooltip(1, 5) == "44.1 kHz"
                    && model->getCellTooltip(2, 2).startsWith("-\n");
            }), "Background header metadata timed out");
            expectEquals(model->getCellTooltip(0, 1), mono.getFullPathName());
            expectEquals(model->getCellTooltip(0, 2), juce::String("0:00.100"));
            expectEquals(model->getCellTooltip(0, 3), formatSampleSize(mono.getSize()));
            expectEquals(model->getCellTooltip(0, 4), juce::String("1"));
            expectEquals(model->getCellTooltip(0, 6), juce::String("768.0 kb/s"));
            expectEquals(model->getCellTooltip(1, 2), juce::String("0:00.200"));
            expectEquals(model->getCellTooltip(1, 3), formatSampleSize(stereo.getSize()));
            expectEquals(model->getCellTooltip(1, 4), juce::String("2"));
            expectEquals(model->getCellTooltip(1, 6), juce::String("2116.8 kb/s"));
            expect(model->getCellTooltip(2, 1).startsWith(malformed.getFullPathName() + "\n"));
            expect(model->getCellTooltip(2, 3).startsWith(formatSampleSize(malformed.getSize()) + "\n"));
            expect(model->getCellTooltip(2, 4).startsWith("-\n"));
            expectEquals(browser.getStatusText(), juce::String("93 WAV files"));

            int selections = 0;
            browser.onSelectionChanged = [&](const juce::File&) { ++selections; };
            browser.selectRow(0);
            expect(waitForBrowser([&] { return model->getCellTooltip(92, 2).startsWith("-\n"); }));
            expectEquals(selections, 1, "Metadata updates must not change selection");
            expect(browser.getSelectedFile() == mono);
        });

        testCase("Folder replacement and same-path refresh discard previous header metadata", [this]
        {
            BrowserTestFolder fixture;
            expect(fixture.created);

            if (!fixture.created)
                return;

            const auto first = fixture.root.getChildFile("first");
            const auto second = fixture.root.getChildFile("second");
            expect(first.createDirectory().wasOk());
            expect(second.createDirectory().wasOk());
            expect(writeBrowserWav(first.getChildFile("sample.wav"), 1, 48000.0, 16, 4800));
            const auto replacement = second.getChildFile("sample.wav");
            expect(writeBrowserWav(replacement, 2, 44100.0, 24, 8820));

            for (int index = 0; index < 90; ++index)
                expect(first.getChildFile("z-" + juce::String(index) + ".wav").replaceWithText("not audio"));

            SampleBrowser browser;
            browser.setSize(640, 240);
            browser.openFolder(first);
            expect(waitForBrowser([&] { return !browser.isLoading(); }));
            auto* table = dynamic_cast<juce::TableListBox*>(sampleList(browser));
            expect(table != nullptr);

            if (table == nullptr)
                return;

            auto* model = table->getTableListBoxModel();

            for (int iteration = 0; iteration < 8; ++iteration)
            {
                browser.openFolder(first);
                browser.openFolder(second);
            }

            expect(waitForBrowser([&]
            {
                return !browser.isLoading() && browser.getNumFiles() == 1
                    && model->getCellTooltip(0, 5) == "44.1 kHz";
            }));
            expectEquals(model->getCellTooltip(0, 4), juce::String("2"));
            expectEquals(model->getCellTooltip(0, 6), juce::String("2116.8 kb/s"));
            browser.selectRow(0);
            expect(browser.getSelectedFile() == replacement);

            expect(replacement.deleteFile());
            expect(writeBrowserWav(replacement, 1, 96000.0, 16, 24000));
            browser.openFolder(second);
            expectEquals(browser.getNumFiles(), 0);
            expect(browser.getSelectedFile() == juce::File());
            expectEquals(model->getCellTooltip(0, 5), juce::String());
            expect(waitForBrowser([&]
            {
                return !browser.isLoading() && model->getCellTooltip(0, 5) == "96 kHz";
            }));
            expectEquals(model->getCellTooltip(0, 2), juce::String("0:00.250"));
            expectEquals(model->getCellTooltip(0, 3), formatSampleSize(replacement.getSize()));
            expectEquals(model->getCellTooltip(0, 4), juce::String("1"));
            expectEquals(model->getCellTooltip(0, 6), juce::String("1536.0 kb/s"));
        });

        testCase("Only immediate WAV files are listed in case-insensitive filename order", [this]
        {
            BrowserTestFolder fixture;
            expect(fixture.created);

            if (!fixture.created)
                return;

            expect(fixture.root.getChildFile("kick2.wav").replaceWithText("not audio"));
            expect(fixture.root.getChildFile("kick10.wav").replaceWithText("not audio"));
            expect(fixture.root.getChildFile("beta.wav").replaceWithText("not audio"));
            expect(fixture.root.getChildFile("Alpha.WAV").replaceWithText("not audio"));
            expect(fixture.root.getChildFile("ignore.mp3").replaceWithText("not audio"));
            expect(fixture.root.getChildFile("ignore.wav.bak").replaceWithText("not audio"));
            const auto nested = fixture.root.getChildFile("not-a-file.wav");
            expect(nested.createDirectory().wasOk());
            expect(nested.getChildFile("nested.wav").replaceWithText("not audio"));

            SampleBrowser browser;
            browser.setSize(640, 240);
            browser.openFolder(fixture.root);
            expect(browser.isLoading());
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }), "Folder scan timed out");
            expectEquals(browser.getNumFiles(), 4);
            expect(browser.getSelectedFile() == juce::File());
            expectEquals(browser.getStatusText(), juce::String("4 WAV files"));

            const juce::StringArray expected { "Alpha.WAV", "beta.wav", "kick10.wav", "kick2.wav" };

            for (int row = 0; row < expected.size(); ++row)
            {
                browser.selectRow(row);
                expectEquals(browser.getSelectedFile().getFileName(), expected[row]);
            }
        });

        testCase("Replacing or refreshing a folder invalidates selection and discards stale scans", [this]
        {
            BrowserTestFolder fixture;
            expect(fixture.created);

            if (!fixture.created)
                return;

            const auto first = fixture.root.getChildFile("first");
            const auto second = fixture.root.getChildFile("second");
            expect(first.createDirectory().wasOk());
            expect(second.createDirectory().wasOk());
            expect(first.getChildFile("first.wav").replaceWithText("not audio"));
            expect(second.getChildFile("second.WAV").replaceWithText("not audio"));

            SampleBrowser browser;
            std::vector<juce::File> selections;
            browser.onSelectionChanged = [this, &selections](const juce::File& file)
            {
                expect(juce::MessageManager::getInstance()->isThisTheMessageThread());
                selections.push_back(file);
            };

            browser.openFolder(first);
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            browser.selectRow(0);
            expect(browser.getSelectedFile() == first.getChildFile("first.wav"));
            expect(!selections.empty() && selections.back() == browser.getSelectedFile());

            browser.openFolder(second);
            expectEquals(browser.getNumFiles(), 0);
            expect(browser.getSelectedFile() == juce::File());
            expect(!selections.empty() && selections.back() == juce::File());

            for (int iteration = 0; iteration < 8; ++iteration)
            {
                browser.openFolder(first);
                browser.openFolder(second);
            }

            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            expectEquals(browser.getNumFiles(), 1);
            browser.selectRow(0);
            expect(browser.getSelectedFile() == second.getChildFile("second.WAV"));
            browser.selectRow(-1);
            expect(browser.getSelectedFile() == juce::File());
            expect(!selections.empty() && selections.back() == juce::File());

            expect(second.getChildFile("added.wav").replaceWithText("not audio"));
            browser.openFolder(second);
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            expectEquals(browser.getNumFiles(), 2);
        });

        testCase("Empty, missing and non-directory paths have useful states and can recover", [this]
        {
            BrowserTestFolder fixture;
            expect(fixture.created);

            if (!fixture.created)
                return;

            SampleBrowser browser;
            browser.openFolder(fixture.root);
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            expectEquals(browser.getNumFiles(), 0);
            expectEquals(browser.getStatusText(), juce::String("No WAV files in this folder."));

            browser.openFolder(fixture.root.getChildFile("missing"));
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            expectEquals(browser.getNumFiles(), 0);
            expect(browser.getStatusText().startsWith("Cannot read folder:"));
            browser.selectRow(0);
            expect(browser.getSelectedFile() == juce::File());

            const auto malformedWav = fixture.root.getChildFile("malformed.WAV");
            expect(malformedWav.replaceWithText("This is deliberately not a WAV header."));
            browser.openFolder(malformedWav);
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            expectEquals(browser.getNumFiles(), 0);
            expect(browser.getStatusText().startsWith("Cannot read folder:"));

            browser.openFolder(juce::File());
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            expectEquals(browser.getNumFiles(), 0);
            expectEquals(browser.getStatusText(), juce::String("No folder was selected."));

            browser.openFolder(fixture.root);
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            expectEquals(browser.getNumFiles(), 1);
            browser.selectRow(0);
            expect(browser.getSelectedFile() == malformedWav, "Malformed files must remain selectable");
        });

        testCase("List navigation and preview keys route message-thread callbacks", [this]
        {
            BrowserTestFolder fixture;
            expect(fixture.created);

            if (!fixture.created)
                return;

            expect(fixture.root.getChildFile("a.wav").replaceWithText("not audio"));
            expect(fixture.root.getChildFile("b.WAV").replaceWithText("not audio"));
            SampleBrowser browser;
            browser.setSize(640, 240);
            browser.openFolder(fixture.root);
            expect(waitForBrowser([&browser] { return !browser.isLoading(); }));
            auto* list = sampleList(browser);
            expect(list != nullptr);

            if (list == nullptr)
                return;

            int selections = 0;
            int plays = 0;
            int toggles = 0;
            int stops = 0;
            const auto checkThread = [this]
            {
                expect(juce::MessageManager::getInstance()->isThisTheMessageThread());
            };
            browser.onSelectionChanged = [&](const juce::File&) { checkThread(); ++selections; };
            browser.onPlayRequested = [&] { checkThread(); ++plays; };
            browser.onTogglePlaybackRequested = [&] { checkThread(); ++toggles; };
            browser.onStopRequested = [&] { checkThread(); ++stops; };

            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
            expect(waitForBrowser([&] { return selections == 1; }));
            expectEquals(browser.getSelectedFile().getFileName(), juce::String("a.wav"));
            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
            expect(waitForBrowser([&] { return selections == 2; }));
            expectEquals(browser.getSelectedFile().getFileName(), juce::String("b.WAV"));
            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
            expect(waitForBrowser([&] { return selections == 3; }));
            expectEquals(browser.getSelectedFile().getFileName(), juce::String("a.wav"));

            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
            expect(waitForBrowser([&] { return plays == 1 && toggles == 1 && stops == 1; }));
            expectEquals(selections, 3);
            expectEquals(browser.getSelectedFile().getFileName(), juce::String("a.wav"));

            const auto eventTime = juce::Time::getCurrentTime();
            const juce::MouseEvent doubleClick(juce::Desktop::getInstance().getMainMouseSource(),
                                               {}, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               list, list, eventTime, {}, eventTime, 2, false);
            list->getListBoxModel()->listBoxItemDoubleClicked(1, doubleClick);
            expect(waitForBrowser([&] { return plays == 2 && selections == 4; }));
            expectEquals(browser.getSelectedFile().getFileName(), juce::String("b.WAV"));

            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
            browser.selectRow(0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
            expectEquals(toggles, 1, "A queued command must not play a replacement selection");

            browser.selectRow(-1);
            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
            expect(!list->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
            expect(list->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
            expect(waitForBrowser([&] { return stops == 2; }));
            expectEquals(plays, 2);
            expectEquals(toggles, 1);
        });

        testCase("Pending list callbacks and scans do not outlive the browser", [this]
        {
            BrowserTestFolder fixture;
            expect(fixture.created);

            if (!fixture.created)
                return;

            expect(fixture.root.getChildFile("a.wav").replaceWithText("not audio"));
            int callbacks = 0;
            auto browser = std::make_unique<SampleBrowser>();
            browser->setSize(640, 240);
            browser->openFolder(fixture.root);
            expect(waitForBrowser([&] { return !browser->isLoading(); }));
            browser->onSelectionChanged = [&](const juce::File&) { ++callbacks; };
            browser->onTogglePlaybackRequested = [&] { ++callbacks; };
            auto* list = sampleList(*browser);
            expect(list != nullptr);

            if (list != nullptr)
            {
                list->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
                list->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
            }

            browser.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
            expectEquals(callbacks, 0);

            browser = std::make_unique<SampleBrowser>();
            browser->setSize(640, 240);
            browser->openFolder(fixture.root);
            expect(waitForBrowser([&] { return !browser->isLoading(); }));
            browser->onSelectionChanged = [&](const juce::File&) { browser.reset(); };
            list = sampleList(*browser);
            expect(list != nullptr);

            if (list != nullptr)
            {
                list->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
                expect(waitForBrowser([&] { return browser == nullptr; }));
            }

            browser = std::make_unique<SampleBrowser>();
            browser->openFolder(fixture.root);
            browser.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        });
    }
};

SampleBrowserTests sampleBrowserTests;
}
}
