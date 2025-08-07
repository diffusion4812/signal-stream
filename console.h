#pragma once

#include <string>
#include <vector>

class Console {
    typedef struct {
        int prio;
        std::string text;
    } LogItem;

public:
    void console();
    void log(int prio, std::string text);
    int getCount() const;
    LogItem getItem(int index) const;
    void removeItem(int index);
    void removeAll();
    void getItemsAsString(std::string& out) const;

private:
    std::vector<LogItem> logItems;
};