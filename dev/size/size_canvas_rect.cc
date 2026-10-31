// Size probe: Canvas primitive recording only. Never executed by the size
// script; referencing rect() is enough to pull canvas.o.
#include <vector>

#include "canvas.hh"

int main(int argc, char**) {
    std::vector<float> curves;
    Canvas canvas(curves, 640, 480, nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
    canvas.rect(8.0f, 8.0f, 64.0f, 32.0f, col::accent, 4.0f);
    return argc == 42 ? (int)curves.size() : 0;
}
