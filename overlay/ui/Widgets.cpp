#include "Widgets.h"
#include "UiCore.h"
#include "saphire_colors.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cstring>

namespace ui
{

void TextDim(const char* txt)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(66.f / 255.f, 65.f / 255.f, 70.f / 255.f, 1.0f));
    ImGui::TextUnformatted(txt);
    ImGui::PopStyleColor();
}

void TextBright(const char* txt)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::TextUnformatted(txt);
    ImGui::PopStyleColor();
}

void GroupTitle(const char* txt)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(77.f / 255.f, 80.f / 255.f, 97.f / 255.f, 1.0f));
    ImGui::TextUnformatted(txt);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void KeyValue(const char* key, const char* value)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(66.f / 255.f, 65.f / 255.f, 70.f / 255.f, 1.0f));
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();

    const ImVec2 p = ImGui::GetCursorPos();
    const float  w = ImGui::CalcTextSize(value, NULL, true).x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - w - 20.0f * S(), p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.86f, 0.88f, 1.0f));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

void KeyValueF(const char* key, const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    KeyValue(key, buf);
}

void RightText(const char* txt)
{
    const ImVec2 p = ImGui::GetCursorPos();
    const float  w = ImGui::CalcTextSize(txt, NULL, true).x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - w - 20.0f * S(), p.y));
    ImGui::TextUnformatted(txt);
}

bool ToggleRow(const char* label, bool* v)
{
    return ImGui::Checkbox(label, v);
}

void CardGap()
{
    ImGui::Spacing();
    ImGui::Spacing();
}

DialogResult Dialog(const char* id, bool* open, const char* title, const char* body,
                    const char* ok, const char* cancel, bool danger)
{
    DialogResult r;
    r.open = (open && *open);
    if (!r.open) return r;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                                                 IM_COL32(0, 0, 0, 150));

    const ImVec2 sz(430.0f * S(), 190.0f * S());
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(sz, ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(21.f / 255.f, 21.f / 255.f, 26.f / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(39.f / 255.f, 38.f / 255.f, 45.f / 255.f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * S());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * S(), 20 * S()));

    char win[128];
    snprintf(win, sizeof win, "%s###%s", title, id);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin(win, NULL, flags))
    {
        ImGui::PushFont(ubu_0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.72f, 0.76f, 1.0f));
        ImGui::PushTextWrapPos(sz.x - 44.0f * S());
        ImGui::TextUnformatted(body);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        const float by = sz.y - (20.0f + 37.0f + 26.0f) * S();
        ImGui::SetCursorPos(ImVec2(22.0f * S(), by));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(28.f / 255.f, 30.f / 255.f, 35.f / 255.f, 1.0f));
        if (ImGui::Button(cancel, ImVec2(110.0f * S(), 37.0f * S())))
        {
            r.cancelled = true;
            *open = false;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 10.0f * S());

        const ImVec4 okc = danger ? ImVec4(0.62f, 0.16f, 0.20f, 1.0f)
                                  : c::accent_color;
        ImGui::PushStyleColor(ImGuiCol_Button, okc);
        if (ImGui::Button(ok, ImVec2(110.0f * S(), 37.0f * S())))
        {
            r.confirmed = true;
            *open = false;
        }
        ImGui::PopStyleColor();
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    return r;
}

}
