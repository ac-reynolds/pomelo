#include "application/application.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        std::puts("Usage: pomelo [input.json]");
        return EXIT_SUCCESS;
    }
    if (argc > 2 || (argc == 2 && (argv[1][0] == '-' || argv[1][0] == '\0'))) {
        std::fprintf(stderr, "Usage: pomelo [input.json]\n");
        return EXIT_FAILURE;
    }
    return application::run(argc == 2 ? std::string_view(argv[1]) : std::string_view{});
}
