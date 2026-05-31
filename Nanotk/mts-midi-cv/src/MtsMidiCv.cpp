#include "plugin.hpp"

#include <algorithm>
#include <array>
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
	std::array<float, 128> tunedSemitones;
	std::array<bool, 128> tuned;
	bool mtsSeen = false;

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
		for (int i = 0; i < 128; ++i) {
			tunedSemitones[i] = (float) i;
			tuned[i] = false;
		}
		mtsSeen = false;
	}

	static float decodeMtsFrequency(uint8_t coarse, uint8_t fineMsb, uint8_t fineLsb) {
		int fine = ((int) fineMsb << 7) | (int) fineLsb;
		return (float) coarse + (float) fine / 16384.f;
	}

	void setTunedNote(uint8_t note, uint8_t coarse, uint8_t fineMsb, uint8_t fineLsb) {
		if (note >= 128 || coarse >= 128)
			return;
		tunedSemitones[note] = decodeMtsFrequency(coarse, fineMsb, fineLsb);
		tuned[note] = true;
		mtsSeen = true;
	}

	void applyScaleOctave1Byte(const std::vector<uint8_t>& bytes, size_t start) {
		if (bytes.size() < start + 12)
			return;
		for (int pc = 0; pc < 12; ++pc) {
			float cents = (float) bytes[start + pc] - 64.f;
			for (int note = pc; note < 128; note += 12) {
				tunedSemitones[note] = (float) note + cents / 100.f;
				tuned[note] = true;
			}
		}
		mtsSeen = true;
	}

	void applyScaleOctave2Byte(const std::vector<uint8_t>& bytes, size_t start) {
		if (bytes.size() < start + 24)
			return;
		for (int pc = 0; pc < 12; ++pc) {
			int raw = ((int) bytes[start + 2 * pc] << 7) | (int) bytes[start + 2 * pc + 1];
			float cents = ((float) raw - 8192.f) * (100.f / 8192.f);
			for (int note = pc; note < 128; note += 12) {
				tunedSemitones[note] = (float) note + cents / 100.f;
				tuned[note] = true;
			}
		}
		mtsSeen = true;
	}

	bool processMtsSysex(const midi::Message& msg) {
		const std::vector<uint8_t>& b = msg.bytes;
		if (b.size() < 7 || b.front() != 0xf0 || b[3] != 0x08)
			return false;
		if (b[1] != 0x7e && b[1] != 0x7f)
			return false;

		const uint8_t subId2 = b[4];
		if (subId2 == 0x01) {
			// Bulk tuning dump: program, 16-byte name, then 128 MTS frequencies.
			size_t p = 22;
			for (int note = 0; note < 128 && p + 2 < b.size(); ++note, p += 3)
				setTunedNote(note, b[p], b[p + 1], b[p + 2]);
			return true;
		}
		if (subId2 == 0x02) {
			// Single note tuning change: program, count, then note + 3-byte MTS frequency.
			if (b.size() < 7)
				return true;
			int count = b[6];
			size_t p = 7;
			for (int i = 0; i < count && p + 3 < b.size(); ++i, p += 4)
				setTunedNote(b[p], b[p + 1], b[p + 2], b[p + 3]);
			return true;
		}
		if (subId2 == 0x04) {
			applyScaleOctave1Byte(b, 6);
			return true;
		}
		if (subId2 == 0x05) {
			applyScaleOctave2Byte(b, 6);
			return true;
		}
		if (subId2 == 0x07 || subId2 == 0x08) {
			// Common banked variants: bank, program, count, then note + 3-byte MTS frequency.
			if (b.size() < 8)
				return true;
			int count = b[7];
			size_t p = 8;
			for (int i = 0; i < count && p + 3 < b.size(); ++i, p += 4)
				setTunedNote(b[p], b[p + 1], b[p + 2], b[p + 3]);
			return true;
		}
		return true;
	}

	float getMtsPitchVoltage(int channel) {
		uint8_t note = midiParser.notes[channel];
		uint8_t wheelChannel = (midiParser.channels > 1 && midiParser.polyMode == dsp::MidiParser<16>::MPE_MODE) ? channel : 0;
		float semitone = tuned[note] ? tunedSemitones[note] : (float) note;
		semitone += midiParser.pwFilters[wheelChannel].out * midiParser.pwRange;
		return (semitone - 60.f) / 12.f;
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			if (!processMtsSysex(msg))
				midiParser.processMessage(msg);
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
		lights[MTS_LIGHT].setBrightnessSmooth(mtsSeen ? 1.f : 0.f, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "midi", midiInput.toJson());
		json_object_set_new(rootJ, "parser", midiParser.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* midiJ = json_object_get(rootJ, "midi");
		if (midiJ)
			midiInput.fromJson(midiJ);
		json_t* parserJ = json_object_get(rootJ, "parser");
		if (parserJ)
			midiParser.fromJson(parserJ);
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
		label(args.vg, 90, 150, "PITCH / PERFORMANCE", 7.f, soft);
		label(args.vg, 90, 266, "TRANSPORT", 7.f, soft);

		label(args.vg, 45, 168, "V/OCT", 8.f, dark);
		label(args.vg, 90, 168, "GATE", 8.f, dark);
		label(args.vg, 135, 168, "VEL", 8.f, dark);
		label(args.vg, 45, 215, "AFT", 8.f, dark);
		label(args.vg, 90, 215, "PW", 8.f, dark);
		label(args.vg, 135, 215, "MOD", 8.f, dark);

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
			app::MidiDisplay* midiDisplay = createWidget<app::MidiDisplay>(Vec(18, 85));
			midiDisplay->box.size = Vec(144, 67);
			midiDisplay->setMidiPort(&module->midiInput);
			addChild(midiDisplay);
		}

		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(151, 35), module, MtsMidiCv::MTS_LIGHT));

		addOutput(createOutputCentered<PJ301MPort>(Vec(45, 184), module, MtsMidiCv::VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(90, 184), module, MtsMidiCv::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(135, 184), module, MtsMidiCv::VELOCITY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(45, 231), module, MtsMidiCv::AFTERTOUCH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(90, 231), module, MtsMidiCv::PW_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(135, 231), module, MtsMidiCv::MOD_OUTPUT));
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
		menu->addChild(createMenuItem("Panic", "", [=]() { if (module) module->midiParser.panic(); }));
		menu->addChild(createMenuItem("Clear MTS tuning", "", [=]() { if (module) module->clearTuning(); }));
	}
};

Model* modelMtsMidiCv = createModel<MtsMidiCv, MtsMidiCvWidget>("mts-midi-cv");
