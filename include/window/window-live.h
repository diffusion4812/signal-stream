#pragma once

#include "window.h"

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