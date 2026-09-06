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
#include "IconButton.h"
#include "DocumentHelpers.h"
#include "SerializationKeys.h"
#include "Config.h"
#include "Workspace.h"
#include "HelioTheme.h"
#include "ColourIDs.h"

const String VLSynthAudioPlugin::instrumentId = "<vl-synth>";
const String VLSynthAudioPlugin::instrumentName = "Helio Wind";

//===----------------------------------------------------------------------===//
// The editor: preset and breath mode, the instrument's driver, resonator
// and four macro knobs, one controller's wiring at a time, and one
// modifier or effect at a time; the rows wrap on narrow screens
//===----------------------------------------------------------------------===//

class VLSynthEditor final : public AudioProcessorEditor
{
public:

    explicit VLSynthEditor(WeakReference<VLSynthAudioPlugin> vlPlugin) :
        AudioProcessorEditor(vlPlugin),
        audioPlugin(vlPlugin)
    {
        constexpr int iconSize = 20;

        const auto makeComboLabel = [this](UniquePointer<TextEditor> &label, UniquePointer<MobileComboBox::Container> &container)
        {
            label = HelioTheme::makeSingleLineTextEditor(false);
            label->setMouseCursor(MouseCursor::PointingHandCursor);
            this->addAndMakeVisible(label.get());
            container = make<MobileComboBox::Container>();
            this->addAndMakeVisible(container.get());
        };

        const auto makeKnob = [this](UniquePointer<Slider> &slider, UniquePointer<Label> &label,
            const String &text, double minimum, double maximum)
        {
            slider = make<Slider>(Slider::RotaryHorizontalVerticalDrag, Slider::NoTextBox);
            slider->setRange(minimum, maximum, 0.0);
            slider->onDragEnd = [this]() { App::Workspace().autosave(); };
            this->addAndMakeVisible(slider.get());

            label = make<Label>(String(), text);
            label->setJustificationType(Justification::centred);
            label->setInterceptsMouseClicks(false, false);
            this->addAndMakeVisible(label.get());
        };

        // preset row
        makeComboLabel(this->programNameLabel, this->programsComboBox);

        this->savePresetButton = make<IconButton>(Icons::commit, CommandIDs::VLSavePreset, this, iconSize);
        this->savePresetButton->setMouseCursor(MouseCursor::PointingHandCursor);
        this->addAndMakeVisible(this->savePresetButton.get());
        this->loadPresetButton = make<IconButton>(Icons::browse, CommandIDs::VLLoadPreset, this, iconSize);
        this->loadPresetButton->setMouseCursor(MouseCursor::PointingHandCursor);
        this->addAndMakeVisible(this->loadPresetButton.get());

        // instrument row and macro knobs
        makeComboLabel(this->breathModeLabel, this->breathModesComboBox);
        makeComboLabel(this->driverLabel, this->driversComboBox);
        makeComboLabel(this->resonatorLabel, this->resonatorsComboBox);

        makeKnob(this->shapeKnob, this->shapeLabel, "Shape", 0.0, 1.0);
        makeKnob(this->brightnessKnob, this->brightnessLabel, "Brightness", 0.0, 1.0);
        makeKnob(this->dampingKnob, this->dampingLabel, "Damping", 0.0, 1.0);
        makeKnob(this->noiseKnob, this->noiseLabel, "Noise", 0.0, 1.0);
        this->shapeKnob->onValueChange = [this]() { this->applyMacro(); };
        this->brightnessKnob->onValueChange = [this]() { this->applyMacro(); };
        this->dampingKnob->onValueChange = [this]() { this->applyMacro(); };
        this->noiseKnob->onValueChange = [this]() { this->applyMacro(); };

        // controller row
        makeComboLabel(this->controllerLabel, this->controllersComboBox);
        makeComboLabel(this->sourceLabel, this->sourcesComboBox);
        makeKnob(this->depthKnob, this->depthLabel, "Depth", -1.0, 1.0);
        makeKnob(this->baseKnob, this->baseLabel, "Base", 0.0, 1.0);
        this->depthKnob->onValueChange = [this]() { this->applyControllerKnobs(); };
        this->baseKnob->onValueChange = [this]() { this->applyControllerKnobs(); };

        // modifier row
        makeComboLabel(this->modifierLabel, this->modifiersComboBox);
        this->modifierToggle = make<ToggleButton>("On");
        this->modifierToggle->onClick = [this]() { this->applyModifierToggle(); };
        this->addAndMakeVisible(this->modifierToggle.get());
        makeKnob(this->amountKnob, this->amountLabel, "Amount", 0.0, 1.0);
        this->amountKnob->onValueChange = [this]() { this->applyModifierAmount(); };

        this->syncDataWithAudioPlugin();
        this->setSize(640, 420);
    }

