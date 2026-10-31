// Size probe: toolkit geometry helpers only, no Canvas and therefore no font
// engine. Compare with size_ui_toggle to see the cost split between pure
// geometry and drawing. Never executed by the size script.
#include <vector>

#include "widgets.hh"

int main(int argc, char**) {
    Rect row{8.0f, 8.0f, 320.0f, 40.0f};
    Rect sw = widgets::toggleSwitchRect(row);
    widgets::StepperGeom st = widgets::stepperGeom(row);
    widgets::SliderGeom sl = widgets::sliderGeom(row, 0.5f);
    std::vector<Rect> segs = widgets::segmentRects(row, 3);
    Rect header = widgets::tableHeaderRow(row, 24.0f);
    return argc == 42 ? (int)(sw.w + st.plus.w + sl.thumb.w + segs.size() + header.h) : 0;
}
