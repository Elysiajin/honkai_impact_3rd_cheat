#pragma once

#include "imgui.h"

// 浅色玻璃主题下的自定义控件配色，取自源项目「雾透」材质。
// 控件本体由 Glass.h 绘制玻璃底板，这里只负责文字、轨道和选中态。
namespace c
{
inline ImVec4 accent_color = ImColor(60, 80, 99, 255); // 冷蓝灰 #3C5063

inline ImVec4 tab_text_active = ImColor(29, 37, 45, 255);
inline ImVec4 tab_text_hov    = ImColor(60, 78, 95, 255);
inline ImVec4 tab_text        = ImColor(111, 132, 148, 255);

inline ImVec4 tab_active      = ImColor(226, 237, 246, 190);
inline ImVec4 tab_active_fill = ImColor(117, 147, 169, 220);
inline ImVec4 glow_active     = ImColor(117, 147, 169, 150);

inline ImVec4 text_active = ImColor(29, 37, 45, 255);
inline ImVec4 text_hov    = ImColor(60, 78, 95, 255);
inline ImVec4 text        = ImColor(111, 132, 148, 255);

// 卡片：近白半透明磨砂，冷白描边；标题用深墨色。
inline ImVec4 child_background       = ImColor(249, 251, 253, 210);
inline ImVec4 border_child           = ImColor(255, 255, 255, 190);
inline ImVec4 child_gradient         = ImColor(170, 196, 214, 255);
inline ImVec4 border_child_default   = ImColor(255, 255, 255, 130);
inline ImVec4 child_name             = ImColor(60, 78, 95, 255);

inline ImVec4 checkbox_active   = ImColor(222, 235, 245, 245);
inline ImVec4 checkbox_inactive = ImColor(255, 255, 255, 145);

inline ImVec4 circle_inactive = ImColor(148, 168, 182, 255);
inline ImVec4 circle_active   = ImColor(accent_color);

inline ImVec4 slider_background      = ImColor(224, 235, 244, 235);
inline ImVec4 circle_push_gradient   = ImColor(255, 255, 255, 245);
inline ImVec4 circle_pop_gradient    = ImColor(205, 223, 236, 245);

inline ImVec4 input_push_gradient = ImColor(255, 255, 255, 220);
inline ImVec4 input_pop_gradient  = ImColor(230, 240, 248, 220);

inline ImVec4 scroll_background = ImColor(230, 240, 248, 120);
inline ImVec4 scroll_keep       = ImColor(117, 147, 169, 210);

inline ImVec4 combo_background = ImColor(248, 251, 254, 235);

inline ImVec4 button = ImColor(249, 252, 254, 210);

inline ImVec4 button_push_gradient_active    = ImColor(222, 235, 245, 245);
inline ImVec4 button_pop_gradient_active     = ImColor(205, 223, 236, 245);
inline ImVec4 button_push_gradient_hovered   = ImColor(255, 255, 255, 235);
inline ImVec4 button_pop_gradient_hovered    = ImColor(233, 242, 249, 235);
inline ImVec4 button_push_gradient           = ImColor(255, 255, 255, 220);
inline ImVec4 button_pop_gradient            = ImColor(230, 240, 248, 220);

inline ImVec4 keybind_background = ImColor(240, 247, 252, 230);
inline ImVec4 keybind_border     = ImColor(255, 255, 255, 190);

inline ImVec4 tab_push_gradient = ImColor(255, 255, 255, 235);
inline ImVec4 tab_pop_gradient  = ImColor(213, 229, 240, 235);
}
