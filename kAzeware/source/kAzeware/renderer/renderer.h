#pragma once

#include "../menu/menu.h"
#include "../features/visuals/visuals.h"
#include "../menu/hud.h"
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

class Renderer {
public:
    Menu menu;
    Esp::Visuals visuals;
    Hud hud;
};
