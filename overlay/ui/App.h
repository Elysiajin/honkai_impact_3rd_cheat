#pragma once

#include "UiCore.h"

namespace ui
{

void AppInit();

void AppFrame(float dt);

bool AppVisible();
void AppSetVisible(bool v);

}
