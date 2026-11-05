#pragma once

#include "UiCore.h"

namespace ui
{

enum class Page
{
    Home = 0,
    Esp,
    Battle,
    Ui,
    About,
    Count
};

const char* PageName(Page p);
const char* PageIcon(Page p);

void PageHome();
void PageEsp();
void PageBattle();
void PageUi();
void PageAbout();

}
