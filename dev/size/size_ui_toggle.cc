// Size probe: a single toolkit widget. Never executed by the size script.
#include <vector>

#include "widgets.hh"

int main(int argc, char**) {
    std::vector<float> curves;
    Canvas canvas(curves, 640, 480, nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
    widgets::drawToggle(canvas, Rect{8.0f, 8.0f, 320.0f, 40.0f}, true, "Toggle");
    return argc == 42 ? (int)curves.size() : 0;
}
