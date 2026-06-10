#include "plugin.hpp"
#include "MtsTuning.hpp"

#include <algorithm>
#include <cmath>

struct MtsMidiCv : Module {
	enum ParamId {
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		VOCT_OUTPUT,
		GATE_OUTPUT,
		VELOCITY_OUTPUT,
		AFTERTOUCH_OUTPUT,
		PW_OUTPUT,
		MOD_OUTPUT,
		CLOCK_OUTPUT,
		CLOCK_DIV_OUTPUT,
		RETRIGGER_OUTPUT,
		START_OUTPUT,
		STOP_OUTPUT,
		CONTINUE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		MTS_LIGHT,
		LIGHTS_LEN
	};

	midi::InputQueue midiInput;
	dsp::MidiParser<16> midiParser;
	nanotk::MtsTuning mtsTuning;
	bool resetSingleNoteTuningOnRelease = true;

	MtsMidiCv() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configOutput(VOCT_OUTPUT, "Pitch (V/oct)");
		configOutput(GATE_OUTPUT, "Gate");
		configOutput(VELOCITY_OUTPUT, "Velocity");
		configOutput(AFTERTOUCH_OUTPUT, "Aftertouch");
		configOutput(PW_OUTPUT, "Pitch wheel");
		configOutput(MOD_OUTPUT, "Mod wheel");
		configOutput(CLOCK_OUTPUT, "Clock");
		configOutput(CLOCK_DIV_OUTPUT, "Clock divided");
		configOutput(RETRIGGER_OUTPUT, "Retrigger");
		configOutput(START_OUTPUT, "Start");
		configOutput(STOP_OUTPUT, "Stop");
		configOutput(CONTINUE_OUTPUT, "Continue");
		configLight(MTS_LIGHT, "MTS received");
		clearTuning();
	}

	void clearTuning() {
		mtsTuning.clear();
	}

	bool processMtsSysex(const midi::Message& msg) {
		return mtsTuning.processSysex(msg.bytes, resetSingleNoteTuningOnRelease);
	}

	float getMtsPitchVoltage(int channel) {
		uint8_t note = midiParser.notes[channel];
		uint8_t wheelChannel = (midiParser.channels > 1 && midiParser.polyMode == dsp::MidiParser<16>::MPE_MODE) ? channel : 0;
		return mtsTuning.getPitchVoltage(note, midiParser.pwFilters[wheelChannel].out, midiParser.pwRange);
	}

	bool isNoteActive(uint8_t note) const {
		for (int c = 0; c < midiParser.channels; ++c) {
			if (midiParser.gates[c] && midiParser.notes[c] == note)
				return true;
		}
		return false;
	}

	void flushPendingTuningResets() {
		for (int note = 0; note < 128; ++note) {
			if (mtsTuning.pendingReset[note] && !isNoteActive((uint8_t) note))
				mtsTuning.resetNote((uint8_t) note);
		}
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			if (processMtsSysex(msg))
				continue;

			const uint8_t status = msg.getStatus();
			const bool noteRelease = status == 0x8 || (status == 0x9 && msg.getValue() == 0);
			const bool sustainRelease = status == 0xb && msg.getNote() == 0x40 && msg.getValue() < 64;
			const bool allNotesOff = status == 0xb && msg.getNote() == 0x7b && msg.getValue() == 0;
			const bool midiStop = status == 0xf && msg.getChannel() == 0x0c;
			const bool systemReset = !msg.bytes.empty() && msg.bytes[0] == 0xff;

			if (noteRelease)
				mtsTuning.requestTransientReset(msg.getNote());

			midiParser.processMessage(msg);

			if (systemReset) {
				midiParser.panic();
				clearTuning();
			}
			else if (allNotesOff || midiStop) {
				mtsTuning.resetAllTransient();
			}
			else if (noteRelease || sustainRelease) {
				flushPendingTuningResets();
			}
		}

		midiParser.processFilters(args.sampleTime);
		midiParser.processPulses(args.sampleTime);

		int channels = midiParser.getChannels();
		for (int outputId = 0; outputId < OUTPUTS_LEN; ++outputId)
			outputs[outputId].setChannels(channels);

		for (int c = 0; c < channels; ++c) {
			outputs[VOCT_OUTPUT].setVoltage(getMtsPitchVoltage(c), c);
			outputs[GATE_OUTPUT].setVoltage(midiParser.gates[c] ? 10.f : 0.f, c);
			outputs[VELOCITY_OUTPUT].setVoltage(10.f * midiParser.velocities[c] / 127.f, c);
			outputs[AFTERTOUCH_OUTPUT].setVoltage(10.f * midiParser.aftertouches[c] / 127.f, c);
			outputs[PW_OUTPUT].setVoltage(5.f * midiParser.getPw(c), c);
			outputs[MOD_OUTPUT].setVoltage(10.f * midiParser.getMod(c), c);
			outputs[CLOCK_OUTPUT].setVoltage(midiParser.clockPulse.isHigh() ? 10.f : 0.f, c);
			outputs[CLOCK_DIV_OUTPUT].setVoltage(midiParser.clockDividerPulse.isHigh() ? 10.f : 0.f, c);
			outputs[RETRIGGER_OUTPUT].setVoltage(midiParser.retriggerPulses[c].isHigh() ? 10.f : 0.f, c);
			outputs[START_OUTPUT].setVoltage(midiParser.startPulse.isHigh() ? 10.f : 0.f, c);
			outputs[STOP_OUTPUT].setVoltage(midiParser.stopPulse.isHigh() ? 10.f : 0.f, c);
			outputs[CONTINUE_OUTPUT].setVoltage(midiParser.continuePulse.isHigh() ? 10.f : 0.f, c);
		}
		lights[MTS_LIGHT].setBrightnessSmooth(mtsTuning.seen ? 1.f : 0.f, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "midi", midiInput.toJson());
		json_object_set_new(rootJ, "parser", midiParser.toJson());
		json_object_set_new(rootJ, "resetSingleNoteTuningOnRelease", json_boolean(resetSingleNoteTuningOnRelease));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* midiJ = json_object_get(rootJ, "midi");
		if (midiJ)
			midiInput.fromJson(midiJ);
		json_t* parserJ = json_object_get(rootJ, "parser");
		if (parserJ)
			midiParser.fromJson(parserJ);
		json_t* resetSingleNoteJ = json_object_get(rootJ, "resetSingleNoteTuningOnRelease");
		if (resetSingleNoteJ)
			resetSingleNoteTuningOnRelease = json_boolean_value(resetSingleNoteJ);
	}
};

