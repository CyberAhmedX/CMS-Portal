#include "cms/console.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#define CMS_ISATTY(fd) _isatty(fd)
#else
#include <termios.h>
#include <unistd.h>
#define CMS_ISATTY(fd) isatty(fd)
#endif

namespace cms::console {

namespace {

// Navy, gold and red that stay readable on light and dark terminals.
// Nothing is colored when output isn't a terminal.
const char* const kBlue = "38;5;32";
const char* const kGold = "38;5;136";
const char* const kRed = "38;5;160";
const char* const kGreen = "38;5;28";
const char* const kDim = "38;5;244";
const char* const kActiveTab = "1;97;48;5;25";

bool colors() {
    static const bool enabled = CMS_ISATTY(1) != 0;
    return enabled;
}

std::string paint(const char* code, const std::string& text) {
    if (!colors()) return text;
    return std::string("\x1b[") + code + "m" + text + "\x1b[0m";
}

// Columns a string takes on screen: skips color codes and counts each
// UTF-8 character once.
std::size_t width(const std::string& text) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x1b) {
            while (i < text.size() && text[i] != 'm') ++i;
            continue;
        }
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string padRight(const std::string& text, std::size_t w) {
    std::size_t used = width(text);
    return used >= w ? text : text + std::string(w - used, ' ');
}

std::string padLeft(const std::string& text, std::size_t w) {
    std::size_t used = width(text);
    return used >= w ? text : std::string(w - used, ' ') + text;
}

std::string repeat(const std::string& piece, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += piece;
    return out;
}

std::string trim(const std::string& text) {
    std::size_t start = 0, end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(start, end - start);
}

std::string getLine() {
    std::string line;
    if (!std::getline(std::cin, line)) throw InputClosed();
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

void prompt(const std::string& label) { std::cout << "  " << paint(kGold, "›") << " " << label << std::flush; }

bool isNumber(const std::string& text) {
    if (text.empty() || text.size() > 9) return false;
    std::size_t start = text[0] == '-' ? 1 : 0;
    if (start == text.size()) return false;
    return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(start), text.end(),
                       [](unsigned char c) { return std::isdigit(c); });
}

// Turns terminal echo off for as long as the object lives.
class HiddenInput {
public:
    HiddenInput() {
#ifdef _WIN32
        handle_ = GetStdHandle(STD_INPUT_HANDLE);
        active_ = GetConsoleMode(handle_, &mode_) != 0;
        if (active_) SetConsoleMode(handle_, mode_ & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
#else
        active_ = isatty(0) && tcgetattr(0, &saved_) == 0;
        if (active_) {
            termios quiet = saved_;
            quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            tcsetattr(0, TCSAFLUSH, &quiet);
        }
#endif
    }
    ~HiddenInput() {
        if (!active_) return;
#ifdef _WIN32
        SetConsoleMode(handle_, mode_);
#else
        tcsetattr(0, TCSAFLUSH, &saved_);
#endif
        std::cout << "\n";
    }
    HiddenInput(const HiddenInput&) = delete;
    HiddenInput& operator=(const HiddenInput&) = delete;

private:
    bool active_ = false;
#ifdef _WIN32
    HANDLE handle_ = nullptr;
    DWORD mode_ = 0;
#else
    termios saved_{};
#endif
};

}  // namespace

void init() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

void clear() {
    if (colors()) std::cout << "\x1b[2J\x1b[H";
    std::cout << "\n";
}

void header(const std::string& who, const std::string& term) {
    const std::size_t inner = kWidth - 2;
    auto row = [&](const std::string& left, const std::string& right) {
        std::size_t gap = inner > width(left) + width(right) ? inner - width(left) - width(right) : 1;
        std::cout << "  " << paint(kBlue, "│") << left << std::string(gap, ' ') << right << paint(kBlue, "│") << "\n";
    };
    std::cout << "  " << paint(kBlue, "╭" + repeat("─", inner) + "╮") << "\n";
    row("  " + paint(kGold, "◆") + " " + paint("1;38;5;32", "CMS") + "  Campus Management System", dim(term) + "  ");
    row("    " + who, "");
    std::cout << "  " << paint(kBlue, "╰" + repeat("─", inner) + "╯") << "\n\n";
}

void tabs(const std::vector<Tab>& items, int active) {
    std::string line = " ";
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Tab& t = items[i];
        std::string key = std::to_string(t.key);
        // Each tab carries a space on both sides, so tabs sit two spaces
        // apart and the active one fills its room with color.
        if (t.key == active) {
            line += colors() ? paint(kActiveTab, " " + key + " " + t.label + " ") : "[" + key + " " + t.label + "]";
        } else {
            line += " " + dim(key) + " " + t.label + " ";
        }
    }
    std::cout << line << "\n\n";
}

