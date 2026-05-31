#include "plugin.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace {
constexpr int kHarmonics = 8;
constexpr int kTableSize = 2048;
constexpr int kMaxChannels = 16;
constexpr float kTwoPi = 2.f * M_PI;

struct SimpleAdsr {
	enum Stage {
		IDLE,
		ATTACK,
		DECAY,
		SUSTAIN,
		RELEASE
	};

	Stage stage = IDLE;
	float env = 0.f;

	void gate(bool high) {
		if (high) {
			if (stage == IDLE || stage == RELEASE)
				stage = ATTACK;
		}
		else if (stage != IDLE && stage != RELEASE) {
			stage = RELEASE;
		}
	}

	float process(float dt, float attack, float release) {
		constexpr float decay = 0.5f;
		constexpr float sustain = 0.85f;

		switch (stage) {
		case IDLE:
			env = 0.f;
			break;
		case ATTACK:
			env += dt / std::max(attack, 0.001f);
			if (env >= 1.f) {
				env = 1.f;
				stage = DECAY;
			}
			break;
		case DECAY:
			env -= dt * (1.f - sustain) / decay;
			if (env <= sustain) {
				env = sustain;
				stage = SUSTAIN;
			}
			break;
		case SUSTAIN:
			env = sustain;
			break;
		case RELEASE:
			env -= dt / std::max(release, 0.001f);
			if (env <= 0.f) {
				env = 0.f;
				stage = IDLE;
			}
			break;
		}
		return env;
	}
};

struct SmoothRandom {
	float current = 0.f;
	float target = 0.f;
	float phase = 0.f;
	float rate = 0.5f;

	float process(float dt, float amount, std::mt19937& rng) {
		phase += dt * rate;
		if (phase >= 1.f) {
			phase -= std::floor(phase);
			current = target;
			std::uniform_real_distribution<float> dist(-amount, amount);
			target = dist(rng);
		}
		const float t = phase * phase * (3.f - 2.f * phase);
		return current + (target - current) * t;
	}
};

struct MovingPan {
	float pan = 0.5f;
	float target = 0.5f;
	float phase = 0.f;
	float rate = 0.07f;
	float min = 0.15f;
	float max = 0.85f;

	float process(float dt, std::mt19937& rng) {
		phase += dt * rate;
		if (phase >= 1.f) {
			phase -= std::floor(phase);
			pan = target;
			std::uniform_real_distribution<float> dist(min, max);
			target = dist(rng);
		}
		const float t = phase * phase * (3.f - 2.f * phase);
		return pan + (target - pan) * t;
	}
};
} // namespace

struct StfPad1 : Module {
	enum ParamId {
		LEVEL_PARAM,
		RISE_PARAM,
		RELEASE_PARAM,
		DRIFT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT,
		GATE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		GATE_LIGHT,
		LIGHTS_LEN
	};

	std::array<float, kHarmonics> harmonics = {1.f, 0.35f, 0.18f, 0.11f, 0.07f, 0.045f, 0.03f, 0.f};
	std::array<float, kTableSize + 1> table = {};
	std::array<float, kMaxChannels> phase1 = {};
	std::array<float, kMaxChannels> phase2 = {};
	std::array<float, kMaxChannels> phase3 = {};
	std::array<SimpleAdsr, kMaxChannels> envs;
	std::array<SmoothRandom, kMaxChannels> det1;
	std::array<SmoothRandom, kMaxChannels> det2;
	std::array<SmoothRandom, kMaxChannels> det3;
	std::array<MovingPan, 3> pans;
	dsp::RCFilter leftTone;
	dsp::RCFilter rightTone;
	std::mt19937 rng;

	StfPad1() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(LEVEL_PARAM, -36.f, 0.f, -6.f, "Level", " dB");
		configParam(RISE_PARAM, 0.01f, 8.f, 0.8f, "Attack / rise", " s");
		configParam(RELEASE_PARAM, 0.05f, 12.f, 3.f, "Release / decay", " s");
		configParam(DRIFT_PARAM, 0.f, 1.2f, 0.18f, "Detune drift", " semitones");
		configInput(VOCT_INPUT, "V/oct");
		configInput(GATE_INPUT, "Gate");
		configOutput(LEFT_OUTPUT, "Left");
		configOutput(RIGHT_OUTPUT, "Right");
		configLight(GATE_LIGHT, "Gate");