struct MtsMidiCvLabels : TransparentWidget {
	static void label(NVGcontext* vg, float x, float y, const char* text, float size, NVGcolor color, int align = NVG_ALIGN_CENTER) {
		nvgFontSize(vg, size);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		nvgFillColor(vg, color);
		nvgTextAlign(vg, align | NVG_ALIGN_MIDDLE);
		nvgText(vg, x, y, text, NULL);
	}

	void draw(const DrawArgs& args) override {
		const NVGcolor dark = nvgRGB(34, 38, 39);
		const NVGcolor soft = nvgRGB(101, 107, 102);
		const NVGcolor gold = nvgRGB(213, 164, 52);
		const NVGcolor cream = nvgRGB(244, 241, 232);

		label(args.vg, 90, 27, "MTS MIDI-CV", 16.f, cream);
		label(args.vg, 90, 45, "Nanotk Audio", 8.f, gold);
		label(args.vg, 134, 45, "MTS", 7.f, gold);

		label(args.vg, 90, 76, "MIDI INPUT", 7.f, soft);
		label(args.vg, 90, 181, "PITCH / PERFORMANCE", 7.f, soft);
		label(args.vg, 90, 266, "TRANSPORT", 7.f, soft);

		label(args.vg, 45, 195, "V/OCT", 8.f, dark);
		label(args.vg, 90, 195, "GATE", 8.f, dark);
		label(args.vg, 135, 195, "VEL", 8.f, dark);
		label(args.vg, 45, 232, "AFT", 8.f, dark);
		label(args.vg, 90, 232, "PW", 8.f, dark);
		label(args.vg, 135, 232, "MOD", 8.f, dark);

		label(args.vg, 45, 284, "CLK", 8.f, dark);
		label(args.vg, 90, 284, "CLK/N", 8.f, dark);
		label(args.vg, 135, 284, "RTRG", 8.f, dark);
		label(args.vg, 45, 331, "STRT", 8.f, dark);
		label(args.vg, 90, 331, "STOP", 8.f, dark);
		label(args.vg, 135, 331, "CONT", 8.f, dark);
	}
};

