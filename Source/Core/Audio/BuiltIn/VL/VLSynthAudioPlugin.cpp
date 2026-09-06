/*
    This file is part of Helio music sequencer.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "VLSynthAudioPlugin.h"
#include "BuiltInSynthsPluginFormat.h"
#include "MobileComboBox.h"
#include "Workspace.h"
#include "HelioTheme.h"
#include "ColourIDs.h"

const String VLSynthAudioPlugin::instrumentId = "<vl-synth>";
const String VLSynthAudioPlugin::instrumentName = "Helio Wind";

//===----------------------------------------------------------------------===//
// A minimal UI: pick a preset and a breath mode
//===----------------------------------------------------------------------===//

class VLSynthEditor final : public AudioProcessorEditor
{
public:

    explicit VLSynthEditor(WeakReference<VLSynthAudioPlugin> vlPlugin) :
        AudioProcessorEditor(vlPlugin),
        audioPlugin(vlPlugin)
    {
        // non-editable text editors instead of labels just to have nice frames
        this->programNameLabel = HelioTheme::makeSingleLineTextEditor(false);
        this->programNameLabel->setMouseCursor(MouseCursor::PointingHandCursor);
        this->addAndMakeVisible(this->programNameLabel.get());

        this->programsComboBox = make<MobileComboBox::Container>();
        this->addAndMakeVisible(this->programsComboBox.get());

        this->breathModeLabel = HelioTheme::makeSingleLineTextEditor(false);
        this->breathModeLabel->setMouseCursor(MouseCursor::PointingHandCursor);
        this->addAndMakeVisible(this->breathModeLabel.get());

        this->breathModesComboBox = make<MobileComboBox::Container>();
        this->addAndMakeVisible(this->breathModesComboBox.get());

        this->syncDataWithAudioPlugin();
        this->setSize(640, 120);
    }

    void syncDataWithAudioPlugin()
    {
        const auto numPrograms = this->audioPlugin->getNumPrograms();
        this->programNameLabel->setText(this->audioPlugin->getCurrentProgramName(), dontSendNotification);

        MenuPanel::Menu programsMenu;
        for (int i = 0; i < numPrograms; ++i)
        {
            const auto programName = this->audioPlugin->getProgramName(i);
            programsMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectPreset + i, programName));
        }

        auto programsMenuCurrentItem = [this]()
        {
            return jlimit(0, jmax(0, this->audioPlugin->getNumPrograms() - 1),
                this->audioPlugin->getCurrentProgram());
        };

        this->programsComboBox->initWith(this->programNameLabel.get(),
            programsMenu, move(programsMenuCurrentItem), true);

        const auto breathMode = this->audioPlugin->getSynthParameters().preset.breathMode;
        this->breathModeLabel->setText(TRANS(I18n::Instruments::vlSynthBreathMode) +
            ": " + VL::getBreathModeName(breathMode), dontSendNotification);

        MenuPanel::Menu breathModesMenu;
        for (int i = 0; i < VL::numBreathModes; ++i)
        {
            breathModesMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectBreathMode + i,
                VL::getBreathModeName(VL::BreathMode(i))));
        }

        auto breathModesMenuCurrentItem = [this]()
        {
            return int(this->audioPlugin->getSynthParameters().preset.breathMode);
        };

        this->breathModesComboBox->initWith(this->breathModeLabel.get(),
            breathModesMenu, move(breathModesMenuCurrentItem), false);
    }

    void resized() override
    {
        const auto getRowArea = [this](float proportionOfHeight, int height, int padding = 30)
        {
            const auto area = this->getLocalBounds().reduced(padding);
            const auto y = area.proportionOfHeight(proportionOfHeight);
            return area.withHeight(height).translated(0, y - height / 2);
        };

        constexpr auto rowHeight = 32;
        constexpr auto paddingX = 6;

        const auto selectProgramArea = getRowArea(0.15f, rowHeight);
        this->programNameLabel->setBounds(selectProgramArea.reduced(paddingX, 0));
        this->programsComboBox->setBounds(this->getLocalBounds().reduced(2));

        const auto breathModeArea = getRowArea(0.7f, rowHeight);
        this->breathModeLabel->setBounds(breathModeArea.reduced(paddingX, 0));
        this->breathModesComboBox->setBounds(this->getLocalBounds().reduced(2));
    }

    void paint(Graphics &g) override
    {
        g.setFillType(findDefaultColour(ColourIDs::Panel::pageFillA));
        g.fillRect(this->getLocalBounds());
    }

    void handleCommandMessage(int commandId) override
    {
        const int presetIndex = commandId - CommandIDs::SelectPreset;
        if (presetIndex >= 0 && presetIndex < this->audioPlugin->getNumPrograms())
        {
            const auto newParams = this->audioPlugin->getSynthParameters().withProgram(presetIndex);
            this->audioPlugin->applySynthParameters(newParams);
            this->syncDataWithAudioPlugin();
            App::Workspace().autosave();
            return;
        }

        const int breathModeIndex = commandId - CommandIDs::SelectBreathMode;
        if (breathModeIndex >= 0 && breathModeIndex < VL::numBreathModes)
        {
            const auto newParams = this->audioPlugin->getSynthParameters()
                .withBreathMode(VL::BreathMode(breathModeIndex));
            this->audioPlugin->applySynthParameters(newParams);
            this->syncDataWithAudioPlugin();
            App::Workspace().autosave();
        }
    }

private:

    WeakReference<VLSynthAudioPlugin> audioPlugin;

    UniquePointer<TextEditor> programNameLabel;
    UniquePointer<MobileComboBox::Container> programsComboBox;

    UniquePointer<TextEditor> breathModeLabel;
    UniquePointer<MobileComboBox::Container> breathModesComboBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VLSynthEditor)
};

//===----------------------------------------------------------------------===//
// VLSynthAudioPlugin
//===----------------------------------------------------------------------===//

VLSynthAudioPlugin::VLSynthAudioPlugin()
{
    this->setPlayConfigDetails(0, 2, this->getSampleRate(), this->getBlockSize());
}

void VLSynthAudioPlugin::fillInPluginDescription(PluginDescription &description) const
{
    description.name = this->getName();
    description.descriptiveName = description.name;
    description.uniqueId = description.name.hashCode();
    description.category = "Synth";
    description.pluginFormatName = BuiltInSynthsPluginFormat::formatName;
    description.fileOrIdentifier = BuiltInSynthsPluginFormat::formatIdentifier;
    description.manufacturerName = "Built-in";
    description.version = "1.0";
    description.isInstrument = true;
    description.numInputChannels = this->getTotalNumInputChannels();
    description.numOutputChannels = this->getTotalNumOutputChannels();
}

const String VLSynthAudioPlugin::getName() const
{
    return VLSynthAudioPlugin::instrumentName;
}

void VLSynthAudioPlugin::processBlock(AudioSampleBuffer &buffer, MidiBuffer &midiMessages)
{
    ScopedNoDenormals noDenormals;
    buffer.clear(0, buffer.getNumSamples());
    this->synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

void VLSynthAudioPlugin::prepareToPlay(double sampleRate, int)
{
    this->synth.prepareToPlay(sampleRate);
}

void VLSynthAudioPlugin::reset()
{
    this->synth.reset();
}

void VLSynthAudioPlugin::releaseResources() {}
double VLSynthAudioPlugin::getTailLengthSeconds() const { return 1.0; }

bool VLSynthAudioPlugin::acceptsMidi() const { return true; }
bool VLSynthAudioPlugin::producesMidi() const { return false; }

bool VLSynthAudioPlugin::hasEditor() const { return true; }
AudioProcessorEditor *VLSynthAudioPlugin::createEditor()
{
    return new VLSynthEditor(this);
}

//===----------------------------------------------------------------------===//
// Presets
//===----------------------------------------------------------------------===//

int VLSynthAudioPlugin::getNumPrograms()
{
    return VL::getFactoryPresets().size();
}

int VLSynthAudioPlugin::getCurrentProgram()
{
    return jmax(0, this->synth.getParameters().programIndex);
}

void VLSynthAudioPlugin::setCurrentProgram(int index)
{
    this->applySynthParameters(this->synth.getParameters().withProgram(index));
}

const String VLSynthAudioPlugin::getProgramName(int index)
{
    const auto &presets = VL::getFactoryPresets();
    return presets[jlimit(0, presets.size() - 1, index)].name;
}

const String VLSynthAudioPlugin::getCurrentProgramName()
{
    // an edited or a user preset shows its own name
    const auto &parameters = this->synth.getParameters();
    return parameters.programIndex < 0 ?
        parameters.preset.name + " *" : this->getProgramName(parameters.programIndex);
}

void VLSynthAudioPlugin::changeProgramName(int, const String &) {}

//===----------------------------------------------------------------------===//
// Parameters
//===----------------------------------------------------------------------===//

void VLSynthAudioPlugin::getStateInformation(MemoryBlock &destData)
{
    const auto state = this->synth.getParameters().serialize();

    String stateAsString;
    if (this->serializer.saveToString(stateAsString, state).ok())
    {
        MemoryOutputStream outStream(destData, false);
        outStream.writeString(stateAsString);
    }
}

void VLSynthAudioPlugin::setStateInformation(const void *data, int sizeInBytes)
{
    MemoryInputStream inStream(data, sizeInBytes, false);
    const auto stateAsString = inStream.readString();
    const auto state = this->serializer.loadFromString(stateAsString);

    VLSynth::Parameters newParameters;
    newParameters.deserialize(state);

    this->applySynthParameters(newParameters);
}

void VLSynthAudioPlugin::applySynthParameters(const VLSynth::Parameters &newParameters)
{
    this->synth.applyParameters(newParameters);
}

const VLSynth::Parameters &VLSynthAudioPlugin::getSynthParameters() const noexcept
{
    return this->synth.getParameters();
}

bool VLSynthAudioPlugin::saveUserPreset(const File &file) const
{
    return this->serializer.saveToFile(file, this->synth.getPreset().serialize()).ok();
}

bool VLSynthAudioPlugin::loadUserPreset(const File &file)
{
    const auto data = this->serializer.loadFromFile(file);
    if (!data.isValid())
    {
        return false;
    }

    VL::Preset preset;
    preset.deserialize(data);
    if (preset.name.isEmpty())
    {
        preset.name = file.getFileNameWithoutExtension();
    }

    this->applySynthParameters(this->synth.getParameters().withPreset(preset));
    return true;
}
