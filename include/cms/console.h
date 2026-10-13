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

void init();  // UTF-8 output and colors on Windows
void clear();

void title(const std::string& text);
void box(const std::vector<std::string>& lines);
void heading(const std::string& text);

void success(const std::string& text);
void error(const std::string& text);
void info(const std::string& text);

std::string readLine(const std::string& prompt);
std::string readPassword(const std::string& prompt);  // not shown while typing
int readInt(const std::string& prompt, int min, int max);
bool confirm(const std::string& prompt);  // y or n

// Shows numbered options and returns the chosen number (1-based).
int menu(const std::vector<std::string>& options);

void pause();

}  // namespace cms::console
