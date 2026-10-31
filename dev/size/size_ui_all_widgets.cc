// Size probe: the whole toolkit. Compare with size_ui_toggle to see what the
// linker keeps for one widget versus all of them. Never executed by the size
// script.
#include <string>
#include <vector>

#include "widgets.hh"

int main(int argc, char**) {
    std::vector<float> curves;
    Canvas canvas(curves, 640, 480, nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
    Rect row{8.0f, 8.0f, 320.0f, 40.0f};
    widgets::drawToggle(canvas, row, true, "Toggle");
    widgets::drawRadioRow(canvas, row, true, false, "Radio");
    widgets::drawStepper(canvas, row, "Stepper", "3");
    widgets::drawSlider(canvas, row, 0.5f, "Slider", "50");
    const std::string_view options[] = {"A", "B"};
    widgets::drawSegmented(canvas, row, options, 2, 0);
    widgets::drawDropdownField(canvas, row, "Label", "Value");
    widgets::drawScrollList(canvas, row, {"one", "two"}, 0, 0.0f, 32.0f);
    widgets::drawSortableTable(canvas, row, {{"Col"}}, [](int, int) { return std::string("x"); },
                               1, 0, true, 0.0f, 32.0f);
    widgets::drawFitButton(canvas, row, "Button", col::accent, col::text);
    widgets::drawGroupHeader(canvas, row, "Group");
    return argc == 42 ? (int)curves.size() : 0;
}
