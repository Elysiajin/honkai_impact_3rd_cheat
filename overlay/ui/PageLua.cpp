#include "Pages.h"
#include "State.h"
#include "Widgets.h"
#include "UiCore.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace ui
{

static char g_code[4096] = "return _VERSION";
static char g_query[64] = "wave";

static void SetCode(const char* code)
{
    snprintf(g_code, sizeof g_code, "%s", code ? code : "");
}

static void SetSearchCode(const char* q)
{
    snprintf(g_query, sizeof g_query, "%s", q ? q : "");
    snprintf(g_code, sizeof g_code,
             "local q=\"%s\"\n"
             "local t={}\n"
             "for k,v in pairs(_G) do\n"
             "  if tostring(k):lower():find(q,1,true) then t[#t+1]=tostring(k)..\" : \"..type(v) end\n"
             "  if type(v)==\"table\" then\n"
             "    for k2,v2 in pairs(v) do\n"
             "      if tostring(k2):lower():find(q,1,true) then t[#t+1]=tostring(k)..\".\"..tostring(k2)..\" : \"..type(v2) end\n"
             "    end\n"
             "  end\n"
             "end\n"
             "table.sort(t)\n"
             "return table.concat(t,\"\\n\")\n",
             g_query);
}

void PageLua()
{
    const Status& s = St();
    char buf[160];

    ImGui::BeginGroup();

    ImGui::TextUnformatted("Lua 控制台");
    ImGui::Separator();

    snprintf(buf, sizeof buf, "%d / %d  已校验 %d",
             s.lua_state_n > 0 ? Cfg().lua_sel + 1 : 0, s.lua_state_n, s.lua_trusted_n);
    ImGui::Text("LuaState: %s   %p", buf, (void*)s.lua_state);
    ImGui::TextWrapped("%s", s.lua_scanning ? "扫描中..." : s.lua_msg);

    if (ImGui::Button("刷新状态", ImVec2(108.0f, 30.0f)))
    {
        Cfg().lua_sel = 0;
        Req().lua_scan = true;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("<", ImVec2(30.0f, 30.0f)))
    {
        if (Cfg().lua_sel > 0) --Cfg().lua_sel;
    }
    ImGui::SameLine(0.0f, 4.0f);
    const int limit = (s.lua_trusted_n > 0) ? s.lua_trusted_n : s.lua_state_n;
    if (ImGui::Button(">", ImVec2(30.0f, 30.0f)))
    {
        if (Cfg().lua_sel + 1 < limit) ++Cfg().lua_sel;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("测试", ImVec2(45.0f, 30.0f)))
    {
        SetCode("return _VERSION");
        Req().lua_code = g_code;
        Req().lua_exec = true;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("信任", ImVec2(45.0f, 30.0f)))
        Req().lua_trust = true;

    ImGui::InputTextMultiline("##lua_code", g_code, sizeof g_code,
                              ImVec2(-1.0f, 130.0f), ImGuiInputTextFlags_AllowTabInput);

    if (ImGui::Button("执行", ImVec2(120.0f, 32.0f)))
    {
        Req().lua_code = g_code;
        Req().lua_exec = true;
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (ImGui::Button("清空", ImVec2(70.0f, 32.0f)))
        SetCode("");

    ImGui::Separator();
    ImGui::TextUnformatted("输出:");
    if (s.lua_out[0])
        ImGui::TextWrapped("%s", s.lua_out);
    else
        ImGui::TextUnformatted("等待执行...");

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();

    ImGui::TextUnformatted("接口发现");
    ImGui::Separator();

    if (ImGui::Button("列出全局", ImVec2(108.0f, 30.0f)))
    {
        SetCode("local t={}\nfor k,v in pairs(_G) do t[#t+1]=tostring(k)..\" : \"..type(v) end\n"
                "table.sort(t)\nreturn table.concat(t,\"\\n\")");
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (ImGui::Button("列出函数", ImVec2(108.0f, 30.0f)))
    {
        SetCode("local t={}\nfor k,v in pairs(_G) do\n"
                "  if type(v)==\"function\" then t[#t+1]=tostring(k)\n"
                "  elseif type(v)==\"table\" then\n"
                "    for k2,v2 in pairs(v) do\n"
                "      if type(v2)==\"function\" then t[#t+1]=tostring(k)..\".\"..tostring(k2) end\n"
                "    end\n"
                "  end\n"
                "end\ntable.sort(t)\nreturn table.concat(t,\"\\n\")");
    }

    if (ImGui::Button("已加载模块", ImVec2(108.0f, 30.0f)))
    {
        SetCode("local t={}\nif package and package.loaded then\n"
                "  for k,v in pairs(package.loaded) do t[#t+1]=tostring(k) end\nend\n"
                "table.sort(t)\nreturn table.concat(t,\"\\n\")");
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (ImGui::Button("当前版本", ImVec2(108.0f, 30.0f)))
        SetCode("return _VERSION");

    ImGui::Spacing();
    ImGui::InputText("##lua_query", g_query, sizeof g_query);
    if (ImGui::Button("搜索名字", ImVec2(108.0f, 30.0f)))
        SetSearchCode(g_query);
    ImGui::SameLine(0.0f, 8.0f);
    if (ImGui::Button("wave", ImVec2(50.0f, 30.0f)))
        SetSearchCode("wave");
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("damage", ImVec2(56.0f, 30.0f)))
        SetSearchCode("damage");
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("win", ImVec2(38.0f, 30.0f)))
        SetSearchCode("win");

    if (ImGui::Button("hp", ImVec2(50.0f, 30.0f)))
        SetSearchCode("hp");
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("monster", ImVec2(64.0f, 30.0f)))
        SetSearchCode("monster");
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("activity", ImVec2(64.0f, 30.0f)))
        SetSearchCode("activity");

    ImGui::Separator();
    ImGui::TextWrapped("活动实体: LuaAIACmpt / LuaAttackACmpt / LuaBeAttackedACmpt");
    ImGui::TextWrapped("小游戏: LuaDraggableACmpt / LuaTouchableACmpt");
    ImGui::TextWrapped("卡牌: HonKaiTCGLuaDesignWrap");
    ImGui::TextWrapped("关卡: LevelDesignManager（继承 LuaManager）");

    ImGui::EndGroup();
}

}
