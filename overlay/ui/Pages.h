#pragma once
#include "UiCore.h"

namespace ui
{

enum class Page
{
    Home = 0,
    Esp,
    Battle,
    Activity,
    Ui,
    About,
    Count
};

const char* PageName(Page p);
const char* PageIcon(Page p);

// 卡片与提示：各页面共用
bool Card(const char* title, const ImVec2& size);
void CardEnd();
void CardGap();
void Hint(const char* txt);
void Mark(bool on);
void Stat(const char* value, const char* unit, const char* label);

void PageHome();
void PageEsp();
void PageBattle();
void PageActivity();
void PageUi();
void PageAbout();

}
