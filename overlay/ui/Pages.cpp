#include "Pages.h"
#include "State.h"
#include "Widgets.h"
#include "UiCore.h"
#include "Glass.h"
#include "saphire_colors.h"
#include "imgui_internal.h"

#include "../ov_mcp.h"
#include "../TpBridge.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace ui
{

static const char* kNames[] = {
    "信息概览",
    "ESP",
    "战斗相关",
    "游戏活动",
    "Lua 控制台",
    "MCP 服务",
    "UI界面",
    "关于本项目",
};

const char* PageName(Page p)
{
    int i = (int)p;
    if (i < 0 || i >= (int)Page::Count) i = 0;
    return kNames[i];
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
    ImGui::PushStyleColor(ImGuiCol_Text, C(theme::TextLow));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
    ImGui::TextUnformatted(txt);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void Stat(const char* value, const char* unit, const char* label)
{
    ImGui::PushFont(ubu_1);
    ImGui::PushStyleColor(ImGuiCol_Text, C(theme::Accent));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (unit && unit[0])
    {
        ImGui::SameLine(0.0f, 6.0f * S());
        ImGui::PushFont(ubu_preview);
        ImGui::PushStyleColor(ImGuiCol_Text, C(theme::TextMid));
        ImGui::TextUnformatted(unit);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::PushFont(ubu_preview);
    ImGui::PushStyleColor(ImGuiCol_Text, C(theme::TextMid));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Mark(bool on)
{
    if (on)
        ImGui::TextColored(C(theme::Success), "生效中");
    else
        ImGui::TextColored(C(theme::TextLow), "未启用");
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
            ImGui::TextColored(C(theme::Success), "内存就绪");
        else
            ImGui::TextColored(C(theme::Warning), "内存等待");
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

    if (Card("绘制", ImVec2(272.0f, 350.0f)))
    {
        ToggleRow("ESP 总开关", &Cfg().esp_on);
        ToggleRow("绘制方框", &Cfg().esp_box);
        ToggleRow("显示名称", &Cfg().esp_name);
        ToggleRow("显示距离", &Cfg().esp_dist);
        ToggleRow("宝箱/采集物", &Cfg().gadget_on);
        ToggleRow("顶部射线", &Cfg().ray_on);
    }
    CardEnd();

    CardGap();

    if (Card("本帧状态", ImVec2(272.0f, 340.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().esp_drawn);
        KeyValue("已绘制", buf);
        snprintf(buf, sizeof buf, "%d", St().gadget_n);
        KeyValue("宝箱缓存", buf);
        float ratio = St().monster_n > 0
            ? (float)St().esp_drawn / (float)St().monster_n : 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        ImGui::ProgressBar(ratio, ImVec2(-1.0f, 6.0f * S()), "");
        static const char* fail_txt[] = { "正常", "无管理器", "读列表失败", "length 超上限" };
        snprintf(buf, sizeof buf, "%s (%d)",
                 fail_txt[St().gadget_fail < 4 ? St().gadget_fail : 3], St().gadget_fail);
        KeyValue("宝箱采集状态", buf);
        snprintf(buf, sizeof buf, "%d / %d / %d",
                 St().gadget_list_n, St().gadget_cap_n, St().gadget_read_n);
        KeyValue("宝箱 List/数组/读入", buf);
        snprintf(buf, sizeof buf, "%d / %d / %d / %d",
                 St().gadget_drop_inst, St().gadget_drop_cls,
                 St().gadget_drop_cfg, St().gadget_drop_coord);
        KeyValue("丢弃 实体/类/配置/坐标", buf);
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("几何", ImVec2(272.0f, 310.0f)))
    {
        ImGui::SliderFloat("名称字号", &Cfg().esp_font, 12.0f, 48.0f, "%.0f px");
        ImGui::SliderFloat("名称偏移", &Cfg().esp_off, 0.0f, 60.0f, "%.0f px");
        ImGui::SliderFloat("人物高度", &Cfg().esp_h, 1.0f, 3.2f, "%.2f m");
        ImGui::SliderFloat("最远距离", &Cfg().max_dist, 20.0f, 2000.0f, "%.0f m");
        ImGui::SliderFloat("射线线宽", &Cfg().ray_w, 0.5f, 12.0f, "%.1f px");
        ToggleRow("平滑跟随", &Cfg().esp_smooth);
    }
    CardEnd();

    CardGap();

    if (Card("颜色", ImVec2(272.0f, 280.0f)))
    {
        ImGui::ColorEdit4("方框",
                          Cfg().esp_col,
                          ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoSidePreview |
                          ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("宝箱",
                          Cfg().gadget_col,
                          ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoSidePreview |
                          ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
    }
    CardEnd();

    // 结束右列，传送卡独占整行（宽 546，适配 566 内容区，避免被右边缘截断）
    ImGui::EndGroup();
    ImGui::BeginGroup();

    // ============================ 传送 ============================
    // 只列出 ESP 已扫描到的实体并提供“传送”按钮。
    static int  tp_cat   = 0;                       // 0=宝箱/采集 1=采集物 3=怪物
    static char tp_filter[64] = "";                 // 名称过滤
    if (Card("传送", ImVec2(546.0f, 880.0f)))
    {
        const char* cats[] = { "宝箱/采集", "晶体采集物", "怪物" };
        const int   ids[]  = { 0, 1, 3 };
        for (int c = 0; c < 3; ++c) {
            if (c) ImGui::SameLine(0.0f, 4.0f * S());
            bool on = (tp_cat == ids[c]);
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, C(theme::Accent, 0.55f));
            if (ImGui::Button(cats[c], ImVec2((546.0f - 2 * 4.0f * S()) / 3.0f, 24.0f * S())))
                tp_cat = ids[c];
            if (on) ImGui::PopStyleColor();
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##tpfilter", "名称过滤（空=全部）", tp_filter, sizeof tp_filter);

        const char* tpm = ovtpui::tp_text();
        if (tpm && tpm[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, C(theme::Cyan));
            ImGui::TextWrapped("传送：%s", tpm);
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("传送：等待目标");
        }

        ovtpui::Item items[256];
        int n = 0;
        ovtpui::fetch(tp_cat, items, 256, n);

        // 按距离由近到远排序，序号与 ESP 画面标签共用 ovtpui::rank_of 的全局编号。
        std::sort(items, items + n, [](const ovtpui::Item& a, const ovtpui::Item& b) {
            const int ra = ovtpui::rank_of(a.p), rb = ovtpui::rank_of(b.p);
            if (ra > 0 && rb > 0 && ra != rb) return ra < rb;
            if (a.dist != b.dist) return a.dist < b.dist;
            return std::strcmp(a.label[0] ? a.label : a.name,
                               b.label[0] ? b.label : b.name) < 0;
        });

        const float tpListH = ImMax(160.0f * S(), ImGui::GetContentRegionAvail().y - 34.0f * S());
        ImGui::BeginChild("##tplist", ImVec2(0, tpListH), true);
        int shown = 0;
        for (int i = 0; i < n; ++i) {
            const ovtpui::Item& it = items[i];
            if (tp_filter[0]) {
                // 大小写不敏感子串匹配（name 优先，其次可读名）
                auto contains = [](const char* hay, const char* needle) {
                    if (!needle[0]) return true;
                    for (const char* h = hay; *h; ++h) {
                        const char* a = h; const char* b = needle;
                        while (*b && *a && std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b)) { ++a; ++b; }
                        if (!*b) return true;
                    }
                    return false;
                };
                if (!contains(it.name, tp_filter) && !contains(it.label, tp_filter)) continue;
            }
            if (++shown > 100) break;   // 列表上限
            const char* nm = it.label[0] ? it.label : (it.name[0] ? it.name : "(未命名)");
            const int rk = ovtpui::rank_of(it.p);
            char row[192];
            snprintf(row, sizeof row, "[%d] %s %.0fm", rk > 0 ? rk : shown, nm, it.dist);
            ImGui::PushID(i);
            const bool tp = ImGui::Button("传送", ImVec2(52.0f * S(), 20.0f * S()));
            ImGui::SameLine(0.0f, 8.0f * S());
            ImGui::TextUnformatted(row);
            ImGui::PopID();
            if (tp) ovtpui::request_teleport(it.x, it.y, it.z);
        }
        if (n == 0) ImGui::TextDisabled("（无数据：等 ESP 扫描出该类对象，或切换类别）");
        ImGui::EndChild();

        KeyValueF("缓存", "%d 个", n);
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
        Hint("屏蔽受击表现，并把最终伤害归零");
    }
    CardEnd();

    CardGap();

    if (Card("能量", ImVec2(272.0f, 210.0f)))
    {
        ToggleRow("无限能量", &Cfg().inf_sp);
        Mark(Cfg().inf_sp && St().inf_sp_applied);
        Hint("技能消耗置 0，能量判定恒真");
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
    }
    CardEnd();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 10.0f * S());
    ImGui::BeginGroup();

    if (Card("冷却", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("无冷却", &Cfg().no_cd);
        Mark(Cfg().no_cd && St().no_cd_applied);
        Hint("关闭角色冷却判定并清零 CD 显示");
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

    CardGap();

    if (Card("伤害倍率", ImVec2(272.0f, 270.0f)))
    {
        ToggleRow("启用伤害倍率", &Cfg().dmg_on);
        ImGui::SliderFloat("最终伤害倍率", &Cfg().dmg_mult, 1.0f, 1000.0f, "%.1f x",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(150.0f * S());
        ImGui::InputFloat("自定义倍率", &Cfg().dmg_mult, 0.0f, 0.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            if (!(Cfg().dmg_mult >= 1.0f)) Cfg().dmg_mult = 1.0f;
            if (Cfg().dmg_mult > 1000.0f) Cfg().dmg_mult = 1000.0f;
        }
        Mark(Cfg().dmg_on && St().dmg_applied);
        Hint("滑动条与输入框共用同一数值，范围 1.0-1000.0");
    }
    CardEnd();

    CardGap();

    if (Card("超高伤害", ImVec2(272.0f, 140.0f)))
    {
        ToggleRow("启用超高伤害", &Cfg().ohk);
        Mark(Cfg().ohk && St().ohk_applied);
        Hint("命中怪物时直接造成 int 最大值伤害，优先于伤害倍率");
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

    if (Card("空间材质", ImVec2(546.0f, 236.0f)))
    {
        ImGui::Combo("主题", &Cfg().theme, glass::kThemeNames, glass::ThemeCount);
        ImGui::SliderFloat("玻璃浓度", &Cfg().glass_alpha, 0.20f, 1.0f, "%.2f");
        ImGui::SliderFloat("背景强度", &Cfg().backdrop_alpha, 0.0f, 0.80f, "%.2f");
        ImGui::SliderInt("星点数量", &Cfg().star_count, 0, 200);
        if (ImGui::Button("恢复默认", ImVec2(110.0f * S(), 28.0f * S())))
        {
            Cfg().theme = 1;
            Cfg().glass_alpha = 1.0f;
            Cfg().backdrop_alpha = 0.42f;
            Cfg().star_count = 110;
        }
        ImGui::SameLine(0.0f, 8.0f * S());
        if (ImGui::Button("保存配置", ImVec2(110.0f * S(), 28.0f * S())))
            SaveConfig();
    }
    CardEnd();

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

    CardGap();

    if (Card("MCP 服务", ImVec2(272.0f, 260.0f)))
    {
        ToggleRow("启用 MCP", &Cfg().mcp_on);
        ImGui::InputInt("监听端口", &Cfg().mcp_port, 1, 100);
        if (Cfg().mcp_port < 1024) Cfg().mcp_port = 1024;
        if (Cfg().mcp_port > 65535) Cfg().mcp_port = 65535;

        const int st = ovmcp::State();
        const char* txt = "已停止";
        if (st == 1) txt = "启动中";
        else if (st == 2) txt = "运行中";
        else if (st == 3) txt = "停止中";
        KeyValue("状态", txt);
        if (st == 2)
        {
            snprintf(buf, sizeof buf, "http://127.0.0.1:%u/mcp", (unsigned)ovmcp::Port());
            KeyValue("地址", buf);
        }
        const std::string e = ovmcp::Error();
        if (!e.empty()) Hint(e.c_str());
        snprintf(buf, sizeof buf, "%d", ovmcp::ToolCount());
        KeyValue("工具数", buf);
        Hint("大块数据落盘到 mcp_file");
    }
    CardEnd();

    ImGui::EndGroup();
}

void PageAbout()
{
	ImGui::TextColored(C(theme::AccentSoft), "作者：Jin | Elysia");
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
		Hint("本项目初衷是为了减少无效的游戏体验与时间");
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