    //===------------------------------------------------------------------===//
    // Sync
    //===------------------------------------------------------------------===//

    void syncDataWithAudioPlugin()
    {
        const ScopedValueSetter<bool> syncing(this->isSyncing, true);
        const auto &preset = this->audioPlugin->getSynthParameters().preset;

        // presets
        this->programNameLabel->setText(this->audioPlugin->getCurrentProgramName(), dontSendNotification);
        MenuPanel::Menu programsMenu;
        for (int i = 0; i < this->audioPlugin->getNumPrograms(); ++i)
        {
            programsMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectPreset + i, this->audioPlugin->getProgramName(i)));
        }

        this->programsComboBox->initWith(this->programNameLabel.get(), programsMenu,
            [this]() { return jlimit(0, jmax(0, this->audioPlugin->getNumPrograms() - 1), this->audioPlugin->getCurrentProgram()); }, true);

        // breath mode, driver, resonator
        this->breathModeLabel->setText(TRANS(I18n::Instruments::vlSynthBreathMode) + ": " +
            VL::getBreathModeName(preset.breathMode), dontSendNotification);
        MenuPanel::Menu breathModesMenu;
        for (int i = 0; i < VL::numBreathModes; ++i)
        {
            breathModesMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectBreathMode + i, VL::getBreathModeName(VL::BreathMode(i))));
        }
        this->breathModesComboBox->initWith(this->breathModeLabel.get(), breathModesMenu,
            [this]() { return int(this->audioPlugin->getSynthParameters().preset.breathMode); });

        this->driverLabel->setText(VL::getDriverTypeName(preset.driver), dontSendNotification);
        MenuPanel::Menu driversMenu;
        for (int i = 0; i < VL::numDriverTypes; ++i)
        {
            driversMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectVLDriver + i, VL::getDriverTypeName(VL::DriverType(i))));
        }
        this->driversComboBox->initWith(this->driverLabel.get(), driversMenu,
            [this]() { return int(this->audioPlugin->getSynthParameters().preset.driver); });

        this->resonatorLabel->setText(VL::getResonatorTypeName(preset.resonator), dontSendNotification);
        MenuPanel::Menu resonatorsMenu;
        for (int i = 0; i < VL::numResonatorTypes; ++i)
        {
            resonatorsMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectVLResonator + i, VL::getResonatorTypeName(VL::ResonatorType(i))));
        }
        this->resonatorsComboBox->initWith(this->resonatorLabel.get(), resonatorsMenu,
            [this]() { return int(this->audioPlugin->getSynthParameters().preset.resonator); });

        // macros
        this->shapeKnob->setValue(VLSynthEditor::getShape(preset), dontSendNotification);
        this->brightnessKnob->setValue(1.0 - double(preset.absorption), dontSendNotification);
        this->dampingKnob->setValue(jlimit(0.0, 1.0, (0.995 - double(preset.lossGain)) / 0.095), dontSendNotification);
        this->noiseKnob->setValue(double(preset.getController(VL::ControllerId::BreathNoise).base), dontSendNotification);

        // controllers
        const auto controllerId = VL::ControllerId(this->selectedController);
        const auto &setting = preset.getController(controllerId);
        this->controllerLabel->setText(VL::getControllerName(controllerId), dontSendNotification);
        MenuPanel::Menu controllersMenu;
        for (int i = 0; i < VL::numControllers; ++i)
        {
            controllersMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectVLController + i, VL::getControllerName(VL::ControllerId(i))));
        }
        this->controllersComboBox->initWith(this->controllerLabel.get(), controllersMenu,
            [this]() { return this->selectedController; });

        this->sourceLabel->setText(VL::getSourceName(setting.source), dontSendNotification);
        MenuPanel::Menu sourcesMenu;
        for (int i = 0; i < VLSynthEditor::numSourceItems; ++i)
        {
            sourcesMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectVLControllerSource + i,
                VL::getSourceName(VLSynthEditor::sourceForItem(i))));
        }
        this->sourcesComboBox->initWith(this->sourceLabel.get(), sourcesMenu,
            [this]()
            {
                const auto &current = this->audioPlugin->getSynthParameters().preset.getController(VL::ControllerId(this->selectedController));
                return VLSynthEditor::itemForSource(current.source);
            });

        this->depthKnob->setValue(double(setting.depth), dontSendNotification);
        this->baseKnob->setValue(double(setting.base), dontSendNotification);

        // modifiers
        this->modifierLabel->setText(VLSynthEditor::getModifierName(this->selectedModifier), dontSendNotification);
        MenuPanel::Menu modifiersMenu;
        for (int i = 0; i < VLSynthEditor::numModifierItems; ++i)
        {
            modifiersMenu.add(MenuItem::item(Icons::empty, CommandIDs::SelectVLModifier + i, VLSynthEditor::getModifierName(i)));
        }
        this->modifiersComboBox->initWith(this->modifierLabel.get(), modifiersMenu,
            [this]() { return this->selectedModifier; });

        this->modifierToggle->setToggleState(VLSynthEditor::isModifierEnabled(preset, this->selectedModifier), dontSendNotification);
        this->amountKnob->setValue(VLSynthEditor::getModifierAmount(preset, this->selectedModifier), dontSendNotification);
    }

    //===------------------------------------------------------------------===//
    // Layout
    //===------------------------------------------------------------------===//

    void resized() override
    {
        constexpr auto rowHeight = 32;
        constexpr auto knobSize = 56;
        constexpr auto labelHeight = 18;
        constexpr auto padding = 12;
        constexpr auto gap = 6;
        const bool narrow = this->getWidth() < 520;

        auto area = this->getLocalBounds().reduced(padding);

        auto presetRow = area.removeFromTop(rowHeight);
        this->loadPresetButton->setBounds(presetRow.removeFromRight(rowHeight));
        this->savePresetButton->setBounds(presetRow.removeFromRight(rowHeight));
        this->programNameLabel->setBounds(presetRow.reduced(gap, 0));
        area.removeFromTop(gap);

        const auto layoutCombos = [&](std::initializer_list<Component *> editors)
        {
            if (narrow)
            {
                for (auto *editor : editors)
                {
                    editor->setBounds(area.removeFromTop(rowHeight).reduced(gap, 0));
                    area.removeFromTop(gap);
                }
            }
            else
            {
                auto row = area.removeFromTop(rowHeight);
                const auto width = row.getWidth() / int(editors.size());
                for (auto *editor : editors)
                {
                    editor->setBounds(row.removeFromLeft(width).reduced(gap, 0));
                }
                area.removeFromTop(gap);
            }
        };

        const auto layoutKnobs = [&](std::initializer_list<std::pair<Slider *, Label *>> knobs, Component *extra)
        {
            auto row = area.removeFromTop(knobSize + labelHeight);
            if (extra != nullptr)
            {
                auto extraArea = row.removeFromLeft(narrow ? row.getWidth() / 3 : 120);
                extra->setBounds(extraArea.withSizeKeepingCentre(extraArea.getWidth() - gap * 2, rowHeight));
            }

            const auto width = row.getWidth() / int(knobs.size());
            for (const auto &knob : knobs)
            {
                auto cell = row.removeFromLeft(width);
                knob.second->setBounds(cell.removeFromBottom(labelHeight));
                knob.first->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize));
            }

            area.removeFromTop(gap);
        };

        layoutCombos({ this->breathModeLabel.get(), this->driverLabel.get(), this->resonatorLabel.get() });
        layoutKnobs({ { this->shapeKnob.get(), this->shapeLabel.get() },
            { this->brightnessKnob.get(), this->brightnessLabel.get() },
            { this->dampingKnob.get(), this->dampingLabel.get() },
            { this->noiseKnob.get(), this->noiseLabel.get() } }, nullptr);

        layoutCombos({ this->controllerLabel.get(), this->sourceLabel.get() });
        layoutKnobs({ { this->depthKnob.get(), this->depthLabel.get() },
            { this->baseKnob.get(), this->baseLabel.get() } }, nullptr);

        layoutCombos({ this->modifierLabel.get() });
        layoutKnobs({ { this->amountKnob.get(), this->amountLabel.get() } }, this->modifierToggle.get());

        // the combo containers cover the whole editor, the menus open over it
        for (auto *container : { this->programsComboBox.get(), this->breathModesComboBox.get(),
            this->driversComboBox.get(), this->resonatorsComboBox.get(), this->controllersComboBox.get(),
            this->sourcesComboBox.get(), this->modifiersComboBox.get() })
        {
            container->setBounds(this->getLocalBounds().reduced(2));
        }

        // on a phone the page sets the width and keeps our height: grow to fit
        const auto neededHeight = area.getY() + padding;
        if (this->getHeight() < neededHeight)
        {
            this->setSize(this->getWidth(), neededHeight);
        }
    }

    void paint(Graphics &g) override
    {
        g.setFillType(findDefaultColour(ColourIDs::Panel::pageFillA));
        g.fillRect(this->getLocalBounds());
    }

    //===------------------------------------------------------------------===//
    // Commands
    //===------------------------------------------------------------------===//

    void handleCommandMessage(int commandId) override
    {
        const auto inRange = [commandId](int first, int count)
        {
            return commandId >= first && commandId < first + count;
        };

        if (inRange(CommandIDs::SelectPreset, this->audioPlugin->getNumPrograms()))
        {
            this->apply(this->audioPlugin->getSynthParameters().withProgram(commandId - CommandIDs::SelectPreset));
        }
        else if (inRange(CommandIDs::SelectBreathMode, VL::numBreathModes))
        {
            this->apply(this->audioPlugin->getSynthParameters().withBreathMode(VL::BreathMode(commandId - CommandIDs::SelectBreathMode)));
        }
        else if (inRange(CommandIDs::SelectVLDriver, VL::numDriverTypes))
        {
            auto preset = this->audioPlugin->getSynthParameters().preset;
            preset.driver = VL::DriverType(commandId - CommandIDs::SelectVLDriver);
            this->applyPreset(preset);
        }
        else if (inRange(CommandIDs::SelectVLResonator, VL::numResonatorTypes))
        {
            auto preset = this->audioPlugin->getSynthParameters().preset;
            preset.resonator = VL::ResonatorType(commandId - CommandIDs::SelectVLResonator);
            this->applyPreset(preset);
        }
        else if (inRange(CommandIDs::SelectVLController, VL::numControllers))
        {
            this->selectedController = commandId - CommandIDs::SelectVLController;
            this->syncDataWithAudioPlugin();
        }
        else if (inRange(CommandIDs::SelectVLControllerSource, VLSynthEditor::numSourceItems))
        {
            auto preset = this->audioPlugin->getSynthParameters().preset;
            preset.controllers[this->selectedController].source =
                VLSynthEditor::sourceForItem(commandId - CommandIDs::SelectVLControllerSource);
            this->applyPreset(preset);
        }
        else if (inRange(CommandIDs::SelectVLModifier, VLSynthEditor::numModifierItems))
        {
            this->selectedModifier = commandId - CommandIDs::SelectVLModifier;
            this->syncDataWithAudioPlugin();
        }
        else if (commandId == CommandIDs::VLSavePreset)
        {
            this->savePreset();
        }
        else if (commandId == CommandIDs::VLLoadPreset)
        {
            this->loadPreset();
        }
    }

