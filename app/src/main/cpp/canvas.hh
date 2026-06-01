#pragma once
#include <cstdint>
#include <vector>
#include "font.hh"

// A Canvas holds a list of draw calls (text strings + positions) to render
// each frame.  Renderer reads it to build the GPU curve buffer.
struct TextDraw {
    const char* text;
    float       x, y;
    float       size;
};

class Canvas {
public:
    Canvas(uint32_t width, uint32_t height);

    void clear();
    void draw_text(const char* text, float x, float y, float size);

    const std::vector<TextDraw>& draws()  const { return draws_; }
    uint32_t                     width()  const { return width_; }
    uint32_t                     height() const { return height_; }

private:
    uint32_t              width_;
    uint32_t              height_;
    std::vector<TextDraw> draws_;
};
