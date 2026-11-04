#pragma once

#include "imgui.h"
#include <cstdarg>
#include <cstddef>

namespace ui
{

void TextDim(const char* txt);
void TextBright(const char* txt);

void GroupTitle(const char* txt);

void KeyValue(const char* key, const char* value);
void KeyValueF(const char* key, const char* fmt, ...);

void  RightText(const char* txt);

bool ToggleRow(const char* label, bool* v);

struct DialogResult
{
    bool open      = false;
    bool confirmed = false;
    bool cancelled = false;
};

DialogResult Dialog(const char* id, bool* open, const char* title, const char* body,
                    const char* ok, const char* cancel, bool danger);

void CardGap();

}
