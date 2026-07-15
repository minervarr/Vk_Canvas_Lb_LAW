#include "layout.hh"

Rect dockTop(Rect& container, float thickness) {
  Rect strip{container.x, container.y, container.w, thickness};
  container.y += thickness;
  container.h -= thickness;
  return strip;
}

Rect dockBottom(Rect& container, float thickness) {
  Rect strip{container.x, container.y + container.h - thickness, container.w, thickness};
  container.h -= thickness;
  return strip;
}

Rect dockLeft(Rect& container, float thickness) {
  Rect strip{container.x, container.y, thickness, container.h};
  container.x += thickness;
  container.w -= thickness;
  return strip;
}

Rect dockRight(Rect& container, float thickness) {
  Rect strip{container.x + container.w - thickness, container.y, thickness, container.h};
  container.w -= thickness;
  return strip;
}

Rect centerIn(const Rect& container, float w, float h) {
  return Rect{
      container.x + (container.w - w) * 0.5f,
      container.y + (container.h - h) * 0.5f,
      w, h};
}
