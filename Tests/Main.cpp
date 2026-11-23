#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

namespace wavecrate::tests
{
int runAudioDeviceSmoke();
}

namespace
{
class ConsoleTestRunner final : public juce::UnitTestRunner
{
    void logMessage(const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};
}

int main(int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc == 2 && juce::String(argv[1]) == "--audio-device")
        return wavecrate::tests::runAudioDeviceSmoke();
    const bool namedTest = argc == 3 && juce::String(argv[1]) == "--test";
    if (argc != 1 && !namedTest)
    {
        std::cerr << "Usage: WaveCrateTests [--audio-device | --test \"test name\"]" << std::endl;
        return 1;
    }

    ConsoleTestRunner runner;
    runner.setAssertOnFailure(false);
    if (namedTest)
        runner.runTestsWithName(argv[2], 0x57415645);
    else
        runner.runTestsInCategory("WaveCrate", 0x57415645);

    if (runner.getNumResults() == 0)
    {
        std::cerr << "No WaveCrate tests were registered." << std::endl;
        return 1;
    }

    int failures = 0;
    int passes = 0;
    for (int index = 0; index < runner.getNumResults(); ++index)
    {
        const auto* result = runner.getResult(index);
        failures += result->failures;
        passes += result->passes;
    }

    std::cout << passes << " checks passed; " << failures << " failed." << std::endl;
    return failures == 0 ? 0 : 1;
}
