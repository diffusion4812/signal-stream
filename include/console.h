#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

class Console {
    struct LogItem {
        int prio;
        int64_t timestamp;
        std::string text;
    };

public:
    void log(int prio, std::string text);
    void updateRecentlyAddedItems();
    size_t getCount() const;
    size_t getCountRecent() const;
    LogItem getItem(int index) const;
    LogItem getItemRecent(int index) const;
    void removeItem(int index);
    void removeAll();
    void getItemsAsString(std::string& out) const;

private:
    std::vector<LogItem> logItems;
    std::vector<LogItem> recentlyAddedItems;
};