#pragma once

#include "csv.h"
#include <imgui.h>
#include "implot.h"
#include "console.h"
#include "buffer.h"

struct IWindow {
    virtual ~IWindow() = default;
    virtual void Draw() = 0;
};

template<typename Derived>
class WindowCRTP : public IWindow {
public:
    void Draw() {
        setFullscreen();
        static_cast<Derived*>(this)->OnDraw();
    }

private:
    void setFullscreen() {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (fullscreen_) {
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::SetNextWindowViewport(vp->ID);
        }
    }

    bool fullscreen_ = false;
};

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

    void OnDraw() {
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

class Window_FPS : public WindowCRTP<Window_FPS> {
public:
    Window_FPS(double* fps) {
        mFPS = fps;
    }
    void OnDraw() {
        ImGui::Begin("FPS");
        ImGui::Text("%.2f", *mFPS);
        ImGui::End();
    }
private:
    double* mFPS;
};

class Window_OpenProject : public WindowCRTP<Window_OpenProject> {
public:
    Window_OpenProject() {
    }

    void OnDraw() {
        if (ImGui::Begin("Project Name")) {

            ImGui::End();
        }
    }
};

class Window_Analysis : public WindowCRTP<Window_Analysis> {
public:
    Window_Analysis(CSVFile* csvFile) {
        CSVFile_ = csvFile;
    }

    void OnDraw() {
        // --- ImPlot CSV Plotting ---
        if (CSVFile_->fileIsRead && CSVFile_->parsedCsv->GetColumnCount() > 1) {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
            ImGui::Begin(CSVFile_->filePath.filename().string().c_str()); // TODO: Add close button
            if (ImGui::Button("Display Table")) {
                CSVFile_->csvTableWindowIsOpen = !CSVFile_->csvTableWindowIsOpen;
            }

            {
                ImGui::BeginChild("SignalSelection", ImVec2(ImGui::GetContentRegionAvail().x * 0.2f, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
                ImGui::BeginTable("SignalSelectionTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
                ImGui::TableSetupColumn("Signal Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("X-Axis", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Y-Axis", ImGuiTableColumnFlags_WidthFixed);
                for (size_t i = 0; i < CSVFile_->parsedCsv->GetColumnCount(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(CSVFile_->parsedCsv->GetColumnName(i).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Checkbox(std::string("##xaxis" + std::to_string(i)).c_str(), &CSVFile_->selectedAxis[0][i]);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Checkbox(std::string("##yaxis" + std::to_string(i)).c_str(), &CSVFile_->selectedAxis[1][i]);
                }
                ImGui::TableNextRow();
                ImGui::EndTable();
                ImGui::EndChild();
            }

            ImGui::SameLine();

            {
                static int xCol = 0; // Default to first column for X-axis
                static int yCol = 1; // Default to second column for Y-axis

                ImGui::BeginChild("CSVPlotting", ImVec2(0, 0), ImGuiChildFlags_Borders);

                for (int i = 0; i < CSVFile_->selectedAxis[0].size(); ++i) {
                    if (CSVFile_->selectedAxis[0][i]) {
                        xCol = i;
                    }

                    if (CSVFile_->selectedAxis[1][i]) {
                        yCol = i;
                    }
                }

                size_t colCount = CSVFile_->parsedCsv->GetColumnCount();
                size_t rowCount = CSVFile_->parsedCsv->GetRowCount();
                size_t plotRows = (rowCount < 1000000) ? rowCount : 1000; // Limit for performance

                // Prepare data
                static std::vector<double> xData, yData;
                xData.resize(plotRows);
                yData.resize(plotRows);
                bool validData = true;
                for (size_t i = 0; i < plotRows; ++i) {
                    try {
                        xData[i] = std::stod(CSVFile_->parsedCsv->GetCell<std::string>(xCol, i));
                        yData[i] = std::stod(CSVFile_->parsedCsv->GetCell<std::string>(yCol, i));
                    }
                    catch (...) {
                        validData = false;
                        break;
                    }
                }

                ImPlot::MapInputReverse();

                if (validData) {
                    if (ImPlot::BeginPlot("CSV Data Plot", ImVec2(-1.0, -1.0))) {
                        ImPlot::PlotLine("Data", xData.data(), yData.data(), (int)plotRows);
                        ImPlot::EndPlot();
                    }
                }
                else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Non-numeric data in selected columns.");
                }
                ImGui::EndChild();
            }

            ImGui::End();
        }

    }
private:
    CSVFile* CSVFile_;
};

class Window_Live : public WindowCRTP<Window_Live> {
public:
    explicit Window_Live(SPSC_CircularBuffer<std::byte*>* buffer)
        : buffer_(buffer) {
        assert(buffer != nullptr);
    }

    void OnDraw() {
        ImGui::Begin("Live Window");
        ImGui::Text("%d", buffer_->capacity());
        if (ImPlot::BeginPlot("Live Data", ImVec2(-1, -1))) {
            ImPlot::PlotLineG("line", DataGetter, buffer_, buffer_->size());
            ImPlot::EndPlot();
        }
        ImGui::End();
    }
private:
    static ImPlotPoint DataGetter(int idx, void* user_data) {
        auto* buffer_ = static_cast<SPSC_CircularBuffer<std::byte*>*>(user_data);
        // TODO: Plot data
        /*if (buffer_->tail_ > buffer_->head_) {
            BufferItem item;
            item = buffer_->buf_[idx + buffer_->head_];
            return ImPlotPoint(item.ms, item.data);
        }*/
        return ImPlotPoint(0, 0);
    }

    SPSC_CircularBuffer<std::byte*>* buffer_;
};