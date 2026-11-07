#include <cstdio>
#include <cstring>

#include "Pages.h"
#include "State.h"
#include "Widgets.h"
#include "UiCore.h"
#include "saphire_colors.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace ui
{

struct ActDef
{
    const char* name;
    const char* tag;
};

static const ActDef kActs[] = {
    { "相遇*回忆玩具小屋", "TurnBattle" },
    { "畅游吼姆世界", "TiledGame" },
    { "航行轶事", "TiledGame" },
};

static const int kActCount = (int)(sizeof kActs / sizeof kActs[0]);

static void UnitRow(const Status::ActUnit& u, int idx)
{
    const bool enemy = (u.camp == 2);
    const ImVec4 col = enemy ? ImVec4(0.94f, 0.42f, 0.42f, 1.0f)
                             : (u.camp == 1 ? ImVec4(0.42f, 0.82f, 0.94f, 1.0f)
                                            : ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
    char line[96];
    char tag[32] = { 0 };
    char raw[16] = { 0 };
    if (u.tag > 0)
        snprintf(tag, sizeof tag, enemy ? "#%d" : "%d", u.tag);
    snprintf(raw, sizeof raw, " C%d", u.raw_camp);
    if (u.max_hp > 0)
        snprintf(line, sizeof line, "%d  %d / %d  %s%s", idx, u.hp, u.max_hp, tag, raw);
    else
        snprintf(line, sizeof line, "%d  --  %s%s", idx, tag, raw);

    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::Selectable(line, false, ImGuiSelectableFlags_Disabled, ImVec2(0.0f, 18.0f * S()));
    ImGui::PopStyleColor();

    if (u.max_hp > 0)
    {
        float r = (float)u.hp / (float)u.max_hp;
        if (r < 0.0f) r = 0.0f;
        if (r > 1.0f) r = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(r, ImVec2(-1.0f, 4.0f * S()), "");
        ImGui::PopStyleColor();
    }
}

void PageActivity()
{
    const Status& s = St();
    char buf[96];

    if (Cfg().act_pick < 0 || Cfg().act_pick >= kActCount) Cfg().act_pick = 0;

    ImGui::BeginGroup();

    if (Card("活动列表", ImVec2(272.0f, 270.0f)))
    {
        for (int i = 0; i < kActCount; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable(kActs[i].name, Cfg().act_pick == i, 0, ImVec2(0.0f, 26.0f * S())))
                Cfg().act_pick = i;
            ImGui::SameLine(0.0f, 8.0f * S());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.44f, 0.44f, 0.49f, 1.0f));
            ImGui::TextUnformatted(kActs[i].tag);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    CardEnd();

    CardGap();

    if (Card("本局", ImVec2(272.0f, 210.0f)))
    {
        KeyValue("机制", kActs[Cfg().act_pick].tag);
        KeyValue("实体",
                 s.act_ready ? "已连接" : "未连接");
        snprintf(buf, sizeof buf, "%d / %d", s.act_ally, s.act_enemy);
        KeyValue("我方 / 敌方", buf);
        snprintf(buf, sizeof buf, "%d ms", s.act_scan_ms);
        KeyValue("扫描耗时", buf);
        snprintf(buf, sizeof buf, "%d", s.act_killed);
        KeyValue("累计秒杀", buf);
        KeyValue("\xe6\x8c\x81\xe7\xbb\xad\xe7\xa7\x92\xe6\x9d\x80",
                 Cfg().act_kill ? "\xe5\xbc\x80" : "\xe5\x85\xb3");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("功能", ImVec2(272.0f, 310.0f)))
    {
        ToggleRow("活动模块", &Cfg().act_on);
        ToggleRow("我方无敌", &Cfg().act_god);
        ToggleRow("\xe6\x8c\x81\xe7\xbb\xad\xe7\xa7\x92\xe6\x9d\x80", &Cfg().act_kill);
        Mark(Cfg().act_kill && s.act_ready);

        ImGui::Spacing();
        if (ImGui::Button("\xe5\x88\xb7\xe6\x96\xb0\xe4\xbf\xa1\xe6\x81\xaf", ImVec2(108.0f * S(), 30.0f * S())))
            Req().act_refresh = true;
        ImGui::SameLine(0.0f, 8.0f * S());
        if (ImGui::Button("\xe9\x87\x8d\xe6\x96\xb0\xe6\x89\xab\xe6\x8f\x8f", ImVec2(108.0f * S(), 30.0f * S())))
        {
            Req().act_scan_again = true;
            Cfg().act_on = true;
        }

        ImGui::Spacing();
        if (ImGui::Button("秒杀全部", ImVec2(227.0f * S(), 33.0f * S())))
        {
            Req().act_kill_all = true;
            Cfg().act_kill = true;  // 点一次后持续处理后续波次
        }

        ImGui::Spacing();
        Hint(s.act_msg[0] ? s.act_msg : "等待活动战斗");
    }
    CardEnd();

    CardGap();

    if (Card("战斗单位", ImVec2(272.0f, 240.0f)))
    {
        if (s.act_n <= 0)
        {
            Hint("未读到实体");
        }
        else
        {
            ImGui::BeginChild("##act_list", ImVec2(0.0f, 178.0f * S()), false);
            for (int i = 0; i < s.act_n && i < 32; ++i)
            {
                ImGui::PushID(i);
                UnitRow(s.act_u[i], i);
                ImGui::PopID();
            }
            ImGui::EndChild();
            Hint("已标记的敌方会持续压血");
        }
    }
    CardEnd();

    ImGui::EndGroup();
}

}