		rng.seed(0x5afe1ad1);
		for (int c = 0; c < kMaxChannels; ++c) {
			det1[c].rate = 0.50f;
			det2[c].rate = 0.33f;
			det3[c].rate = 0.25f;
		}
		pans[0].min = 0.15f; pans[0].max = 0.85f; pans[0].rate = 0.08f;
		pans[1].min = 0.15f; pans[1].max = 0.85f; pans[1].rate = 0.065f;
		pans[2].min = 0.25f; pans[2].max = 0.75f; pans[2].rate = 0.05f;
		rebuildTable();
	}

	void rebuildTable() {
		float peak = 0.f;
		for (int i = 0; i < kTableSize; ++i) {
			const float phase = (float) i / (float) kTableSize;
			float v = 0.f;
			for (int h = 0; h < kHarmonics; ++h)
				v += harmonics[h] * std::sin(kTwoPi * phase * (float) (h + 1));
			table[i] = v;
			peak = std::max(peak, std::fabs(v));
		}
		if (peak < 1e-6f)
			peak = 1.f;
		for (int i = 0; i < kTableSize; ++i)
			table[i] /= peak;
		table[kTableSize] = table[0];
	}

	void setHarmonic(int index, float value) {
		if (index < 0 || index >= kHarmonics)
			return;
		harmonics[index] = clamp(value, 0.f, 1.f);
		rebuildTable();
	}

	void setPreset(const std::array<float, kHarmonics>& values) {
		harmonics = values;
		rebuildTable();
	}

	float osc(float& phase, float freq, float dt) {
		phase += freq * dt;
		phase -= std::floor(phase);
		const float x = phase * (float) kTableSize;
		const int i = (int) x;
		const float frac = x - (float) i;
		return crossfade(table[i], table[i + 1], frac);
	}

	static float panGainL(float pan) {
		return std::cos(clamp(pan, 0.f, 1.f) * M_PI * 0.5f);
	}

	static float panGainR(float pan) {
		return std::sin(clamp(pan, 0.f, 1.f) * M_PI * 0.5f);
	}

	void process(const ProcessArgs& args) override {
		const int pitchChannels = inputs[VOCT_INPUT].getChannels();
		const int gateChannels = inputs[GATE_INPUT].getChannels();
		const int channels = std::max(1, std::max(pitchChannels, gateChannels));
		const float levelDb = params[LEVEL_PARAM].getValue();
		const float amp = dsp::dbToAmplitude(clamp(levelDb, -60.f, 6.f));
		const float rise = params[RISE_PARAM].getValue();
		const float rel = params[RELEASE_PARAM].getValue();
		const float drift = params[DRIFT_PARAM].getValue();

		float left = 0.f;
		float right = 0.f;
		const float pan1 = pans[0].process(args.sampleTime, rng);
		const float pan2 = pans[1].process(args.sampleTime, rng);
		const float pan3 = pans[2].process(args.sampleTime, rng);
		bool anyGate = false;

		for (int c = 0; c < channels && c < kMaxChannels; ++c) {
			const int gateChannel = gateChannels <= 1 ? 0 : std::min(c, gateChannels - 1);
			const int pitchChannel = pitchChannels <= 1 ? 0 : std::min(c, pitchChannels - 1);
			const bool gate = !inputs[GATE_INPUT].isConnected() || inputs[GATE_INPUT].getVoltage(gateChannel) >= 1.f;
			anyGate = anyGate || gate;
			envs[c].gate(gate);
			const float env = envs[c].process(args.sampleTime, rise, rel);
			const float pitch = inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getVoltage(pitchChannel) : 0.f;
			const float baseFreq = dsp::FREQ_C4 * std::pow(2.f, pitch);

			const float f1 = baseFreq * std::pow(2.f, det1[c].process(args.sampleTime, drift, rng) / 12.f);
			const float f2 = baseFreq * std::pow(2.f, det2[c].process(args.sampleTime, drift, rng) / 12.f);
			const float f3 = baseFreq * std::pow(2.f, det3[c].process(args.sampleTime, drift, rng) / 12.f);

			const float a1 = osc(phase1[c], f1, args.sampleTime) * env * amp * 0.33f;
			const float a2 = osc(phase2[c], f2, args.sampleTime) * env * amp * 0.33f;
			const float a3 = osc(phase3[c], f3, args.sampleTime) * env * amp * 0.33f;

			left += a1 * panGainL(pan1) + a2 * panGainL(pan2) + a3 * panGainL(pan3);
			right += a1 * panGainR(pan1) + a2 * panGainR(pan2) + a3 * panGainR(pan3);
		}

		leftTone.setCutoffFreq(1800.f * args.sampleTime);
		rightTone.setCutoffFreq(1800.f * args.sampleTime);
		leftTone.process(left * 5.f);
		rightTone.process(right * 5.f);
		outputs[LEFT_OUTPUT].setVoltage(clamp(leftTone.lowpass(), -10.f, 10.f));
		outputs[RIGHT_OUTPUT].setVoltage(clamp(rightTone.lowpass(), -10.f, 10.f));
		lights[GATE_LIGHT].setBrightnessSmooth(anyGate ? 1.f : 0.f, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* harmonicsJ = json_array();
		for (float h : harmonics)
			json_array_append_new(harmonicsJ, json_real(h));
		json_object_set_new(root, "harmonics", harmonicsJ);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* harmonicsJ = json_object_get(root, "harmonics");
		if (harmonicsJ) {
			for (int i = 0; i < kHarmonics; ++i) {
				json_t* hJ = json_array_get(harmonicsJ, i);
				if (hJ)
					harmonics[i] = clamp((float) json_number_value(hJ), 0.f, 1.f);
			}
			rebuildTable();
		}
	}
};

