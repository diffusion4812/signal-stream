#include "window.h"

void Window::draw() {}

Window_Console::Window_Console(Console* console, bool* consoleIsOpen) {
    mConsole = console;
    mConsoleIsOpen = consoleIsOpen;
}

void Window_Console::draw() {
    ImGui::Begin("Console", mConsoleIsOpen);
    if (ImGui::Button("Clear Console")) {
        mConsole->removeAll();
    }
    ImGui::SameLine();
    ImGui::Text("Log Count: %d", mConsole->getCount());
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
}

Window_FPS::Window_FPS(double* fps) {
    mFPS = fps;
}

void Window_FPS::draw() {
    ImGui::Begin("FPS");
    ImGui::Text("%.2f", *mFPS);
    ImGui::End();
}

Window_Analysis::Window_Analysis(CSVFile* csvFile) {
    mCSVFile = csvFile;
}

void Window_Analysis::draw() {
    // --- ImPlot CSV Plotting ---
    if (mCSVFile->fileIsRead && mCSVFile->parsedCsv->GetColumnCount() > 1) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
        ImGui::Begin(mCSVFile->filePath.filename().string().c_str()); // TODO: Add close button
        if (ImGui::Button("Display Table")) {
            mCSVFile->csvTableWindowIsOpen = !mCSVFile->csvTableWindowIsOpen;
        }

        {
            ImGui::BeginChild("SignalSelection", ImVec2(ImGui::GetContentRegionAvail().x * 0.2f, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
            ImGui::BeginTable("SignalSelectionTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
            ImGui::TableSetupColumn("Signal Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("X-Axis", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Y-Axis", ImGuiTableColumnFlags_WidthFixed);
            for (size_t i = 0; i < mCSVFile->parsedCsv->GetColumnCount(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(mCSVFile->parsedCsv->GetColumnName(i).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Checkbox(std::string("##xaxis" + std::to_string(i)).c_str(), &mCSVFile->selectedAxis[0][i]);
                ImGui::TableSetColumnIndex(2);
                ImGui::Checkbox(std::string("##yaxis" + std::to_string(i)).c_str(), &mCSVFile->selectedAxis[1][i]);
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

            for (int i = 0; i < mCSVFile->selectedAxis[0].size(); ++i) {
                if (mCSVFile->selectedAxis[0][i]) {
                    xCol = i;
                }

                if (mCSVFile->selectedAxis[1][i]) {
                    yCol = i;
                }
            }

            size_t colCount = mCSVFile->parsedCsv->GetColumnCount();
            size_t rowCount = mCSVFile->parsedCsv->GetRowCount();
            size_t plotRows = (rowCount < 1000000) ? rowCount : 1000; // Limit for performance

            // Prepare data
            static std::vector<double> xData, yData;
            xData.resize(plotRows);
            yData.resize(plotRows);
            bool validData = true;
            for (size_t i = 0; i < plotRows; ++i) {
                try {
                    xData[i] = std::stod(mCSVFile->parsedCsv->GetCell<std::string>(xCol, i));
                    yData[i] = std::stod(mCSVFile->parsedCsv->GetCell<std::string>(yCol, i));
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
