#include "console.h"

void Console::log(int prio, std::string text) {
    LogItem logItem;
    logItem.prio = prio;
    logItem.text = text;

    logItems.push_back(logItem);
}

int Console::getCount() const {
    return logItems.size();
}

Console::LogItem Console::getItem(int index) const {
    return logItems[index];
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