int chooseTab(const std::vector<Tab>& items, const std::string& label) {
    std::cout << "\n";
    while (true) {
        prompt(label + ": ");
        std::string text = trim(getLine());
        if (isNumber(text)) {
            int key = std::stoi(text);
            for (const Tab& t : items) {
                if (t.key == key) return key;
            }
        }
        std::string keys;
        for (const Tab& t : items) keys += (keys.empty() ? "" : ", ") + std::to_string(t.key);
        fail("Type one of the tab numbers: " + keys + ".");
    }
}

void section(const std::string& title) {
    std::string upper;
    for (char c : title) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    std::cout << "  " << paint("1;38;5;32", upper) << "\n\n";
}

void hint(const std::string& text) { std::cout << "  " << dim(text) << "\n"; }
void ok(const std::string& text) { std::cout << "  " << paint(kGreen, text) << "\n"; }
void fail(const std::string& text) { std::cout << "  " << paint(kRed, text) << "\n"; }
void warn(const std::string& text) { std::cout << "  " << paint(kRed, text) << "\n"; }

std::string bold(const std::string& text) { return paint("1", text); }
std::string red(const std::string& text) { return paint(kRed, text); }
std::string blue(const std::string& text) { return paint(kBlue, text); }
std::string dim(const std::string& text) { return paint(kDim, text); }

std::string dots(const std::vector<bool>& present) {
    std::string out;
    for (bool p : present) out += p ? paint(kBlue, "●") : dim("○");
    return out;
}

void table(const std::vector<Column>& columns, const std::vector<std::vector<std::string>>& rows,
           const std::vector<std::string>& total) {
    auto line = [&](const std::vector<std::string>& cells, bool strong) {
        std::string out = "  ";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            std::string cell = i < cells.size() ? cells[i] : "";
            // Shorten plain text that doesn't fit (styled cells are short by design).
            if (width(cell) > columns[i].width && cell.find('\x1b') == std::string::npos && columns[i].width > 3) {
                std::string cut;
                for (std::size_t j = 0; j < cell.size() && width(cut) < columns[i].width - 3; ++j) cut += cell[j];
                cell = cut + "...";
            }
            if (strong) cell = bold(cell);
            if (i) out += "  ";
            out += columns[i].right ? padLeft(cell, columns[i].width) : padRight(cell, columns[i].width);
        }
        std::cout << out << "\n";
    };
    std::size_t full = 0;
    for (const Column& c : columns) full += c.width;
    full += 2 * (columns.size() - 1);

    std::vector<std::string> titles;
    for (const Column& c : columns) titles.push_back(dim(c.title));
    line(titles, false);
    std::cout << "  " << dim(repeat("─", full)) << "\n";
    for (const auto& row : rows) line(row, false);
    if (!total.empty()) {
        std::cout << "  " << dim(repeat("─", full)) << "\n";
        line(total, true);
    }
}

std::string readLine(const std::string& label) {
    prompt(label);
    return trim(getLine());
}

std::string readPassword(const std::string& label) {
    prompt(label);
    HiddenInput hidden;
    return getLine();
}

int readInt(const std::string& label, int min, int max) {
    while (true) {
        std::string text = readLine(label);
        if (isNumber(text)) {
            int value = std::stoi(text);
            if (value >= min && value <= max) return value;
        }
        fail("Enter a number from " + std::to_string(min) + " to " + std::to_string(max) + ".");
    }
}

bool confirm(const std::string& question) {
    while (true) {
        std::string text = readLine(question + " (y/n): ");
        if (text == "y" || text == "Y") return true;
        if (text == "n" || text == "N") return false;
        fail("Type y or n.");
    }
}

}  // namespace cms::console
