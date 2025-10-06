#pragma once

#include "window.h"
#include "projectenvironment.h"

class Window_SignalBrowser : public WindowCRTP<Window_SignalBrowser>{
public:
    Window_SignalBrowser(ProjectManager* project) : pm_(project) {
    }

    void OnDraw() {
        if (ImGui::Begin("Signal Browser")) {
            for (const auto& source : pm_->GetProjectData().sources) {
                if (ImGui::TreeNode(source.name.c_str())) {
                    ImGui::Text("Type: %s", source.type.c_str());
                    ImGui::Separator();
                    ImGui::Text("Schema:");
                    for (const auto& field : source.schema.get_fields()) {
                        ImGui::BulletText("%s: %s", field.name.c_str(), kindToString(field.kind));
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::End();
        }
    }

private:
    ProjectManager* pm_;
};
