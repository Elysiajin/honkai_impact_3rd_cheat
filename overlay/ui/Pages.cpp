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
    "信息概览",
    "ESP",
    "战斗相关",
    "游戏活动",
    "UI界面",
    "关于本项目",
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

bool Card(const char* title, const ImVec2& size)
{
    // 卡片尺寸按界面缩放放大：调用方写的是 1.0x 下的基准尺寸，
    // 这里统一乘 S()，避免每处 Card 调用都重复写 * S()。
    const float s = S();
    const bool r = ImGui::BeginChildPos(nullptr, title, ImVec2(size.x * s, size.y * s), false, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 14.0f * s));
    return r;
}

void CardEnd()
{
    ImGui::PopStyleVar();
    ImGui::EndChildPos();
}

void Hint(const char* txt)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
    ImGui::TextUnformatted(txt);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void Stat(const char* value, const char* unit, const char* label)
{
    ImGui::PushFont(ubu_1);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.00f, 1.00f, 1.0f));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (unit && unit[0])
    {
        ImGui::SameLine(0.0f, 6.0f * S());
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

void Mark(bool on)
{
    if (on)
        ImGui::TextColored(ImVec4(0.74f, 0.00f, 1.00f, 1.0f), "生效中");
    else
        ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.53f, 1.0f), "未启用");
}

void PageHome()
{
    const Status& s = St();
    char buf[64];

    ImGui::BeginGroup();

    if (Card("概览", ImVec2(272.0f, 270.0f)))
    {
        snprintf(buf, sizeof buf, "%.0f", s.fps);
        Stat(buf, "FPS", "当前帧率");

        snprintf(buf, sizeof buf, "%d", s.monster_n);
        Stat(buf, "", "敌人数量");

        if (s.data_ready)
            ImGui::TextColored(ImVec4(0.74f, 0.00f, 1.00f, 1.0f), "内存就绪");
        else
            ImGui::TextColored(ImVec4(0.94f, 0.42f, 0.42f, 1.0f), "内存等待");
    }
    CardEnd();

    CardGap();

    if (Card("系统", ImVec2(272.0f, 210.0f)))
    {
        snprintf(buf, sizeof buf, "%d", s.avatar_n);
        KeyValue("队伍角色", buf);
        snprintf(buf, sizeof buf, "%d", s.class_count);
        KeyValue("已验证管理器", buf);
        KeyValue("视角矩阵", s.cam_ok ? "就绪" : "等待");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("快捷键", ImVec2(272.0f, 270.0f)))
    {
        KeyValue("INSERT", "显隐菜单");
        KeyValue("F3", "ESP");
        KeyValue("F5", "秒杀");
        KeyValue("F6", "无敌");
        KeyValue("F7", "无限能量");
        KeyValue("F8", "无冷却");
        KeyValue("F9", "百分百暴击");
        KeyValue("F10", "倍攻");
    }
    CardEnd();

    CardGap();

    if (Card("绘制开关", ImVec2(272.0f, 210.0f)))
    {
        ToggleRow("ESP 总开关", &Cfg().esp_on);
        ToggleRow("显示方框", &Cfg().esp_box);
        ToggleRow("显示名称", &Cfg().esp_name);
    }
    CardEnd();

    ImGui::EndGroup();
}

