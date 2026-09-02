#include "Draw.h"

#include "EinkCanvas.h"

// Bresenham. Integer-only, no floating point in the inner loop, and it handles
// every octant without special cases - which matters because target vectors
// point in arbitrary directions.
void drawLine(EinkCanvas& canvas, int x0, int y0, int x1, int y1, bool black) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    canvas.setPixel(x0, y0, black);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = err * 2;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}

// Midpoint circle, eight-way symmetric. Used for the plot's range rings, where
// a perfectly round ring matters less than a cheap one - this runs twice per
// redraw and never allocates.
void drawCircle(EinkCanvas& canvas, int cx, int cy, int radius, bool black) {
  if (radius <= 0) return;
  int x = radius;
  int y = 0;
  int err = 1 - radius;
  while (x >= y) {
    canvas.setPixel(cx + x, cy + y, black);
    canvas.setPixel(cx + y, cy + x, black);
    canvas.setPixel(cx - y, cy + x, black);
    canvas.setPixel(cx - x, cy + y, black);
    canvas.setPixel(cx - x, cy - y, black);
    canvas.setPixel(cx - y, cy - x, black);
    canvas.setPixel(cx + y, cy - x, black);
    canvas.setPixel(cx + x, cy - y, black);
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
}
