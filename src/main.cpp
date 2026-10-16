#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>

#include <lua2/version.hpp>

int main() {
    // Nothing may escape main: an uncaught exception calls std::terminate.
    // The handlers use C stdio because std::println itself can throw.
    try {
        std::println("{}", lua2::version());
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::fputs("lua2: ", stderr);
        std::fputs(e.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("lua2: unknown error\n", stderr);
    }
    return EXIT_FAILURE;
}
