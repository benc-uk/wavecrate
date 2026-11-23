#include "DesktopLayout.h"
#include "SampleBrowser.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace wavecrate
{
namespace
{
class NavigationTestFolder final
{
public:
    NavigationTestFolder()
        : root(juce::File::getCurrentWorkingDirectory().getChildFile("out")
                   .getChildFile("tests").getChildFile("navigation-" + juce::Uuid().toString())),
          opened(root.getChildFile("opened")),
          child(opened.getChildFile("child")),
          grandchild(child.getChildFile("grandchild"))
    {
        created = grandchild.createDirectory().wasOk()
            && opened.getChildFile("root.wav").replaceWithText("not audio")
            && opened.getChildFile("ignore.txt").replaceWithText("not audio")
            && child.getChildFile("child.WAV").replaceWithText("not audio")
            && grandchild.getChildFile("nested.wav").replaceWithText("not audio");
    }

    ~NavigationTestFolder()
    {
        root.deleteRecursively();
    }

    const juce::File root, opened, child, grandchild;
    bool created = false;
};

bool waitForNavigation(const std::function<bool()>& ready)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;

    while (!ready() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    return ready();
}

juce::FileBrowserComponent* folderBrowser(juce::Component& component)
{
    if (auto* browser = dynamic_cast<juce::FileBrowserComponent*>(&component))
        return browser;

    for (auto* child : component.getChildren())
    {
        if (auto* browser = folderBrowser(*child))
            return browser;
    }

    return nullptr;
}

juce::TreeViewItem* childItem(juce::TreeViewItem* parent, const juce::File& folder)
{
    if (parent != nullptr)
    {
        for (int index = 0; index < parent->getNumSubItems(); ++index)
        {
            auto* item = parent->getSubItem(index);
            if (item->getUniqueName() == folder.getFullPathName())
                return item;
        }
    }

    return nullptr;
}

bool waitForRoot(juce::FileTreeComponent& tree, SampleBrowser& samples,
                 const juce::File& folder, int numChildren)
{
    return waitForNavigation([&]
    {
        auto* root = tree.getRootItem();
        return root != nullptr && root->getUniqueName() == folder.getFullPathName()
            && root->isOpen() && root->isSelected() && root->getNumSubItems() == numChildren
            && !tree.directoryContentsList.isStillLoading() && !samples.isLoading();
    });
}

class FolderNavigationTests final : public juce::UnitTest
{
public:
    FolderNavigationTests()
        : juce::UnitTest("Folder navigation", "WaveCrate")
    {
    }

    void runTest() override
    {
        testCase("Opening exposes folders, not files, and deeper expansion is lazy", [this]
        {
            NavigationTestFolder fixture;
            expect(fixture.created);
            if (!fixture.created)
                return;

            const auto empty = fixture.opened.getChildFile("empty");
            expect(empty.createDirectory().wasOk());
            DesktopLayout layout;
            auto* browser = folderBrowser(layout);
            auto* tree = browser == nullptr ? nullptr
                : dynamic_cast<juce::FileTreeComponent*>(browser->getDisplayComponent());
            expect(tree != nullptr);
            if (tree == nullptr)
                return;

            browser->setRoot(fixture.opened);
            // Do not open the root here: Open must expose its children itself.
            const auto ready = waitForRoot(*tree, layout.getSamples(), fixture.opened, 2);
            expect(ready, "The opened root did not expose its immediate folders");
            if (!ready)
                return;

            auto* root = tree->getRootItem();
            auto* child = childItem(root, fixture.child);
            expect(child != nullptr);
            expect(childItem(root, empty) != nullptr);
            expect(childItem(root, fixture.opened.getChildFile("root.wav")) == nullptr);
            expect(childItem(root, fixture.opened.getChildFile("ignore.txt")) == nullptr);
            if (child == nullptr)
                return;

            expect(!child->isOpen());
            expectEquals(child->getNumSubItems(), 0, "Closed descendants must not be preloaded");
            child->setOpen(true);
            expect(waitForNavigation([&]
            {
                return child->getNumSubItems() == 1
                    && childItem(child, fixture.grandchild) != nullptr;
            }), "Expanding a child did not reveal its grandchild folder");
            expect(childItem(child, fixture.child.getChildFile("child.WAV")) == nullptr);
            child->setOpen(false);
            expect(!child->isOpen());
            expect(waitForNavigation([&]
            {
                return !tree->directoryContentsList.isStillLoading()
                    && !layout.getSamples().isLoading();
            }));
        });

        testCase("Tree selection changes samples, clears selection and returns to the parent", [this]
        {
            NavigationTestFolder fixture;
            expect(fixture.created);
            if (!fixture.created)
                return;

            std::vector<juce::File> selections;
            DesktopLayout layout;
            auto& samples = layout.getSamples();
            samples.onSelectionChanged = [&](const juce::File& file) { selections.push_back(file); };
            auto* browser = folderBrowser(layout);
            auto* tree = browser == nullptr ? nullptr
                : dynamic_cast<juce::FileTreeComponent*>(browser->getDisplayComponent());
            expect(tree != nullptr);
            if (tree == nullptr)
                return;

            browser->setRoot(fixture.opened);
            const auto ready = waitForRoot(*tree, samples, fixture.opened, 1);
            expect(ready, "Initial folder scan timed out");
            if (!ready)
                return;

            expectEquals(samples.getNumFiles(), 1);
            samples.selectRow(0);
            expect(samples.getSelectedFile() == fixture.opened.getChildFile("root.wav"));
            selections.clear();

            auto* child = childItem(tree->getRootItem(), fixture.child);
            expect(child != nullptr);
            if (child == nullptr)
                return;

            child->setSelected(true, true);
            expect(waitForNavigation([&]
            {
                return !selections.empty() && !samples.isLoading();
            }), "Child selection or scan timed out");
            expect(tree->getSelectedFile() == fixture.child);
            expect(samples.getSelectedFile() == juce::File());
            expect(!selections.empty() && selections.back() == juce::File(),
                   "Changing folders must notify the preview that the old selection is cleared");
            expectEquals(samples.getNumFiles(), 1, "Only WAVs directly inside the child belong here");
            samples.selectRow(0);
            expect(samples.getSelectedFile() == fixture.child.getChildFile("child.WAV"));
            selections.clear();

            tree->getRootItem()->setSelected(true, true);
            expect(waitForNavigation([&]
            {
                return !selections.empty() && !samples.isLoading();
            }), "Parent selection or scan timed out");
            expect(waitForRoot(*tree, samples, fixture.opened, 1), "Parent folder was not selected");
            expect(samples.getSelectedFile() == juce::File());
            expect(!selections.empty() && selections.back() == juce::File());
            expectEquals(samples.getNumFiles(), 1);
            samples.selectRow(0);
            expect(samples.getSelectedFile() == fixture.opened.getChildFile("root.wav"));
        });

        testCase("Replacing the opened root removes stale folders and samples, including an empty root", [this]
        {
            NavigationTestFolder fixture;
            expect(fixture.created);
            if (!fixture.created)
                return;

            const auto replacement = fixture.root.getChildFile("replacement");
            const auto newChild = replacement.getChildFile("new-child");
            const auto empty = fixture.root.getChildFile("empty");
            expect(newChild.createDirectory().wasOk());
            expect(replacement.getChildFile("new.wav").replaceWithText("not audio"));
            expect(empty.createDirectory().wasOk());
            DesktopLayout layout;
            auto& samples = layout.getSamples();
            auto* browser = folderBrowser(layout);
            auto* tree = browser == nullptr ? nullptr
                : dynamic_cast<juce::FileTreeComponent*>(browser->getDisplayComponent());
            expect(tree != nullptr);
            if (tree == nullptr)
                return;

            browser->setRoot(fixture.opened);
            const auto initialReady = waitForRoot(*tree, samples, fixture.opened, 1);
            expect(initialReady, "Initial folder scan timed out");
            if (!initialReady)
                return;
            samples.selectRow(0);
            expect(samples.getSelectedFile() == fixture.opened.getChildFile("root.wav"));
            auto* oldChild = childItem(tree->getRootItem(), fixture.child);
            expect(oldChild != nullptr);
            if (oldChild == nullptr)
                return;
            oldChild->setSelected(true, true);
            expect(waitForNavigation([&]
            {
                return samples.getSelectedFile() == juce::File() && !samples.isLoading();
            }), "Child selection or scan timed out");
            samples.selectRow(0);
            expect(samples.getSelectedFile() == fixture.child.getChildFile("child.WAV"));

            browser->setRoot(replacement);
            const auto replacementReady = waitForRoot(*tree, samples, replacement, 1);
            expect(replacementReady, "Replacement folder scan timed out");
            if (!replacementReady)
                return;
            expect(childItem(tree->getRootItem(), fixture.child) == nullptr);
            expect(childItem(tree->getRootItem(), newChild) != nullptr);
            expect(samples.getSelectedFile() == juce::File());
            expectEquals(samples.getNumFiles(), 1);
            samples.selectRow(0);
            expect(samples.getSelectedFile() == replacement.getChildFile("new.wav"));

            browser->setRoot(empty);
            expect(waitForRoot(*tree, samples, empty, 0), "Empty root retained stale folders");
            expectEquals(samples.getNumFiles(), 0);
            expect(samples.getSelectedFile() == juce::File());
            expectEquals(samples.getStatusText(), juce::String("No WAV files in this folder."));
        });
    }
};

FolderNavigationTests folderNavigationTests;
}
}
