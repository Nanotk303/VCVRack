#include "plugin.hpp"

Plugin* pluginInstance;

extern Model* modelRndSample;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelRndSample);
}
