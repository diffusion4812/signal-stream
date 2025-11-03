#pragma once

#include "window.h"
#include "console.h"

class Window_Console : public WindowCRTP<Window_Console> {
public:
    Window_Console(Console* console, bool* consoleIsOpen) {
        mConsole = console;
        mConsoleIsOpen = consoleIsOpen;
        slideTimer = 0.0f;            // current elapsed time for the slide
        slideDuration = 0.5f;         // seconds for full slide-in
        slidingIn = false;            // set true to start slide-in
        slidingOut = false;           // set true to start slide-out
        visible = false;              // whether the panel is visible (should remain after slide completes)
        isSliding = false;            // one-shot trigger to start sliding  
        StartSlideIn();
    }

    void StartSlideIn() {
        slidingIn = true;
        slidingOut = false;
        slideTimer = 0.0f;
    }

    void StartSlideOut() {
        slidingIn = false;
        slidingOut = true;
        slideTimer = 0.0f;
    }

    void draw_popup() {
        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime;
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 workPos = vp->WorkPos;     // top-left of usable area
        ImVec2 workSize = vp->WorkSize;   // size of usable area

        ImVec2 panelSize = ImVec2(400.0f, 100.0f);

        // Update slide timer when sliding
        if (slidingIn || slidingOut) {
            if (!isSliding) {
                slideTimer = 0.0f;
                isSliding = true; // start sliding
                visible = true; // Ensure visibility when starting to slide in
            }
            else {
                slideTimer += dt;
            }

            if (slideTimer >= slideDuration) {
                slideTimer = slideDuration;
                isSliding = false; // Movement complete
                if (slidingOut) {
                    visible = false; // Hide after sliding out
                }
                slidingIn = false;
                slidingOut = false;
            }
        }

        if (visible) { // Any kind of visibility
            // Compute normalized t in [0,1]
            float t = slideTimer / slideDuration;
            if (slidingOut) {
                // if sliding out, invert t to move from visible -> offscreen
                t = 1.0f - t;
            }

            // Easing: ease-out cubic (you can replace with any easing)
            auto easeOutCubic = [](float x) {
                return 1.0f - powf(1.0f - x, 3.0f);
                };
            float e = easeOutCubic(t);

            // Compute final position:
            // Start: entirely below work area (y = workPos.y + workSize.y)
            // End: anchored at bottom with margin (y = workPos.y + workSize.y - panelSize.y - margin)
            float margin = 10.0f;
            ImVec2 startPos = ImVec2(workPos.x + (workSize.x - panelSize.x) * 0.5f,
                workPos.y + workSize.y + 0.0f); // fully off-screen below
            ImVec2 endPos = ImVec2(workPos.x + (workSize.x - panelSize.x) * 0.5f,
                workPos.y + workSize.y - panelSize.y - margin);

            ImVec2 curPos;
            curPos.x = startPos.x + (endPos.x - startPos.x) * e;
            curPos.y = startPos.y + (endPos.y - startPos.y) * e;

            // Ensure window appears above other windows (optional)
            ImGui::SetNextWindowBgAlpha(0.95f); // slightly transparent background
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBringToFrontOnFocus;

            ImGui::SetNextWindowPos(curPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);

            ImGui::Begin("Recent Logs", (bool*)0, flags);
            for (int i = 0; i < mConsole->getCountRecent(); ++i) {
                auto item = mConsole->getItemRecent(i);
                ImGui::Text("%s", item.text.c_str());
            }
            ImGui::End();
        }

        mConsole->updateRecentlyAddedItems(); // Organise recently added items
        if (mConsole->getCountRecent() > 0) {
            if (!isSliding && !visible)
                StartSlideIn(); // One-shot start sliding trigger
        }
        else {
            if (!isSliding && visible)
                StartSlideOut(); // One-shot start sliding trigger
        }
    }

    void OnRender() {
        ImGui::Begin("Console", mConsoleIsOpen);
        if (ImGui::Button("Clear Console")) {
            mConsole->removeAll();
        }
        ImGui::SameLine();
        ImGui::Text("Log Count: %d", mConsole->getCount());
        if (ImGui::Button("Slide In")) {
            StartSlideIn();
        }
        ImGui::Text("Console Output:");

        // Display each log entry with context menu
        for (int i = 0; i < mConsole->getCount(); ++i) {
            auto item = mConsole->getItem(i);
            ImGui::PushID(i);
            if (ImGui::Selectable(item.text.c_str())) {
                // Optionally handle selection
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Copy")) {
                    ImGui::SetClipboardText(item.text.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::End();

        draw_popup();
    }
private:
    Console* mConsole;
    bool* mConsoleIsOpen;
    float slideTimer;     // current elapsed time for the slide
    float slideDuration;  // seconds for full slide-in
    bool slidingIn;       // set true to start slide-in
    bool slidingOut;      // set true to start slide-in
    bool visible;         // whether the panel is visible (should remain after slide completes)
    bool isSliding;       // one-shot trigger to start sliding
};