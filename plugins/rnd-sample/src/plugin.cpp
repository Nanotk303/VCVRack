#include "plugin.hpp"

Plugin* pluginInstance;

extern Model* modelRndSample;
extern Model* modelStfPad1;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelRndSample);
	p->addModel(modelStfPad1);
}
