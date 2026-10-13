#include <exception>
#include <iostream>
#include <string>

#include "cms/app.h"
#include "cms/console.h"
#include "cms/portal.h"

namespace {

void usage() {
    std::cout << "Usage: cms [--data <folder>]\n\n"
                 "  --data <folder>  where accounts and records are kept (default: data)\n"
                 "  --help           show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    cms::Portal::Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--data" && i + 1 < argc) {
            options.dataDir = argv[++i];
            continue;
        }
        std::cerr << "Unknown option: " << arg << "\n\n";
        usage();
        return 2;
    }

    cms::console::init();
    try {
        cms::Portal portal(options);
        return cms::App(portal).run();
    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 1;
    }
}
