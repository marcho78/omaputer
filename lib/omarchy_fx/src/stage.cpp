#include "stage.h"

#include <algorithm>
#include <climits>

namespace ofx {

Stage::Stage(const Logo& logo, int cols, int rows) : width(cols), height(rows) {
    center_col = std::max(cols / 2, 1) + ((cols % 2 == 1 && cols > 1) ? 1 : 0);
    center_row = std::max(rows / 2, 1) + ((rows % 2 == 1 && rows > 1) ? 1 : 0);
    if (logo.chars.empty()) return;

    // ttfx drops trailing empty lines, numbers lines bottom-up from 1, and centres the
    // text by its last column and top row.
    int first_line = INT_MAX, last_line = 0, last_col = 0;
    for (const LogoChar& c : logo.chars) {
        first_line = std::min<int>(first_line, c.row);
        last_line = std::max<int>(last_line, c.row);
        last_col = std::max(last_col, c.col + 1);
    }
    const int lines = last_line + 1;
    const int top_row = lines - first_line;
    const int dc = last_col != cols ? center_col - last_col / 2 : 0;
    const int dr = top_row != rows ? center_row - top_row / 2 : 0;

    for (const LogoChar& c : logo.chars) {  // parse_logo yields reading order, i.e. ttfx id order.
        const int col = c.col + 1 + dc;
        const int row = lines - c.row + dr;
        if (col < 1 || col > cols || row < 1 || row > rows) continue;  // Off the canvas: dropped.
        chars.push_back({static_cast<int16_t>(col), static_cast<int16_t>(row), c.symbol});
    }
    if (chars.empty()) return;

    text = {chars[0].col, chars[0].col, chars[0].row, chars[0].row};
    for (const StageChar& c : chars) {
        text.left = std::min<int>(text.left, c.col);
        text.right = std::max<int>(text.right, c.col);
        text.bottom = std::min<int>(text.bottom, c.row);
        text.top = std::max<int>(text.top, c.row);
    }
}

}  // namespace ofx
