// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>

#include "layout.hh"

static bool nearlyEqual(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
  // UiScale: 1.0 at the reference height, floored (not negative/zero) well
  // below it, and NOT capped above it.
  UiScale s{661.0f, 0.5f};
  assert(nearlyEqual(s.factor(661.0f), 1.0f));
  assert(nearlyEqual(s.factor(200.0f), 0.5f));   // floored
  assert(nearlyEqual(s.factor(1322.0f), 2.0f));  // uncapped, scales up
  assert(nearlyEqual(s.scale(44.0f, 661.0f), 44.0f));
  assert(nearlyEqual(s.scale(44.0f, 1322.0f), 88.0f));

  // cappedFactor: identical to factor() below the cap, pinned at the cap
  // above it, and the floor still applies underneath.
  assert(nearlyEqual(s.cappedFactor(661.0f, 1.6f), 1.0f));
  assert(nearlyEqual(s.cappedFactor(1322.0f, 1.6f), 1.6f));  // 2.0 capped
  assert(nearlyEqual(s.cappedFactor(200.0f, 1.6f), 0.5f));   // floor wins

  // clampScroll: content that fits can't scroll; taller content clamps to
  // (contentH - viewH); never negative.
  assert(nearlyEqual(clampScroll(50.0f, 300.0f, 600.0f), 0.0f));    // fits
  assert(nearlyEqual(clampScroll(-25.0f, 900.0f, 600.0f), 0.0f));   // above top
  assert(nearlyEqual(clampScroll(150.0f, 900.0f, 600.0f), 150.0f)); // in range
  assert(nearlyEqual(clampScroll(999.0f, 900.0f, 600.0f), 300.0f)); // past bottom

  // dockTop shrinks the container from the top and returns the strip.
  Rect container{0, 0, 800, 600};
  Rect top = dockTop(container, 80.0f);
  assert(nearlyEqual(top.x, 0) && nearlyEqual(top.y, 0));
  assert(nearlyEqual(top.w, 800) && nearlyEqual(top.h, 80));
  assert(nearlyEqual(container.y, 80) && nearlyEqual(container.h, 520));
  assert(nearlyEqual(container.x, 0) && nearlyEqual(container.w, 800));

  // dockBottom shrinks from the bottom.
  Rect container2{0, 0, 800, 600};
  Rect bottom = dockBottom(container2, 100.0f);
  assert(nearlyEqual(bottom.y, 500) && nearlyEqual(bottom.h, 100));
  assert(nearlyEqual(container2.h, 500));

  // dockLeft / dockRight shrink horizontally.
  Rect container3{0, 0, 800, 600};
  Rect left = dockLeft(container3, 200.0f);
  assert(nearlyEqual(left.w, 200) && nearlyEqual(container3.x, 200) && nearlyEqual(container3.w, 600));
  Rect container4{0, 0, 800, 600};
  Rect right = dockRight(container4, 150.0f);
  assert(nearlyEqual(right.x, 650) && nearlyEqual(right.w, 150) && nearlyEqual(container4.w, 650));

  // centerIn centers a w x h box within a container.
  Rect box = centerIn(Rect{0, 0, 800, 600}, 200, 100);
  assert(nearlyEqual(box.x, 300) && nearlyEqual(box.y, 250));
  assert(nearlyEqual(box.w, 200) && nearlyEqual(box.h, 100));

  // RowCursor chains x by w+gap each call.
  RowCursor row(10, 20, 8);
  Rect r1 = row.next(100, 40);
  Rect r2 = row.next(50, 40);
  assert(nearlyEqual(r1.x, 10) && nearlyEqual(r1.y, 20));
  assert(nearlyEqual(r2.x, 118));  // 10 + 100 + 8

  // ColumnCursor chains y by h+gap each call.
  ColumnCursor col(10, 20, 5);
  Rect c1 = col.next(200, 30);
  Rect c2 = col.next(200, 30);
  assert(nearlyEqual(c1.y, 20) && nearlyEqual(c2.y, 55));  // 20 + 30 + 5

  printf("layout_test: OK\n");
  return 0;
}
