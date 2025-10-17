#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

namespace ImGui {
    class ImShape {
    public:
        static bool Circle(ImU32 color) {
            ImGuiWindow* window = GetCurrentWindow();
            if (window->SkipItems)
                return false;

            const float square_sz = GetFrameHeight();
            const ImVec2 pos = window->DC.CursorPos;
            const ImRect total_bb(pos, pos + ImVec2(square_sz, square_sz));
            ItemSize(total_bb, GetStyle().FramePadding.y);

            ImVec2 center = total_bb.GetCenter();
            center.x = IM_ROUND(center.x);
            center.y = IM_ROUND(center.y);
            const float radius = (square_sz - 0.0f) * 0.5f;

            const int num_segment = window->DrawList->_CalcCircleAutoSegmentCount(radius);
            window->DrawList->AddCircleFilled(center, radius, color, num_segment);

            return true;
        }
        
        static bool Square(ImU32 color, float rounding) {
            ImGuiWindow* window = GetCurrentWindow();
            if (window->SkipItems)
                return false;

            const float square_sz = GetFrameHeight();
            const ImVec2 pos = window->DC.CursorPos;
            const ImRect total_bb(pos, pos + ImVec2(square_sz, square_sz));
            ItemSize(total_bb, GetStyle().FramePadding.y);

            window->DrawList->AddRectFilled(total_bb.Min, total_bb.Max, color, rounding);

            return true;
        }
        
        static bool Triangle(ImU32 color) {
            ImGuiWindow* window = GetCurrentWindow();
            if (window->SkipItems)
                return false; 
            const float square_sz = GetFrameHeight();
            const ImVec2 pos = window->DC.CursorPos;
            const ImRect total_bb(pos, pos + ImVec2(square_sz, square_sz));
            ItemSize(total_bb, GetStyle().FramePadding.y);

#ifdef USE_GOLDEN_RATIO //TODO: not finished
            const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
            const float alpha = (phi - 1.0f) / (phi + 1.0f);
            float halfBase = 0.5f * 0.45f;

            ImVec2 center = total_bb.GetCenter();
            float apexY = center.y - (0.5f - alpha);
            float baseY = center.y + (0.5f + alpha);


            ImVec2 p1 = ImVec2(center.x, apexY);
            ImVec2 p2 = ImVec2(center.x - halfBase, baseY);
            ImVec2 p3 = ImVec2(center.x + halfBase, baseY);
#else
            ImVec2 p1 = ImVec2((total_bb.Min.x + total_bb.Max.x) * 0.5f, total_bb.Min.y);
            ImVec2 p2 = ImVec2(total_bb.Min.x, total_bb.Max.y);
            ImVec2 p3 = ImVec2(total_bb.Max.x, total_bb.Max.y);
#endif
            window->DrawList->AddTriangleFilled(p1, p2, p3, color);
            return true;
        }
    };
}