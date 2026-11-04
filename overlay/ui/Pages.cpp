#include "Pages.h"
#include "State.h"
#include "Widgets.h"
#include "UiCore.h"
#include "saphire_colors.h"
#include "imgui_internal.h"

#include <cstdio>

namespace ui
{

static const char* kNames[] = {
    "\xe6\xa6\x82\xe8\xa7\x88",
    "ESP",
    "\xe6\x88\x98\xe6\x96\x97",
    "\xe7\x95\x8c\xe9\x9d\xa2",
    "\xe6\x97\xa5\xe5\xbf\x97",
    "\xe5\x85\xb3\xe4\xba\x8e",
};

static const char* kIcons[] = { "A", "B", "C", "D", "E", "F" };

const char* PageName(Page p)
{
    int i = (int)p;
    if (i < 0 || i >= (int)Page::Count) i = 0;
    return kNames[i];
}

const char* PageIcon(Page p)
{
    int i = (int)p;
    if (i < 0 || i >= (int)Page::Count) i = 0;
    return kIcons[i];
}

static bool Card(const char* title, const ImVec2& size)
{
    const bool r = ImGui::BeginChildPos((ImTextureID)NULL, title, size, false, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 14.0f));
    return r;
}

static void CardEnd()
{
    ImGui::PopStyleVar();
    ImGui::EndChildPos();
}

static void Hint(const char* txt)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
    ImGui::TextUnformatted(txt);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

