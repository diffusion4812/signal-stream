#include "window_console.h"

void draw_console(Console* &console, bool* consoleIsOpen) {
    ImGui::Begin("Console", consoleIsOpen);
    if (ImGui::Button("Clear Console")) {
        console->removeAll();
    }
    ImGui::SameLine();
    ImGui::Text("Log Count: %d", console->getCount());
    ImGui::Text("Console Output:");

    // Display each log entry with context menu
    for (int i = 0; i < console->getCount(); ++i) {
        auto item = console->getItem(i);
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
}