struct HarmonicEditor : OpaqueWidget {
	StfPad1* module = nullptr;
	int dragIndex = -1;
	Vec dragPos;

	void draw(const DrawArgs& args) override {
		nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, nvgRGB(34, 40, 37));
		nvgFill(args.vg);

		nvgStrokeColor(args.vg, nvgRGBA(241, 213, 139, 48));
		nvgStrokeWidth(args.vg, 1.f);
		for (int i = 1; i < 4; ++i) {
			const float y = box.size.y * (float) i / 4.f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 4.f, y);
			nvgLineTo(args.vg, box.size.x - 4.f, y);
			nvgStroke(args.vg);
		}

		if (module) {
			nvgBeginPath(args.vg);
			for (int i = 0; i <= 96; ++i) {
				const float x = 6.f + (box.size.x - 12.f) * (float) i / 96.f;
				const float phase = (float) i / 96.f;
				float v = 0.f;
				for (int h = 0; h < kHarmonics; ++h)
					v += module->harmonics[h] * std::sin(kTwoPi * phase * (float) (h + 1));
				const float y = box.size.y * 0.34f - clamp(v * 0.17f, -0.28f, 0.28f) * box.size.y;
				if (i == 0)
					nvgMoveTo(args.vg, x, y);
				else
					nvgLineTo(args.vg, x, y);
			}
			nvgStrokeColor(args.vg, nvgRGB(241, 213, 139));
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);

			const float barW = (box.size.x - 18.f) / (float) kHarmonics;
			for (int h = 0; h < kHarmonics; ++h) {
				const float v = module->harmonics[h];
				const float x = 9.f + barW * h;
				const float top = box.size.y - 8.f - v * 34.f;
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x + 2.f, top, barW - 4.f, box.size.y - 8.f - top, 2.f);
				nvgFillColor(args.vg, h == dragIndex ? nvgRGB(240, 192, 88) : nvgRGB(214, 163, 63));
				nvgFill(args.vg);
			}
		}
		nvgResetScissor(args.vg);
	}

	void editAt(Vec pos) {
		if (!module)
			return;
		const float barW = (box.size.x - 18.f) / (float) kHarmonics;
		dragIndex = clamp((int) ((pos.x - 9.f) / barW), 0, kHarmonics - 1);
		const float value = clamp((box.size.y - 8.f - pos.y) / 34.f, 0.f, 1.f);
		module->setHarmonic(dragIndex, value);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			dragPos = e.pos;
			editAt(dragPos);
			e.consume(this);
		}
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			dragPos = dragPos.plus(e.mouseDelta);
			editAt(dragPos);
			e.consume(this);
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		dragIndex = -1;
	}
};

struct StfPadLabels : TransparentWidget {
	static void label(NVGcontext* vg, float x, float y, const char* text, float size, NVGcolor color, int align = NVG_ALIGN_CENTER) {
		nvgFontSize(vg, size);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		nvgFillColor(vg, color);
		nvgTextAlign(vg, align | NVG_ALIGN_MIDDLE);
		nvgText(vg, x, y, text, nullptr);
	}

