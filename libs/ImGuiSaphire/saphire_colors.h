#pragma once

#include "imgui.h"

namespace c
{
inline ImVec4 accent_color = ImColor(169, 112, 255); // 雾紫 #A970FF

inline ImVec4 tab_text_active = ImColor(242, 238, 255, 255);
inline ImVec4 tab_text_hov    = ImColor(212, 184, 255, 255);
inline ImVec4 tab_text        = ImColor(176, 171, 196, 255);

inline ImVec4 tab_active      = ImColor(36, 37, 64, 165);
inline ImVec4 tab_active_fill = ImColor(169, 112, 255, 255);
inline ImVec4 glow_active     = ImColor(169, 112, 255, 210);

inline ImVec4 text_active = ImColor(242, 238, 255, 255);
inline ImVec4 text_hov    = ImColor(212, 184, 255, 255);
inline ImVec4 text        = ImColor(176, 171, 196, 255);

// 卡片底色比内容底板(#191B34)略亮，形成层次；文字用中灰紫保证可读性。
inline ImVec4 child_background       = ImColor(36, 37, 64, 232);
inline ImVec4 border_child           = ImColor(59, 54, 82, 255);
inline ImVec4 child_gradient         = ImColor(120, 100, 175, 255);
inline ImVec4 border_child_default   = ImColor(45, 43, 75, 255);
inline ImVec4 child_name             = ImColor(176, 171, 196, 255);

inline ImVec4 checkbox_active   = ImColor(45, 43, 75, 255);
inline ImVec4 checkbox_inactive = ImColor(31, 32, 58, 255);

inline ImVec4 circle_inactive = ImColor(93, 88, 120, 255);
inline ImVec4 circle_active   = ImColor(accent_color);

inline ImVec4 slider_background      = ImColor(31, 32, 58, 255);
inline ImVec4 circle_push_gradient   = ImColor(59, 54, 82, 255);
inline ImVec4 circle_pop_gradient    = ImColor(72, 66, 100, 255);

inline ImVec4 input_push_gradient = ImColor(36, 37, 64, 255);
inline ImVec4 input_pop_gradient  = ImColor(25, 27, 46, 255);

inline ImVec4 scroll_background = ImColor(20, 22, 38, 255);
inline ImVec4 scroll_keep       = ImColor(59, 54, 82, 255);

inline ImVec4 combo_background = ImColor(36, 37, 64, 255);

inline ImVec4 button = ImColor(36, 37, 64, 255);

inline ImVec4 button_push_gradient_active    = ImColor(45, 43, 75, 255);
inline ImVec4 button_pop_gradient_active     = ImColor(36, 37, 64, 255);
inline ImVec4 button_push_gradient_hovered   = ImColor(45, 43, 75, 255);
inline ImVec4 button_pop_gradient_hovered    = ImColor(30, 31, 54, 255);
inline ImVec4 button_push_gradient           = ImColor(36, 37, 64, 255);
inline ImVec4 button_pop_gradient            = ImColor(25, 27, 46, 255);

inline ImVec4 keybind_background = ImColor(36, 37, 64, 255);
inline ImVec4 keybind_border     = ImColor(45, 43, 75, 255);

inline ImVec4 tab_push_gradient = ImColor(169, 112, 255, 255);
inline ImVec4 tab_pop_gradient  = ImColor(96, 62, 160, 255);
}
