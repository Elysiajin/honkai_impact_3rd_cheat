#include "Pages.h"
#include "State.h"
#include "Widgets.h"
#include "UiCore.h"
#include "Glass.h"
#include "saphire_colors.h"
#include "imgui_internal.h"

#include "../ov_mcp.h"
#include "../NavBridge.h"

#include <cstdio>
#include <cstring>

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

static const char* kIcons[] = { "A", "B", "C", "D", "G", "H", "E", "F" };

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
        ToggleRow("队友框", &Cfg().esp_team);
        ToggleRow("宝箱/采集物", &Cfg().gadget_on);
        ToggleRow("绘制NPC", &Cfg().npc_on);
        ToggleRow("顶部射线", &Cfg().ray_on);
    }
    CardEnd();

    CardGap();

    if (Card("本帧状态", ImVec2(272.0f, 240.0f)))
    {
        snprintf(buf, sizeof buf, "%d", St().esp_drawn);
        KeyValue("已绘制", buf);
        snprintf(buf, sizeof buf, "%d / %d", St().gadget_n, St().npc_n);
        KeyValue("宝箱 / NPC 缓存", buf);
        float ratio = St().monster_n > 0
            ? (float)St().esp_drawn / (float)St().monster_n : 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        ImGui::ProgressBar(ratio, ImVec2(-1.0f, 6.0f * S()), "");
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
        ImGui::ColorEdit4("NPC",
                          Cfg().npc_col,
                          ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoSidePreview |
                          ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
    }
    CardEnd();

    // 结束右列，寻路卡独占整行（宽 546，适配 566 内容区，避免被右边缘截断）
    ImGui::EndGroup();
    ImGui::BeginGroup();

    // ============================ 寻路 ============================
    // 列出 ESP 扫描到的实体（渲染线程 g_nv 直接读），每行"寻路"按钮
    // → ovnavui::request_to(x,y,z) → 游戏主线程 nav_tick 计算 NavMesh 路径。
    static int  nav_cat   = 0;                       // 0=宝箱/采集 1=采集物 2=NPC 3=怪物
    static char nav_filter[64] = "";                 // 名称过滤
    if (Card("寻路", ImVec2(546.0f, 560.0f)))
    {
        const char* cats[] = { "宝箱/采集", "晶体采集物", "NPC", "怪物" };
        for (int c = 0; c < 4; ++c) {
            if (c) ImGui::SameLine(0.0f, 4.0f * S());
            bool on = (nav_cat == c);
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, C(theme::Accent, 0.55f));
            if (ImGui::Button(cats[c], ImVec2((546.0f - 3 * 4.0f * S()) / 4.0f, 24.0f * S())))
                nav_cat = c;
            if (on) ImGui::PopStyleColor();
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##navfilter", "名称过滤（空=全部）", nav_filter, sizeof nav_filter);

        // ---- 状态区（放在列表上方，保证一定看得见）----
        {
            const char* msg = ovnavui::last_msg();
            const bool ok = ovnavui::last_ok();
            ImGui::PushStyleColor(ImGuiCol_Text, ok ? C(theme::Success) : C(theme::Danger));
            ImGui::TextWrapped("结果：%s", msg && msg[0] ? msg : "尚未寻路");
            ImGui::PopStyleColor();

            const char* tpm = ovnavui::tp_text();
            if (tpm && tpm[0]) {
                ImGui::PushStyleColor(ImGuiCol_Text, C(theme::Cyan));
                ImGui::Text("传送：%s", tpm);
                ImGui::PopStyleColor();
            }

            char dbg[256];
            ovnavui::debug_info(dbg, (int)sizeof dbg);
            ImGui::PushStyleColor(ImGuiCol_Text, C(theme::TextMid));
            ImGui::TextWrapped("链路：%s", dbg);
            ImGui::PopStyleColor();

            const char* dg = ovnavui::diag_text();
            if (dg && dg[0]) {
                ImGui::PushStyleColor(ImGuiCol_Text, C(theme::Warning));
                ImGui::TextWrapped("自检：%s", dg);
                ImGui::PopStyleColor();
            }

            if (ImGui::Button("自检导航网格", ImVec2(140.0f * S(), 24.0f * S())))
                ovnavui::diag();
            ImGui::SameLine(0.0f, 8.0f * S());
            ImGui::TextDisabled("（点一次：报告能否取到当前地图 NavMesh）");
        }

        ovnavui::Item items[256];
        int n = 0;
        ovnavui::fetch(nav_cat, items, 256, n);

        ImGui::BeginChild("##navlist", ImVec2(0, 300.0f * S()), true);
        int shown = 0;
        for (int i = 0; i < n; ++i) {
            const ovnavui::Item& it = items[i];
            if (nav_filter[0]) {
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
                if (!contains(it.name, nav_filter) && !contains(it.label, nav_filter)) continue;
            }
            if (++shown > 100) break;   // 列表上限
            const char* nm = it.label[0] ? it.label : (it.name[0] ? it.name : "(未命名)");
            char row[192];
            snprintf(row, sizeof row, "%s  (%.0fm)", nm, it.dist);
            ImGui::PushID(i);
            const bool pressed = ImGui::Button("寻路", ImVec2(46.0f * S(), 20.0f * S()));
            ImGui::SameLine(0.0f, 4.0f * S());
            const bool tp = ImGui::Button("传送", ImVec2(46.0f * S(), 20.0f * S()));
            ImGui::SameLine(0.0f, 8.0f * S());
            ImGui::TextUnformatted(row);
            ImGui::PopID();
            if (pressed) ovnavui::request_to(it.x, it.y, it.z);
            if (tp) ovnavui::request_teleport(it.x, it.y, it.z);
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
