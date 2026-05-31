#include "plugin.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

struct RndSample : Module {
	enum ParamId {
		PROB_PARAM,
		N_PARAM,
		SEED_PARAM,
		NOREP_PARAM,
		RESET_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TRIG_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		VOCT_OUTPUT,
		CV_OUTPUT,
		INDEX_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		TRIG_LIGHT,
		LIGHTS_LEN
	};

	std::string basketText = "0 2 4 5 7 9 11";
	std::vector<float> basketValues;
	std::mt19937 rng;
	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger resetButtonTrigger;
	dsp::PulseGenerator gatePulse;
	int lastIndex = -1;
	int lastSeed = -1;

	RndSample() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PROB_PARAM, 0.f, 1.f, 0.5f, "Probability bias");
		configParam(N_PARAM, 1.f, 16.f, 1.f, "Number of samples");
		getParamQuantity(N_PARAM)->snapEnabled = true;
		configParam(SEED_PARAM, 0.f, 9999.f, 32.f, "Seed");
		getParamQuantity(SEED_PARAM)->snapEnabled = true;
		configSwitch(NOREP_PARAM, 0.f, 1.f, 1.f, "No consecutive repeat", {"Off", "On"});
		configButton(RESET_PARAM, "Reset seed");
		configInput(TRIG_INPUT, "Trigger");
		configInput(RESET_INPUT, "Reset seed");
		configOutput(VOCT_OUTPUT, "V/oct sample");
		configOutput(CV_OUTPUT, "Raw CV sample");
		configOutput(INDEX_OUTPUT, "Basket index");
		configOutput(GATE_OUTPUT, "Gate");
		configLight(TRIG_LIGHT, "Trigger");
		parseBasket();
		reseed();
	}

	void parseBasket() {
		basketValues.clear();
		std::string cleaned = basketText;
		for (char& c : cleaned) {
			if (c == ',' || c == ';' || c == '(' || c == ')' || c == '[' || c == ']')
				c = ' ';
		}

		std::stringstream stream(cleaned);
		float value = 0.f;
		while (stream >> value)
			basketValues.push_back(value);

		if (basketValues.empty())
			basketValues.push_back(0.f);
	}

	void setBasketText(const std::string& text) {
		basketText = text;
		parseBasket();
		lastIndex = -1;
	}

	void reseed() {
		const int seed = (int) std::round(params[SEED_PARAM].getValue());
		rng.seed((uint32_t) seed);
		lastSeed = seed;
		lastIndex = -1;
	}

	int chooseIndex(float prob, bool norep) {
		const int size = (int) basketValues.size();
		if (size <= 1)
			return 0;

		prob = clamp(prob, 0.f, 1.f);
		const float bias = (prob - 0.5f) * 8.f;
		std::vector<float> weights(size);
		float sum = 0.f;
		for (int i = 0; i < size; ++i) {
			const float position = size == 1 ? 0.5f : (float) i / (float) (size - 1);
			float weight = std::exp((position - 0.5f) * bias);
			if (norep && i == lastIndex)
				weight = 0.f;
			weights[i] = weight;
			sum += weight;
		}

		if (sum <= 0.f) {
			std::uniform_int_distribution<int> dist(0, size - 1);
			return dist(rng);
		}

		std::uniform_real_distribution<float> dist(0.f, sum);
		float pick = dist(rng);
		for (int i = 0; i < size; ++i) {
			pick -= weights[i];
			if (pick <= 0.f)
				return i;
		}
		return size - 1;
	}

	void sampleNow() {
		const int n = clamp((int) std::round(params[N_PARAM].getValue()), 1, 16);
		const bool norep = params[NOREP_PARAM].getValue() > 0.5f;
		const float prob = params[PROB_PARAM].getValue();

		outputs[VOCT_OUTPUT].setChannels(n);
		outputs[CV_OUTPUT].setChannels(n);
		outputs[INDEX_OUTPUT].setChannels(n);
		outputs[GATE_OUTPUT].setChannels(n);

		for (int c = 0; c < n; ++c) {
			const int index = chooseIndex(prob, norep);
			const float value = basketValues[index];
			outputs[VOCT_OUTPUT].setVoltage(value / 12.f, c);
			outputs[CV_OUTPUT].setVoltage(value, c);
			outputs[INDEX_OUTPUT].setVoltage((float) index, c);
			lastIndex = index;
		}

		gatePulse.trigger(1e-3f);
	}

	void process(const ProcessArgs& args) override {
		const int seed = (int) std::round(params[SEED_PARAM].getValue());
		if (seed != lastSeed)
			reseed();

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage()) || resetButtonTrigger.process(params[RESET_PARAM].getValue()))
			reseed();

		if (trigTrigger.process(inputs[TRIG_INPUT].getVoltage()))
			sampleNow();

		const bool gate = gatePulse.process(args.sampleTime);
		const int channels = std::max(1, outputs[VOCT_OUTPUT].getChannels());
		outputs[GATE_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; ++c)
			outputs[GATE_OUTPUT].setVoltage(gate ? 10.f : 0.f, c);
		lights[TRIG_LIGHT].setBrightnessSmooth(gate ? 1.f : 0.f, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "basket", json_string(basketText.c_str()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* basketJ = json_object_get(root, "basket");
		if (basketJ)
			setBasketText(json_string_value(basketJ));
	}
};