	void draw(const DrawArgs& args) override {
		const NVGcolor dark = nvgRGB(32, 36, 34);
		const NVGcolor soft = nvgRGB(96, 106, 98);
		const NVGcolor gold = nvgRGB(215, 167, 70);
		const NVGcolor cream = nvgRGB(247, 240, 228);

		label(args.vg, 90, 27, "Stf-Pad1", 19.f, cream);
		label(args.vg, 90, 43, "Nanotk Audio", 8.f, gold);
		label(args.vg, 90, 67, "EDIT WAVE", 7.f, gold);
		label(args.vg, 90, 154, "PAD ENGINE", 7.f, soft);

		label(args.vg, 34, 178, "LEVEL", 7.5f, dark);
		label(args.vg, 72, 178, "RISE", 7.5f, dark);
		label(args.vg, 110, 178, "REL", 7.5f, dark);
		label(args.vg, 148, 178, "DRIFT", 7.5f, dark);
		label(args.vg, 34, 222, "dB", 6.5f, soft);
		label(args.vg, 72, 222, "attack", 6.5f, soft);
		label(args.vg, 110, 222, "decay", 6.5f, soft);
		label(args.vg, 148, 222, "cents", 6.5f, soft);

		label(args.vg, 50, 250, "INPUTS", 7.f, soft);
		label(args.vg, 130, 250, "CONTROL", 7.f, soft);
		label(args.vg, 50, 264, "V/OCT", 8.f, cream);
		label(args.vg, 130, 264, "GATE", 8.f, cream);
		label(args.vg, 50, 291, "pitch", 6.5f, gold);
		label(args.vg, 130, 291, "env", 6.5f, gold);

		label(args.vg, 90, 306, "OUTPUTS", 7.f, soft);
		label(args.vg, 47, 317, "LEFT", 8.f, dark);
		label(args.vg, 133, 317, "RIGHT", 8.f, dark);
	}
};

struct StfPad1Widget : ModuleWidget {
	StfPad1Widget(StfPad1* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/stf-pad1.svg")));

		auto* labels = new StfPadLabels;
		labels->box.pos = Vec(0, 0);
		labels->box.size = Vec(180, 380);
		addChild(labels);

		auto* editor = new HarmonicEditor;
		editor->module = module;
		editor->box.pos = Vec(20, 75);
		editor->box.size = Vec(140, 70);
		addChild(editor);

		addParam(createParamCentered<RoundBlackKnob>(Vec(34, 201), module, StfPad1::LEVEL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(72, 201), module, StfPad1::RISE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(110, 201), module, StfPad1::RELEASE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(148, 201), module, StfPad1::DRIFT_PARAM));

		addInput(createInputCentered<PJ301MPort>(Vec(50, 277), module, StfPad1::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(130, 277), module, StfPad1::GATE_INPUT));
		addChild(createLightCentered<SmallLight<GreenLight>>(Vec(152, 291), module, StfPad1::GATE_LIGHT));

		addOutput(createOutputCentered<PJ301MPort>(Vec(47, 333), module, StfPad1::LEFT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(133, 333), module, StfPad1::RIGHT_OUTPUT));
	}

	void appendContextMenu(ui::Menu* menu) override {
		StfPad1* module = getModule<StfPad1>();
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("stf-pad1 wave"));
		menu->addChild(createMenuItem("Csound PAD1 source", "", [=]() {
			if (module) module->setPreset({1.f, 0.35f, 0.18f, 0.11f, 0.07f, 0.045f, 0.03f, 0.f});
		}));
		menu->addChild(createMenuItem("Warm saw pad", "", [=]() {
			if (module) module->setPreset({1.f, 0.55f, 0.36f, 0.25f, 0.18f, 0.13f, 0.09f, 0.06f});
		}));
		menu->addChild(createMenuItem("Hollow organ", "", [=]() {
			if (module) module->setPreset({1.f, 0.f, 0.42f, 0.f, 0.21f, 0.f, 0.1f, 0.f});
		}));
		menu->addChild(createMenuItem("Soft sine", "", [=]() {
			if (module) module->setPreset({1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
		}));
	}
};

Model* modelStfPad1 = createModel<StfPad1, StfPad1Widget>("stf-pad1");
