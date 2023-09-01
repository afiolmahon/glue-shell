
#include "../lib/include/util.hpp"
#include <iostream>

#include <fmt/format.h>
#include <fmt/std.h>

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

/* Initialize new terminal i/o settings */
static struct termios old, new1;
void initTermios(int echo)
{
    tcgetattr(0, &old); /* grab old terminal i/o settings */
    new1 = old; /* make new settings same as old settings */
    new1.c_lflag &= ~ICANON; /* disable buffered i/o */
    new1.c_lflag &= echo ? ECHO : ~ECHO; /* set echo mode */
    tcsetattr(0, TCSANOW, &new1); /* use these new terminal i/o settings now */
}

/* Restore old terminal i/o settings */
void resetTermios(void)
{
    tcsetattr(0, TCSANOW, &old);
}

enum class C0ControlCodes {
    Bel = 0x07, // bell
    Bs = 0x08, // backspace
    Ht = 0x09, // tab
    Lf = 0x0A, // linefeed
    Ff = 0x0C, // formfeed
    CR = 0x0D, // carriage return
    Esc = 0x1B, // start escape sequence
};

int main(int argc, char** argv)
{
    char c;
    initTermios(0);

    auto& outStr = std::cout;
    auto& errStr = outStr;

    outStr << "REPL:" << std::endl;

    const auto error = [](auto output) {
        errStr << output;
        errStr.flush();
    };

    std::string lastLine{"<last>"};
    std::string line{};
    while (true) {
        if (::read(0, &c, 1) == -1) {
            crew::fatal("error");
        }
        // if (c >= 0 && c <= 127) { // ASCII range
        // } else {
        // }
        outStr << fmt::format("{:x}\n", c);
        outStr.flush();

        // render
        // write current prompt
        // newline
        // write next prompt
        // move cursor up to line 1
    }
}
