#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace signal_stream {

    class Console {
        typedef struct {
            int prio;
            int64_t timestamp;
            std::string text;
        } LogItem;

    public:
        void log(int prio, std::string text);
        void updateRecentlyAddedItems();
        int getCount() const;
        int getCountRecent() const;
        LogItem getItem(int index) const;
        LogItem getItemRecent(int index) const;
        void removeItem(int index);
        void removeAll();
        void getItemsAsString(std::string& out) const;

    private:
        std::vector<LogItem> logItems;
        std::vector<LogItem> recentlyAddedItems;
    };

} // namespace signal_stream