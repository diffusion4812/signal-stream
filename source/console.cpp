#include "console.h"

namespace signal_stream {

    void Console::log(int prio, std::string text) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        LogItem logItem;
        logItem.prio = prio;
        logItem.timestamp = static_cast<int64_t>(ms);
        logItem.text = text;

        logItems.push_back(logItem);
        recentlyAddedItems.push_back(logItem); // Also add to recently added items
    }

    void Console::updateRecentlyAddedItems() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        for (int i = 0; i < recentlyAddedItems.size(); ) {
            if (ms - recentlyAddedItems[i].timestamp > 5000) { // 5 seconds
                recentlyAddedItems.erase(recentlyAddedItems.begin() + i);
            }
            else {
                ++i;
            }
        }
    }

    size_t Console::getCount() const {
        return logItems.size();
    }

    size_t Console::getCountRecent() const {
        return recentlyAddedItems.size();
    }

    Console::LogItem Console::getItem(int index) const {
        return logItems[index];
    }

    Console::LogItem Console::getItemRecent(int index) const {
        return recentlyAddedItems[index];
    }

    void Console::removeItem(int index) {
        logItems.erase(logItems.begin() + index);
    }

    void Console::removeAll() {
        logItems.clear();
    }

    void Console::getItemsAsString(std::string& out) const {
        out.clear();
        for (const auto& item : logItems) {
            out += std::to_string(item.prio) + ": " + item.text + "\n";
        }
    }

} // namespace signal_stream