struct BasketDisplay : TransparentWidget {
	RndSample* module = nullptr;

	void draw(const DrawArgs& args) override {
		nvgFillColor(args.vg, nvgRGB(244, 239, 229));
		nvgFontSize(args.vg, 9.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		std::string text = module ? module->basketText : "0 2 4 5 7";
		if (text.size() > 22)
			text = text.substr(0, 21) + "...";

		nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.68f, text.c_str(), nullptr);
	}
};

struct PanelLabels : TransparentWidget {
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

		label(args.vg, 75, 29, "Rnd-Sample", 18.f, cream);
		label(args.vg, 75, 45, "Nanotk Audio", 8.f, gold);

		label(args.vg, 75, 60, "BASKET", 7.5f, gold);

		label(args.vg, 75, 101, "SELECTION", 7.f, soft);
		label(args.vg, 36, 116, "PROB", 8.f, dark);
		label(args.vg, 75, 116, "N", 8.f, dark);
		label(args.vg, 114, 116, "SEED", 8.f, dark);
		label(args.vg, 36, 157, "bias", 6.5f, soft);
		label(args.vg, 75, 157, "voices", 6.5f, soft);
		label(args.vg, 114, 157, "replay", 6.5f, soft);

		label(args.vg, 75, 181, "RULES", 7.f, soft);
		label(args.vg, 38, 197, "NOREP", 8.f, dark);
		label(args.vg, 112, 197, "RESET", 8.f, dark);
		label(args.vg, 38, 229, "no repeat", 6.5f, soft);
		label(args.vg, 112, 229, "seed", 6.5f, soft);

		label(args.vg, 75, 248, "INPUTS", 7.f, soft);
		label(args.vg, 38, 256, "TRIG", 8.f, dark);
		label(args.vg, 112, 256, "RESET", 8.f, dark);

		label(args.vg, 75, 293, "OUTPUTS", 7.f, soft);
		label(args.vg, 38, 296, "V/OCT", 8.f, dark);
		label(args.vg, 112, 296, "CV", 8.f, dark);
		label(args.vg, 38, 329, "INDEX", 8.f, dark);
		label(args.vg, 112, 329, "GATE", 8.f, dark);
	}
};

struct BasketTextField : ui::TextField {
	RndSample* module = nullptr;

	void onSelectKey(const SelectKeyEvent& e) override {
		ui::TextField::onSelectKey(e);
		if ((e.action == GLFW_PRESS || e.action == GLFW_REPEAT) && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			if (module)
				module->setBasketText(text);
			e.consume(this);
		}
	}

	void onDeselect(const DeselectEvent& e) override {
		if (module)
			module->setBasketText(text);
		ui::TextField::onDeselect(e);
	}
};

struct RndSampleWidget : ModuleWidget {
	RndSampleWidget(RndSample* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/rnd-sample.svg")));

		auto* labels = new PanelLabels;
		labels->box.pos = Vec(0, 0);
		labels->box.size = Vec(150, 380);
		addChild(labels);

		auto* display = new BasketDisplay;
		display->module = module;
		display->box.pos = Vec(18, 46);
		display->box.size = Vec(114, 42);
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(Vec(36, 139), module, RndSample::PROB_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(75, 139), module, RndSample::N_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(114, 139), module, RndSample::SEED_PARAM));
		addParam(createParamCentered<CKSS>(Vec(38, 216), module, RndSample::NOREP_PARAM));
		addParam(createParamCentered<LEDButton>(Vec(112, 216), module, RndSample::RESET_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(Vec(112, 216), module, RndSample::TRIG_LIGHT));

		addInput(createInputCentered<PJ301MPort>(Vec(38, 279), module, RndSample::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(112, 279), module, RndSample::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(Vec(38, 315), module, RndSample::VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(112, 315), module, RndSample::CV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(38, 343), module, RndSample::INDEX_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(112, 343), module, RndSample::GATE_OUTPUT));
	}

	void appendContextMenu(ui::Menu* menu) override {
		RndSample* module = getModule<RndSample>();
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("rnd-sample basket"));

		auto* field = new BasketTextField;
		field->module = module;
		field->box.size = Vec(240, 32);
		field->placeholder = "0 2 4 5 7 9 11";
		if (module)
			field->setText(module->basketText);
		menu->addChild(field);

		menu->addChild(createMenuLabel("Examples"));
		menu->addChild(createMenuItem("Major scale", "", [=]() {
			if (module) module->setBasketText("0 2 4 5 7 9 11");
		}));
		menu->addChild(createMenuItem("Minor pentatonic", "", [=]() {
			if (module) module->setBasketText("0 3 5 7 10");
		}));
		menu->addChild(createMenuItem("Octaves and fifths", "", [=]() {
			if (module) module->setBasketText("-24 -12 -7 0 7 12 24");
		}));
		menu->addChild(createMenuItem("Rhythmic voltages", "", [=]() {
			if (module) module->setBasketText("0 1 2 3 5 8 13");
		}));
	}
};

Model* modelRndSample = createModel<RndSample, RndSampleWidget>("rnd-sample");
