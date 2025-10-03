#pragma once
#include "gav_log.h"

#include <godot_cpp/classes/node.hpp>

class GAVSingleton : public godot::Node {
	GDCLASS(GAVSingleton, Node)
	GAVLog log = GAVLog("GAVSingleton");

	static void _bind_methods() {};

public:
	GAVSingleton();
	~GAVSingleton();
};
