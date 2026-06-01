#include "canvas.hh"

Canvas::Canvas(uint32_t width, uint32_t height)
    : width_(width), height_(height) {}

void Canvas::clear() {
    draws_.clear();
}

void Canvas::draw_text(const char* text, float x, float y, float size) {
    draws_.push_back({ text, x, y, size });
}
