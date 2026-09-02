#pragma once

class EinkCanvas;

// Drawing primitives eAIS needs and eNMEA does not.
//
// Deliberately free functions in their own file rather than methods on
// EinkCanvas: that class is shared byte-for-byte with eNMEA, and adding a plot
// primitive there would either fork the shared file or leave dead code in a
// project that only ever draws rectangles and text.
void drawLine(EinkCanvas& canvas, int x0, int y0, int x1, int y1, bool black);
void drawCircle(EinkCanvas& canvas, int cx, int cy, int radius, bool black);
