#include <terminal/terminal.hpp>

#include <array>

#include <unistd.h>

#include <sys/ioctl.h>

namespace crew {

std::vector<std::string> toRows(const std::string content, int32_t width)
{
    std::vector<std::string> result;
    std::string next;
    int32_t rowWidth = 0;

    const auto pushRow = [&result, &next, &rowWidth]() {
        result.push_back(std::move(next));
        next = {};
        rowWidth = 0;
    };

    auto it = content.begin();
    for (; it != content.end(); ++it) {
        // wrap if we reach the window length
        if (rowWidth == width) {
            pushRow();
        }
        if (*it == '\t') {
            if (rowWidth + 4 >= width) {
                pushRow();
            }
            next.append("    "); // TODO: avoid translation to spaces once we couple tabwidth to that of the terminal
            rowWidth += 4;
        } else if (*it == '\n') {
            pushRow();
        } else {
            next.push_back(*it);
            rowWidth += 1;
        }
    }

    if (!next.empty()) {
        result.push_back(std::move(next));
    }
    return result;
}

int readKey()
{
    int nread{};
    char c{};
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    if (c == '\x1b') {
        std::array<char, 3> seq{};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '1':
                        return fmt::underlying(EditorKey::HomeKey);
                    case '3':
                        return fmt::underlying(EditorKey::DeleteKey);
                    case '4':
                        return fmt::underlying(EditorKey::EndKey);
                    case '5':
                        return fmt::underlying(EditorKey::PageUp);
                    case '6':
                        return fmt::underlying(EditorKey::PageDown);
                    case '7':
                        return fmt::underlying(EditorKey::HomeKey);
                    case '8':
                        return fmt::underlying(EditorKey::EndKey);
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    return fmt::underlying(EditorKey::ArrowUp);
                case 'B':
                    return fmt::underlying(EditorKey::ArrowDown);
                case 'C':
                    return fmt::underlying(EditorKey::ArrowRight);
                case 'D':
                    return fmt::underlying(EditorKey::ArrowLeft);
                case 'H':
                    return fmt::underlying(EditorKey::HomeKey);
                case 'F':
                    return fmt::underlying(EditorKey::EndKey);
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H':
                return fmt::underlying(EditorKey::HomeKey);
            case 'F':
                return fmt::underlying(EditorKey::EndKey);
            }
        }
        return '\x1b';
    }
    return c;
}

std::optional<Position> getCursorPos()
{
    std::array<char, 32> buf;
    uint32_t i{};

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return {};
    }

    while (i < buf.size() - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        ++i;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') {
        return {};
    }

    Position pos{};
    if (sscanf(&buf[2], "%d;%d", &pos.y, &pos.x) != 2) {
        return {};
    }
    return pos;
}

std::optional<Position> getWindowSize()
{
    struct winsize ws {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // fallback if ioctl basde lookup fails
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return {};
        }
        return getCursorPos();
    }
    return Position{ws.ws_col, ws.ws_row};
}
} // namespace crew
