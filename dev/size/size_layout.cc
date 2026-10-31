// Size probe: base layout only. Never executed by the size script.
#include "layout.hh"

int main(int argc, char**) {
    Rect row{0.0f, 0.0f, 320.0f, 48.0f};
    Rect docked = dockTop(row, 12.0f);
    return argc == 42 ? (int)docked.h : 0;
}