struct MtsMidiCvWidget : ModuleWidget {
	MtsMidiCvWidget(MtsMidiCv* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/mts-midi-cv.svg")));

		auto* labels = new MtsMidiCvLabels;
		labels->box.pos = Vec(0, 0);
		labels->box.size = Vec(180, 380);
		addChild(labels);

		if (module) {
			app::MidiDisplay* midiDisplay = createWidget<app::MidiDisplay>(Vec(12, 84));
			midiDisplay->box.size = Vec(156, 82);
			midiDisplay->setMidiPort(&module->midiInput);
			addChild(midiDisplay);
		}

		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(151, 35), module, MtsMidiCv::MTS_LIGHT));

		addOutput(createOutputCentered<PJ301MPort>(Vec(45, 210), module, MtsMidiCv::VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(90, 210), module, MtsMidiCv::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(135, 210), module, MtsMidiCv::VELOCITY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(45, 247), module, MtsMidiCv::AFTERTOUCH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(90, 247), module, MtsMidiCv::PW_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(135, 247), module, MtsMidiCv::MOD_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(45, 300), module, MtsMidiCv::CLOCK_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(90, 300), module, MtsMidiCv::CLOCK_DIV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(135, 300), module, MtsMidiCv::RETRIGGER_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(45, 347), module, MtsMidiCv::START_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(90, 347), module, MtsMidiCv::STOP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(135, 347), module, MtsMidiCv::CONTINUE_OUTPUT));
	}

	void appendContextMenu(ui::Menu* menu) override {
		MtsMidiCv* module = getModule<MtsMidiCv>();
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("MTS MIDI-CV"));
		if (module)
			app::appendMidiMenu(menu, &module->midiInput);

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Channels", {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"},
			[=]() { return module ? (size_t) module->midiParser.channels - 1 : 0; },
			[=](size_t i) { if (module) module->midiParser.setChannels((uint8_t) i + 1); }));
		menu->addChild(createIndexSubmenuItem("Mono priority", {"Last", "First", "Lowest", "Highest"},
			[=]() { return module ? (size_t) module->midiParser.monoMode : 0; },
			[=](size_t i) { if (module) module->midiParser.setMonoMode((dsp::MidiParser<16>::MonoMode) i); }));
		menu->addChild(createIndexSubmenuItem("Polyphony mode", {"Rotate", "Reuse", "Reset", "MPE"},
			[=]() { return module ? (size_t) module->midiParser.polyMode : 0; },
			[=](size_t i) { if (module) module->midiParser.setPolyMode((dsp::MidiParser<16>::PolyMode) i); }));
		menu->addChild(createIndexSubmenuItem("Pitch wheel range", {"1 semitone", "2 semitones", "12 semitones", "24 semitones", "48 semitones"},
			[=]() {
				if (!module) return (size_t) 1;
				float r = module->midiParser.pwRange;
				if (r <= 1.f) return (size_t) 0;
				if (r <= 2.f) return (size_t) 1;
				if (r <= 12.f) return (size_t) 2;
				if (r <= 24.f) return (size_t) 3;
				return (size_t) 4;
			},
			[=](size_t i) {
				static const float ranges[] = {1.f, 2.f, 12.f, 24.f, 48.f};
				if (module) module->midiParser.pwRange = ranges[i];
			}));
		menu->addChild(createIndexSubmenuItem("Clock division", {"1", "2", "4", "8", "12", "24", "48", "96"},
			[=]() {
				if (!module) return (size_t) 5;
				static const uint32_t divs[] = {1, 2, 4, 8, 12, 24, 48, 96};
				for (size_t i = 0; i < 8; ++i)
					if (module->midiParser.clockDivision == divs[i])
						return i;
				return (size_t) 5;
			},
			[=](size_t i) {
				static const uint32_t divs[] = {1, 2, 4, 8, 12, 24, 48, 96};
				if (module) module->midiParser.clockDivision = divs[i];
			}));
		menu->addChild(createBoolPtrMenuItem("Smooth wheels", "", module ? &module->midiParser.smooth : NULL));
		menu->addChild(createBoolPtrMenuItem("Release velocity", "", module ? &module->midiParser.releaseVelocityEnabled : NULL));
		menu->addChild(createBoolPtrMenuItem("Retrigger on resume", "", module ? &module->midiParser.retriggerOnResume : NULL));
		menu->addChild(createBoolPtrMenuItem("Reset single-note MTS after Note Off", "", module ? &module->resetSingleNoteTuningOnRelease : NULL));
		menu->addChild(createMenuItem("Panic", "", [=]() {
			if (module) {
				module->midiParser.panic();
				module->mtsTuning.resetAllTransient();
			}
		}));
		menu->addChild(createMenuItem("Clear MTS tuning", "", [=]() { if (module) module->clearTuning(); }));
	}
};

Model* modelMtsMidiCv = createModel<MtsMidiCv, MtsMidiCvWidget>("mts-midi-cv");