void PageEsp()
{
    char buf[64];

    ImGui::BeginGroup();

    if (Card("绘制", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("ESP 总开关", &Cfg().esp_on);
        ToggleRow("绘制方框", &Cfg().esp_box);
        ToggleRow("显示名称", &Cfg().esp_name);
        ToggleRow("显示距离", &Cfg().esp_dist);
        ToggleRow("队友框", &Cfg().esp_team);
    }
    CardEnd();

    CardGap();

    if (Card("本帧", ImVec2(272.0f, 210.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().esp_drawn);
        KeyValue("已绘制", buf);
        float ratio = St().monster_n > 0
            ? (float)St().esp_drawn / (float)St().monster_n : 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        ImGui::ProgressBar(ratio, ImVec2(-1.0f, 6.0f * S()), "");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("几何", ImVec2(272.0f, 270.0f)))
    {
        ImGui::SliderFloat("名称字号", &Cfg().esp_font, 12.0f, 48.0f, "%.0f px");
        ImGui::SliderFloat("名称偏移", &Cfg().esp_off, 0.0f, 60.0f, "%.0f px");
        ImGui::SliderFloat("人物高度", &Cfg().esp_h, 1.0f, 3.2f, "%.2f m");
        ImGui::SliderFloat("最远距离", &Cfg().max_dist, 20.0f, 2000.0f, "%.0f m");
        ToggleRow("平滑跟随", &Cfg().esp_smooth);
    }
    CardEnd();

    CardGap();

    if (Card("颜色", ImVec2(272.0f, 210.0f)))
    {
        ImGui::ColorEdit4("方框",
                          Cfg().esp_col,
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

    if (Card("无敌", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("启用无敌", &Cfg().god);
        Mark(Cfg().god && St().god_applied);
        Hint("仅拦截受击入口，不修改生命值");
    }
    CardEnd();

    CardGap();

    if (Card("能量", ImVec2(272.0f, 210.0f)))
    {
        ToggleRow("无限能量", &Cfg().inf_sp);
        Mark(Cfg().inf_sp && St().inf_sp_applied);
        Hint("放开技能与大招的能量判定");
    }
    CardEnd();

    CardGap();

    if (Card("暴击", ImVec2(272.0f, 130.0f)))
    {
        ToggleRow("百分百暴击", &Cfg().crit);
        Mark(Cfg().crit && St().crit_applied);
        Hint("跳过暴击随机判定，攻击必定暴击");
    }
    CardEnd();

    CardGap();

    if (Card("倍攻", ImVec2(272.0f, 190.0f)))
    {
        ToggleRow("启用倍攻", &Cfg().atk_on);
        ImGui::SliderFloat("攻击速度倍率", &Cfg().atk_mult, 1.0f, 10.0f, "%.1f x");
        Mark(Cfg().atk_on && St().atk_speed_applied);
        Hint("Animator speed override, off restores original");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("冷却", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("无冷却", &Cfg().no_cd);
        Mark(Cfg().no_cd && St().no_cd_applied);
        Hint("大招与武器技能不再进入冷却");
    }
    CardEnd();

    CardGap();

    if (Card("操作", ImVec2(272.0f, 210.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().kill_count);
        KeyValue("累计秒杀", buf);
        if (ImGui::Button("秒杀全部", ImVec2(227.0f * S(), 37.0f * S())))
            ask_kill = true;
    }
    CardEnd();

    ImGui::EndGroup();

    DialogResult r = Dialog("kill_confirm", &ask_kill,
                            "秒杀全部",
                            "将对当前场上所有怪物执行即死，无法撤销",
                            "执行", "取消", false);
    if (r.confirmed) Req().kill_all = true;
}

void PageUi()
{
    char buf[64];

    ImGui::BeginGroup();

    if (Card("显示", ImVec2(272.0f, 270.0f)))
    {
        ImGui::SliderFloat("界面缩放", &Cfg().ui_scale, 0.75f, 1.60f, "%.2f x");
        Hint("缩放变更后会重建字体");
    }
    CardEnd();

    CardGap();

    if (Card("渲染", ImVec2(272.0f, 210.0f)))
    {
        KeyValue("字体", "zh-cn.ttf");
        snprintf(buf, sizeof buf, "%.2f x", UiScale());
        KeyValue("缩放", buf);
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("框显示", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("框动画", &Cfg().esp_smooth);
        ToggleRow("网格", &Cfg().show_grid);
        ToggleRow("动画", &Cfg().anim_on);
        Hint("平滑与网格影响 ESP 跟随表现");
    }
    CardEnd();

    CardGap();

    if (Card("菜单", ImVec2(272.0f, 210.0f)))
    {
        KeyValue("开关", "INSERT");
        KeyValue("关闭", "左上角 X");
    }
    CardEnd();

    ImGui::EndGroup();
}

void PageAbout()
{
	ImGui::TextColored(ImVec4(0.74f, 0.00f, 1.00f, 1.0f), "作者：Jin | Elysia");
    char buf[64];

    ImGui::BeginGroup();

    if (Card("项目信息", ImVec2(272.0f, 270.0f)))
    {
        KeyValue("版本号", AppVersion());
        KeyValue("构建时间", AppBuildTime());
    }
    CardEnd();

    CardGap();

    if (Card("说明", ImVec2(272.0f, 210.0f)))
    {
		Hint("请在单机环境下进行游玩使用！");
        Hint("禁止以此破坏玩家竞争，联机等体验");
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("运行时信息", ImVec2(272.0f, 270.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().class_count);
        KeyValue("已捕捉的管理器", buf);
        snprintf(buf, sizeof buf, "%.0f", St().fps);
        KeyValue("游戏帧率", buf);
        snprintf(buf, sizeof buf, "%.2f x", UiScale());
        KeyValue("界面缩放比例", buf);
    }
    CardEnd();

    ImGui::EndGroup();
}

}