static void Stat(const char* value, const char* unit, const char* label)
{
    ImGui::PushFont(ubu_1);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.00f, 1.00f, 1.0f));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (unit && unit[0])
    {
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::PushFont(ubu_preview);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.44f, 0.44f, 0.49f, 1.0f));
        ImGui::TextUnformatted(unit);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::PushFont(ubu_preview);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void PageHome()
{
    const Status& s = St();
    char buf[64];

    ImGui::BeginGroup();

    if (Card("\xe6\xa6\x82\xe8\xa7\x88", ImVec2(272.0f, 270.0f)))
    {
        snprintf(buf, sizeof buf, "%.0f", s.fps);
        Stat(buf, "FPS", "\xe5\xbd\x93\xe5\x89\x8d\xe5\xb8\xa7\xe7\x8e\x87");

        snprintf(buf, sizeof buf, "%d", s.monster_n);
        Stat(buf, "", "\xe5\x9c\xba\xe4\xb8\x8a\xe6\x80\xaa\xe7\x89\xa9");

        if (s.data_ready)
            ImGui::TextColored(ImVec4(0.74f, 0.00f, 1.00f, 1.0f), "\xe5\x86\x85\xe5\xad\x98\xe5\xb0\xb1\xe7\xbb\xaa");
        else
            ImGui::TextColored(ImVec4(0.94f, 0.42f, 0.42f, 1.0f), "\xe7\xad\x89\xe5\xbe\x85\xe5\x86\x85\xe5\xad\x98");
    }
    CardEnd();

    CardGap();

    if (Card("\xe7\xb3\xbb\xe7\xbb\x9f", ImVec2(272.0f, 210.0f)))
    {
        snprintf(buf, sizeof buf, "%d", s.avatar_n);
        KeyValue("\xe9\x98\x9f\xe4\xbc\x8d\xe8\xa7\x92\xe8\x89\xb2", buf);
        snprintf(buf, sizeof buf, "%d", s.class_count);
        KeyValue("\xe7\xb1\xbb\xe6\x80\xbb\xe6\x95\xb0", buf);
        KeyValue("\xe8\xa7\x86\xe8\xa7\x92\xe7\x9f\xa9\xe9\x98\xb5", s.cam_ok ? "\xe5\xb0\xb1\xe7\xbb\xaa" : "\xe6\x9c\xaa\xe5\xb0\xb1\xe7\xbb\xaa");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::BeginGroup();

    if (Card("\xe7\x83\xad\xe9\x94\xae", ImVec2(272.0f, 270.0f)))
    {
        KeyValue("INSERT", "\xe8\x8f\x9c\xe5\x8d\x95");
        KeyValue("F3", "ESP");
        KeyValue("F5", "\xe7\xa7\x92\xe6\x9d\x80");
        KeyValue("F6", "\xe6\x97\xa0\xe6\x95\x8c");
        Hint("\xe8\x8f\x9c\xe5\x8d\x95\xe5\xbc\x80\xe5\x90\xaf\xe6\x97\xb6\xe9\xbc\xa0\xe9\x94\xae\xe4\xb8\x8d\xe4\xbc\x9a\xe4\xbc\xa0\xe7\xbb\x99\xe6\xb8\xb8\xe6\x88\x8f");
    }
    CardEnd();

    CardGap();

    if (Card("\xe5\xbf\xab\xe6\x8d\xb7\xe5\xbc\x80\xe5\x85\xb3", ImVec2(272.0f, 210.0f)))
    {
        ToggleRow("ESP \xe6\x80\xbb\xe5\xbc\x80\xe5\x85\xb3", &Cfg().esp_on);
        ToggleRow("\xe6\x98\xbe\xe7\xa4\xba\xe9\x98\xb2\xe6\xa1\x86", &Cfg().esp_box);
        ToggleRow("\xe6\x98\xbe\xe7\xa4\xba\xe5\x90\x8d\xe7\xa7\xb0", &Cfg().esp_name);
    }
    CardEnd();

    ImGui::EndGroup();
}

void PageEsp()
{
    char buf[64];

    ImGui::BeginGroup();

    if (Card("\xe7\xbb\x98\xe5\x88\xb6", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("ESP \xe6\x80\xbb\xe5\xbc\x80\xe5\x85\xb3", &Cfg().esp_on);
        ToggleRow("\xe6\x98\xbe\xe7\xa4\xba\xe9\x98\xb2\xe6\xa1\x86", &Cfg().esp_box);
        ToggleRow("\xe6\x98\xbe\xe7\xa4\xba\xe5\x90\x8d\xe7\xa7\xb0", &Cfg().esp_name);
        ToggleRow("\xe6\x98\xbe\xe7\xa4\xba\xe8\xb7\x9d\xe7\xa6\xbb", &Cfg().esp_dist);
    }
    CardEnd();

    CardGap();

    if (Card("\xe6\x9c\xac\xe5\xb8\xa7", ImVec2(272.0f, 210.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().esp_drawn);
        KeyValue("\xe5\xb7\xb2\xe7\xbb\x98\xe5\x88\xb6", buf);
        snprintf(buf, sizeof buf, "%d", St().monster_n);
        KeyValue("\xe5\x9c\xba\xe4\xb8\x8a\xe6\x80\xaa\xe7\x89\xa9", buf);
        if (St().monster_n > 0)
            ImGui::ProgressBar((float)St().esp_drawn / (float)St().monster_n, ImVec2(-1.0f, 8.0f), "");
        else
            ImGui::ProgressBar(0.0f, ImVec2(-1.0f, 8.0f), "");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::BeginGroup();

    if (Card("\xe5\x87\xa0\xe4\xbd\x95", ImVec2(272.0f, 270.0f)))
    {
        ImGui::SliderFloat("\xe4\xba\xba\xe7\x89\xa9\xe9\xab\x98\xe5\xba\xa6", &Cfg().esp_h, 1.0f, 3.2f, "%.2f m");
        ImGui::SliderFloat("\xe6\x9c\x80\xe8\xbf\x9c\xe8\xb7\x9d\xe7\xa6\xbb", &Cfg().max_dist, 20.0f, 2000.0f, "%.0f m");
    }
    CardEnd();

    CardGap();

    if (Card("\xe9\xa2\x9c\xe8\x89\xb2", ImVec2(272.0f, 210.0f)))
    {
        ImGui::ColorEdit4("\xe6\x96\xb9\xe6\xa1\x86", Cfg().esp_col,
                          ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoSidePreview |
                          ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
    }
    CardEnd();

    ImGui::EndGroup();
}

void PageBattle()
{
    static bool ask_kill = false;
    char buf[64];

    ImGui::BeginGroup();

    if (Card("\xe6\x97\xa0\xe6\x95\x8c", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("\xe5\x90\xaf\xe7\x94\xa8\xe6\x97\xa0\xe6\x95\x8c", &Cfg().god);
        if (Cfg().god)
            ImGui::TextColored(ImVec4(0.74f, 0.00f, 1.00f, 1.0f), "\xe7\x94\x9f\xe6\x95\x88\xe4\xb8\xad");
        else
            ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.53f, 1.0f), "\xe6\x9c\xaa\xe5\x90\xaf\xe7\x94\xa8");
        Hint("\xe4\xbb\x85\xe6\x8b\xa6\xe6\x88\xaa\xe5\x8f\x97\xe5\x87\xbb\xe5\x85\xa5\xe5\x8f\xa3\xef\xbc\x8c\xe4\xb8\x8d\xe4\xbf\xae\xe6\x94\xb9\xe7\x94\x9f\xe5\x91\xbd\xe5\x80\xbc");
    }
    CardEnd();

    CardGap();

    if (Card("\xe8\xae\xa1\xe6\x95\xb0", ImVec2(272.0f, 210.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().kill_count);
        KeyValue("\xe7\xb4\xaf\xe8\xae\xa1\xe7\xa7\x92\xe6\x9d\x80", buf);
        snprintf(buf, sizeof buf, "%d", St().monster_n);
        KeyValue("\xe5\xbd\x93\xe5\x89\x8d\xe7\x9b\xae\xe6\xa0\x87", buf);
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::BeginGroup();

    if (Card("\xe6\x93\x8d\xe4\xbd\x9c", ImVec2(272.0f, 270.0f)))
    {
        if (ImGui::Button("\xe7\xa7\x92\xe6\x9d\x80\xe5\x85\xa8\xe9\x83\xa8", ImVec2(227.0f, 37.0f)))
            ask_kill = true;
        Hint("\xe6\x89\xa7\xe8\xa1\x8c\xe5\x89\x8d\xe4\xbc\x9a\xe4\xba\x8c\xe6\xac\xa1\xe7\xa1\xae\xe8\xae\xa4");
    }
    CardEnd();

    CardGap();

    if (Card("\xe7\x8a\xb6\xe6\x80\x81", ImVec2(272.0f, 210.0f)))
    {
        if (Cfg().god)
            ImGui::TextColored(ImVec4(0.74f, 0.00f, 1.00f, 1.0f), "\xe6\x97\xa0\xe6\x95\x8c\xef\xbc\x9a\xe5\xbc\x80");
        else
            ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.53f, 1.0f), "\xe6\x97\xa0\xe6\x95\x8c\xef\xbc\x9a\xe5\x85\xb3");
        if (St().data_ready)
            ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.66f, 1.0f), "\xe6\x95\xb0\xe6\x8d\xae\xef\xbc\x9a\xe5\xb0\xb1\xe7\xbb\xaa");
        else
            ImGui::TextColored(ImVec4(0.94f, 0.42f, 0.42f, 1.0f), "\xe6\x95\xb0\xe6\x8d\xae\xef\xbc\x9a\xe7\xad\x89\xe5\xbe\x85");
    }
    CardEnd();

    ImGui::EndGroup();

    DialogResult r = Dialog("kill_confirm", &ask_kill,
                            "\xe7\xa7\x92\xe6\x9d\x80\xe5\x85\xa8\xe9\x83\xa8",
                            "\xe5\xb0\x86\xe5\xaf\xb9\xe5\xbd\x93\xe5\x89\x8d\xe5\x9c\xba\xe4\xb8\x8a\xe6\x89\x80\xe6\x9c\x89\xe6\x80\xaa\xe7\x89\xa9\xe6\x89\xa7\xe8\xa1\x8c\xe5\x8d\xb3\xe6\xad\xbb\xef\xbc\x8c\xe6\x97\xa0\xe6\xb3\x95\xe6\x92\xa4\xe9\x94\x80",
                            "\xe6\x89\xa7\xe8\xa1\x8c", "\xe5\x8f\x96\xe6\xb6\x88", false);
    if (r.confirmed) Req().kill_all = true;
}

void PageUi()
{
    char buf[64];

    ImGui::BeginGroup();

    if (Card("\xe6\x98\xbe\xe7\xa4\xba", ImVec2(272.0f, 270.0f)))
    {
        ImGui::SliderFloat("\xe7\x95\x8c\xe9\x9d\xa2\xe7\xbc\xa9\xe6\x94\xbe", &Cfg().ui_scale, 0.75f, 1.60f, "%.2f x");
        Hint("\xe7\xbc\xa9\xe6\x94\xbe\xe5\x8f\x98\xe6\x9b\xb4\xe5\x90\x8e\xe4\xbc\x9a\xe9\x87\x8d\xe5\xbb\xba\xe5\xad\x97\xe4\xbd\x93");
    }
    CardEnd();

    CardGap();

    if (Card("\xe6\xb8\xb2\xe6\x9f\x93", ImVec2(272.0f, 210.0f)))
    {
        KeyValue("\xe5\x90\x8e\xe7\xab\xaf", "Direct3D 11");
        KeyValue("\xe5\xad\x97\xe4\xbd\x93", "zh-cn.ttf");
        snprintf(buf, sizeof buf, "%.2f x", UiScale());
        KeyValue("\xe7\xbc\xa9\xe6\x94\xbe", buf);
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::BeginGroup();

    if (Card("\xe6\x97\xa5\xe5\xbf\x97", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("\xe8\x87\xaa\xe5\x8a\xa8\xe6\xbb\x9a\xe5\x8a\xa8", &Cfg().log_auto);
        snprintf(buf, sizeof buf, "%d", LogCount());
        KeyValue("\xe5\xbd\x93\xe5\x89\x8d\xe6\x9d\xa1\xe6\x95\xb0", buf);
        if (ImGui::Button("\xe6\xb8\x85\xe7\xa9\xba\xe6\x97\xa5\xe5\xbf\x97", ImVec2(227.0f, 37.0f)))
            LogClear();
    }
    CardEnd();

    CardGap();

    if (Card("\xe8\x8f\x9c\xe5\x8d\x95", ImVec2(272.0f, 210.0f)))
    {
        KeyValue("\xe5\xbc\x80\xe5\x85\xb3", "INSERT");
        KeyValue("\xe5\x85\xb3\xe9\x97\xad", "\xe5\x8f\xb3\xe4\xb8\x8a\xe8\xa7\x92 X");
        Hint("\xe5\x85\xb3\xe9\x97\xad\xe5\x90\x8e\xe9\xbc\xa0\xe9\x94\xae\xe5\x8f\xaf\xe6\xad\xa3\xe5\xb8\xb8\xe6\x93\x8d\xe4\xbd\x9c\xe6\xb8\xb8\xe6\x88\x8f");
    }
    CardEnd();

    ImGui::EndGroup();
}

void PageLogs()
{
    if (Card("\xe6\x97\xa5\xe5\xbf\x97", ImVec2(554.0f, 486.0f)))
    {
        if (ImGui::Button("\xe6\xb8\x85\xe7\xa9\xba", ImVec2(90.0f, 30.0f)))
            LogClear();
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::Checkbox("\xe8\x87\xaa\xe5\x8a\xa8\xe6\xbb\x9a\xe5\x8a\xa8", &Cfg().log_auto);
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextColored(ImVec4(0.44f, 0.44f, 0.49f, 1.0f), "%d \xe6\x9d\xa1", LogCount());

        ImGui::BeginChild("##logbody", ImVec2(-1.0f, -1.0f), false, ImGuiWindowFlags_NoBackground);
        LogScope sc;
        if (sc.Count() == 0)
        {
            ImGui::TextColored(ImVec4(0.38f, 0.38f, 0.43f, 1.0f), "\xe6\x9a\x82\xe6\x97\xa0\xe6\x97\xa5\xe5\xbf\x97");
        }
        else
        {
            ImGuiListClipper clipper;
            clipper.Begin(sc.Count());
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const char* line = sc.At(i);
                    if (!line) continue;
                    ImVec4 col(0.62f, 0.62f, 0.66f, 1.0f);
                    if (line[0] == '[' && line[1] == '+') col = ImVec4(0.74f, 0.00f, 1.00f, 1.0f);
                    else if (line[0] == '[' && line[1] == '!') col = ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
                    else if (line[0] == '[' && line[1] == '-') col = ImVec4(0.44f, 0.44f, 0.49f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(line);
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndChild();
    }
    CardEnd();
}

void PageAbout()
{
    char buf[64];

    ImGui::BeginGroup();

    if (Card("\xe5\x85\xb3\xe4\xba\x8e", ImVec2(272.0f, 270.0f)))
    {
        KeyValue("\xe7\x89\x88\xe6\x9c\xac", AppVersion());
        KeyValue("\xe6\x9e\x84\xe5\xbb\xba", AppBuildTime());
        KeyValue("\xe5\x90\x8e\xe7\xab\xaf", "Direct3D 11");
        KeyValue("\xe5\xad\x97\xe4\xbd\x93", "zh-cn.ttf");
    }
    CardEnd();

    CardGap();

    if (Card("\xe8\xaf\xb4\xe6\x98\x8e", ImVec2(272.0f, 210.0f)))
    {
        Hint("\xe4\xbb\x85\xe4\xbe\x9b\xe6\x9c\xac\xe5\x9c\xb0\xe7\xa0\x94\xe7\xa9\xb6\xe4\xb8\x8e\xe5\xad\xa6\xe4\xb9\xa0\xe4\xbd\xbf\xe7\x94\xa8");
        Hint("\xe6\x89\x80\xe6\x9c\x89\xe6\x95\xb0\xe6\x8d\xae\xe5\x9d\x87\xe5\x9c\xa8\xe6\x9c\xac\xe6\x9c\xba\xe5\x86\x85\xe5\xad\x98\xe5\xa4\x84\xe7\x90\x86");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::BeginGroup();

    if (Card("\xe8\xbf\x90\xe8\xa1\x8c\xe6\x97\xb6", ImVec2(272.0f, 270.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().class_count);
        KeyValue("\xe7\xb1\xbb\xe6\x80\xbb\xe6\x95\xb0", buf);
        snprintf(buf, sizeof buf, "%.0f", St().fps);
        KeyValue("\xe5\xb8\xa7\xe7\x8e\x87", buf);
        snprintf(buf, sizeof buf, "%.2f x", UiScale());
        KeyValue("\xe7\xbc\xa9\xe6\x94\xbe", buf);
    }
    CardEnd();

    ImGui::EndGroup();
}

}
