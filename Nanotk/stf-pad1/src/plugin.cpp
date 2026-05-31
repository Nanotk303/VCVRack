#include "plugin.hpp"

Plugin* pluginInstance;

extern Model* modelStfPad1;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelStfPad1);
}