private:

    //===------------------------------------------------------------------===//
    // Applying edits
    //===------------------------------------------------------------------===//

    void apply(const VLSynth::Parameters &parameters)
    {
        this->audioPlugin->applySynthParameters(parameters);
        this->syncDataWithAudioPlugin();
        App::Workspace().autosave();
    }

    void applyPreset(const VL::Preset &preset)
    {
        this->apply(this->audioPlugin->getSynthParameters().withPreset(preset));
    }

    // knobs apply on every change without re-syncing (that would recreate
    // the combos mid-drag), and autosave when the drag ends
    void applyPresetLive(const VL::Preset &preset)
    {
        if (!this->isSyncing)
        {
            this->audioPlugin->applySynthParameters(this->audioPlugin->getSynthParameters().withPreset(preset));
            this->programNameLabel->setText(this->audioPlugin->getCurrentProgramName(), dontSendNotification);
        }
    }

    void applyMacro()
    {
        auto preset = this->audioPlugin->getSynthParameters().preset;
        VLSynthEditor::setShape(preset, float(this->shapeKnob->getValue()));
        preset.absorption = 1.f - float(this->brightnessKnob->getValue());
        preset.lossGain = 0.995f - 0.095f * float(this->dampingKnob->getValue());
        preset.controllers[int(VL::ControllerId::BreathNoise)].base = float(this->noiseKnob->getValue());
        this->applyPresetLive(preset);
    }

    void applyControllerKnobs()
    {
        auto preset = this->audioPlugin->getSynthParameters().preset;
        preset.controllers[this->selectedController].depth = float(this->depthKnob->getValue());
        preset.controllers[this->selectedController].base = float(this->baseKnob->getValue());
        this->applyPresetLive(preset);
    }

    void applyModifierToggle()
    {
        if (this->isSyncing)
        {
            return;
        }

        auto preset = this->audioPlugin->getSynthParameters().preset;
        VLSynthEditor::setModifierEnabled(preset, this->selectedModifier, this->modifierToggle->getToggleState());
        this->applyPreset(preset);
    }

    void applyModifierAmount()
    {
        auto preset = this->audioPlugin->getSynthParameters().preset;
        VLSynthEditor::setModifierAmount(preset, this->selectedModifier, float(this->amountKnob->getValue()));
        this->applyPresetLive(preset);
    }

    //===------------------------------------------------------------------===//
    // Macro mappings
    //===------------------------------------------------------------------===//

    static double getShape(const VL::Preset &preset)
    {
        switch (preset.driver)
        {
        case VL::DriverType::Jet: return jlimit(0.0, 1.0, (double(preset.jetRatio) - 0.2) / 0.3);
        case VL::DriverType::Bow: return jlimit(0.0, 1.0, double(preset.bowForce));
        default: return jlimit(0.0, 1.0, (double(preset.reedOffset) - 0.5) / 0.4);
        }
    }

    static void setShape(VL::Preset &preset, float value)
    {
        switch (preset.driver)
        {
        case VL::DriverType::Jet: preset.jetRatio = 0.2f + 0.3f * value; break;
        case VL::DriverType::Bow: preset.bowForce = value; break;
        default: preset.reedOffset = 0.5f + 0.4f * value; break;
        }
    }

    //===------------------------------------------------------------------===//
    // Source items: none, velocity, aftertouch, note number, then CC 0..79
    //===------------------------------------------------------------------===//

    static constexpr int numSpecialSources = 4;
    static constexpr int numCCSources = 80;
    static constexpr int numSourceItems = numSpecialSources + numCCSources;

    static int sourceForItem(int item)
    {
        switch (item)
        {
        case 0: return VL::Source::none;
        case 1: return VL::Source::velocity;
        case 2: return VL::Source::aftertouch;
        case 3: return VL::Source::noteNumber;
        default: return VL::Source::firstCC + (item - numSpecialSources);
        }
    }

    static int itemForSource(int source)
    {
        if (source == VL::Source::velocity) { return 1; }
        if (source == VL::Source::aftertouch) { return 2; }
        if (source == VL::Source::noteNumber) { return 3; }
        if (source >= VL::Source::firstCC && source < VL::Source::firstCC + numCCSources) { return numSpecialSources + source; }
        return 0;
    }

    //===------------------------------------------------------------------===//
    // Modifier items
    //===------------------------------------------------------------------===//

    static constexpr int numModifierItems = 7;

    static String getModifierName(int item)
    {
        switch (item)
        {
        case 0: return "Harmonic enhancer";
        case 1: return "Dynamic filter";
        case 2: return "Equalizer";
        case 3: return "Impulse expander";
        case 4: return "Resonator bank";
        case 5: return "Chorus";
        case 6: return "Reverb";
        default: return {};
        }
    }

    static bool isModifierEnabled(const VL::Preset &preset, int item)
    {
        switch (item)
        {
        case 0: return preset.modifiers.harmonicEnhancer.enabled;
        case 1: return preset.modifiers.dynamicFilter.enabled;
        case 2: return preset.modifiers.equalizer.enabled;
        case 3: return preset.modifiers.impulseExpander.enabled;
        case 4: return preset.modifiers.resonatorBank.enabled;
        case 5: return preset.effects.chorus.enabled;
        case 6: return preset.effects.reverb.enabled;
        default: return false;
        }
    }

    static void setModifierEnabled(VL::Preset &preset, int item, bool enabled)
    {
        switch (item)
        {
        case 0: preset.modifiers.harmonicEnhancer.enabled = enabled; break;
        case 1: preset.modifiers.dynamicFilter.enabled = enabled; break;
        case 2: preset.modifiers.equalizer.enabled = enabled; break;
        case 3: preset.modifiers.impulseExpander.enabled = enabled; break;
        case 4: preset.modifiers.resonatorBank.enabled = enabled; break;
        case 5: preset.effects.chorus.enabled = enabled; break;
        case 6: preset.effects.reverb.enabled = enabled; break;
        default: break;
        }
    }

    // the "amount" of each: its mix, or for the dynamic filter its depth
    // in octaves, or for the equalizer the presence band's gain
    static double getModifierAmount(const VL::Preset &preset, int item)
    {
        switch (item)
        {
        case 0: return double(preset.modifiers.harmonicEnhancer.mix);
        case 1: return double(preset.modifiers.dynamicFilter.depth) / 8.0;
        case 2: return jlimit(0.0, 1.0, (double(preset.modifiers.equalizer.bands[3].gain) + 12.0) / 24.0);
        case 3: return double(preset.modifiers.impulseExpander.mix);
        case 4: return double(preset.modifiers.resonatorBank.mix);
        case 5: return double(preset.effects.chorus.mix);
        case 6: return double(preset.effects.reverb.mix);
        default: return 0.0;
        }
    }

    static void setModifierAmount(VL::Preset &preset, int item, float value)
    {
        switch (item)
        {
        case 0: preset.modifiers.harmonicEnhancer.mix = value; break;
        case 1: preset.modifiers.dynamicFilter.depth = value * 8.f; break;
        case 2: preset.modifiers.equalizer.bands[3].gain = value * 24.f - 12.f; break;
        case 3: preset.modifiers.impulseExpander.mix = value; break;
        case 4: preset.modifiers.resonatorBank.mix = value; break;
        case 5: preset.effects.chorus.mix = value; break;
        case 6: preset.effects.reverb.mix = value; break;
        default: break;
        }
    }

    //===------------------------------------------------------------------===//
    // User presets
    //===------------------------------------------------------------------===//

    String getPresetsDirectory() const
    {
        return App::Config().getProperty(Serialization::UI::lastWindPresetsPath,
            File::getSpecialLocation(File::userDocumentsDirectory).getFullPathName());
    }

    void savePreset()
    {
        const auto &preset = this->audioPlugin->getSynthParameters().preset;
        const auto defaultFileName = File::createLegalFileName(preset.name.trimCharactersAtEnd(" *") + ".json");

        this->fileChooser = make<FileChooser>(TRANS(I18n::Dialog::documentExport),
            File(this->getPresetsDirectory()).getChildFile(defaultFileName), "*.json", true);

        DocumentHelpers::showFileChooser(this->fileChooser,
            Globals::UI::FileChooser::forFileToSave,
            [this](URL &url)
            {
                if (!url.isLocalFile())
                {
                    return;
                }

                const auto file = url.getLocalFile();
                App::Config().setProperty(Serialization::UI::lastWindPresetsPath,
                    file.getParentDirectory().getFullPathName());

                this->audioPlugin->saveUserPreset(file);
            });
    }

    void loadPreset()
    {
        this->fileChooser = make<FileChooser>(TRANS(I18n::Dialog::documentLoad),
            this->getPresetsDirectory(), "*.json", true);

        DocumentHelpers::showFileChooser(this->fileChooser,
            Globals::UI::FileChooser::forFileToOpen,
            [this](URL &url)
            {
                if (!url.isLocalFile() || !url.getLocalFile().exists())
                {
                    return;
                }

                const auto file = url.getLocalFile();
                App::Config().setProperty(Serialization::UI::lastWindPresetsPath,
                    file.getParentDirectory().getFullPathName());

                if (this->audioPlugin->loadUserPreset(file))
                {
                    this->syncDataWithAudioPlugin();
                    App::Workspace().autosave();
                }
            });
    }

    //===------------------------------------------------------------------===//
    // Members
    //===------------------------------------------------------------------===//

    WeakReference<VLSynthAudioPlugin> audioPlugin;
    bool isSyncing = false;

    int selectedController = 0;
    int selectedModifier = 0;

    UniquePointer<TextEditor> programNameLabel;
    UniquePointer<MobileComboBox::Container> programsComboBox;
    UniquePointer<IconButton> savePresetButton;
    UniquePointer<IconButton> loadPresetButton;
    UniquePointer<FileChooser> fileChooser;

    UniquePointer<TextEditor> breathModeLabel;
    UniquePointer<MobileComboBox::Container> breathModesComboBox;
    UniquePointer<TextEditor> driverLabel;
    UniquePointer<MobileComboBox::Container> driversComboBox;
    UniquePointer<TextEditor> resonatorLabel;
    UniquePointer<MobileComboBox::Container> resonatorsComboBox;

    UniquePointer<Slider> shapeKnob;
    UniquePointer<Slider> brightnessKnob;
    UniquePointer<Slider> dampingKnob;
    UniquePointer<Slider> noiseKnob;
    UniquePointer<Label> shapeLabel;
    UniquePointer<Label> brightnessLabel;
    UniquePointer<Label> dampingLabel;
    UniquePointer<Label> noiseLabel;

    UniquePointer<TextEditor> controllerLabel;
    UniquePointer<MobileComboBox::Container> controllersComboBox;
    UniquePointer<TextEditor> sourceLabel;
    UniquePointer<MobileComboBox::Container> sourcesComboBox;
    UniquePointer<Slider> depthKnob;
    UniquePointer<Slider> baseKnob;
    UniquePointer<Label> depthLabel;
    UniquePointer<Label> baseLabel;

    UniquePointer<TextEditor> modifierLabel;
    UniquePointer<MobileComboBox::Container> modifiersComboBox;
    UniquePointer<ToggleButton> modifierToggle;
    UniquePointer<Slider> amountKnob;
    UniquePointer<Label> amountLabel;

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
double VLSynthAudioPlugin::getTailLengthSeconds() const { return 1.0 + this->synth.getTailLengthSeconds(); }

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
