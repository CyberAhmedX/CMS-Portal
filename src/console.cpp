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

constexpr std::size_t kWidth = 60;

bool colors() {
    static const bool enabled = CMS_ISATTY(1) != 0;
    return enabled;
}

void colored(const char* code, const std::string& text) {
    if (colors()) {
        std::cout << "\x1b[" << code << "m" << text << "\x1b[0m\n";
    } else {
        std::cout << text << "\n";
    }
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

void title(const std::string& text) {
    std::string spaced;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i) spaced += ' ';
        spaced += text[i];
    }
    std::size_t inner = kWidth - 2;
    std::size_t left = (inner - std::min(inner, spaced.size())) / 2;
    std::size_t right = inner - left - std::min(inner, spaced.size());

    std::cout << "  ╔" << repeat("═", inner) << "╗\n";
    std::cout << "  ║" << std::string(left, ' ');
    if (colors()) std::cout << "\x1b[1m";
    std::cout << spaced;
    if (colors()) std::cout << "\x1b[0m";
    std::cout << std::string(right, ' ') << "║\n";
    std::cout << "  ╚" << repeat("═", inner) << "╝\n\n";
}

void box(const std::vector<std::string>& lines) {
    std::size_t inner = kWidth - 4;
    std::cout << "  ┌" << repeat("─", kWidth - 2) << "┐\n";
    for (const std::string& line : lines) {
        std::string text = line.size() > inner ? line.substr(0, inner) : line;
        std::cout << "  │ " << text << std::string(inner - text.size(), ' ') << " │\n";
    }
    std::cout << "  └" << repeat("─", kWidth - 2) << "┘\n";
}

void heading(const std::string& text) {
    std::cout << "\n  ";
    colored("1", text);
    std::cout << "  " << repeat("─", text.size()) << "\n";
}

void success(const std::string& text) { colored("32", "  " + text); }
void error(const std::string& text) { colored("31", "  " + text); }
void info(const std::string& text) { colored("36", "  " + text); }

std::string readLine(const std::string& prompt) {
    std::cout << "  " << prompt << std::flush;
    return trim(getLine());
}

std::string readPassword(const std::string& prompt) {
    std::cout << "  " << prompt << std::flush;
    HiddenInput hidden;
    return getLine();
}

int readInt(const std::string& prompt, int min, int max) {
    while (true) {
        std::string text = readLine(prompt);
        bool digits = !text.empty() && text.size() <= 9 &&
                      std::all_of(text.begin() + (text[0] == '-' ? 1 : 0), text.end(),
                                  [](unsigned char c) { return std::isdigit(c); }) &&
                      text != "-";
        if (digits) {
            int value = std::stoi(text);
            if (value >= min && value <= max) return value;
        }
        error("Enter a number from " + std::to_string(min) + " to " + std::to_string(max) + ".");
    }
}

bool confirm(const std::string& prompt) {
    while (true) {
        std::string text = readLine(prompt + " (y/n): ");
        if (text == "y" || text == "Y") return true;
        if (text == "n" || text == "N") return false;
        error("Type y or n.");
    }
}

int menu(const std::vector<std::string>& options) {
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < options.size(); ++i) {
        lines.push_back(std::to_string(i + 1) + "  " + options[i]);
    }
    box(lines);
    std::cout << "\n";
    return readInt("Choose: ", 1, static_cast<int>(options.size()));
}

void pause() {
    std::cout << "\n";
    readLine("Press Enter to continue...");
}

}  // namespace cms::console
