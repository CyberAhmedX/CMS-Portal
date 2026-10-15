#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace cms::console {

// Thrown when input ends (Ctrl+D, Ctrl+Z or a closed pipe), so the program
// can exit cleanly instead of looping forever on a dead input stream.
class InputClosed : public std::runtime_error {
public:
    InputClosed() : std::runtime_error("input closed") {}
};

constexpr std::size_t kWidth = 72;

void init();  // UTF-8 output and colors on Windows
void clear();

// The campus card at the top of every screen.
void header(const std::string& who, const std::string& term);

struct Tab {
    int key;
    std::string label;
};
// A row of numbered tabs; `active` is highlighted (0 for none).
void tabs(const std::vector<Tab>& items, int active);
// Asks for one of the tab keys and returns it.
int chooseTab(const std::vector<Tab>& items, const std::string& prompt = "Choose a tab");

void section(const std::string& title);
void hint(const std::string& text);
void ok(const std::string& text);
void fail(const std::string& text);
void warn(const std::string& text);

// Styled text to put inside table cells or lines.
std::string bold(const std::string& text);
std::string red(const std::string& text);
std::string blue(const std::string& text);
std::string dim(const std::string& text);
// Dots for attended (●) and missed (○) classes.
std::string dots(const std::vector<bool>& present);

struct Column {
    std::string title;
    std::size_t width;
    bool right = false;  // right-align (numbers)
};
// Rows may contain styled text; widths count what is visible.
void table(const std::vector<Column>& columns, const std::vector<std::vector<std::string>>& rows,
           const std::vector<std::string>& total = {});

std::string readLine(const std::string& label);
std::string readPassword(const std::string& label);  // not shown while typing
int readInt(const std::string& label, int min, int max);
bool confirm(const std::string& question);

}  // namespace cms::console
