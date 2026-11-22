// Renders the plugin editor to a PNG without a host, for the README.
//   cmake -B build -DVO_DUCKER_BUILD_SCREENSHOT_TOOL=ON && cmake --build build --target VODuckerScreenshot
//   ./build/VODuckerScreenshot_artefacts/Release/VODuckerScreenshot docs/screenshot.png
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    const juce::File out = juce::File::getCurrentWorkingDirectory()
                               .getChildFile (argc > 1 ? argv[1] : "docs/screenshot.png");

    VODuckerProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::fprintf (stderr, "Couldn't write %s\n", out.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("Wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}
