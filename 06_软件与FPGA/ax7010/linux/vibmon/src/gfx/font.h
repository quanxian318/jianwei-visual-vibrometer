#pragma once

#include <cstdint>
#include <string>

#include "gfx/canvas.h"

namespace vm {

// Pre-rendered 4-bit anti-aliased glyphs (tools/gen_font.py, Noto Sans SC
// Medium/Bold, SIL OFL 1.1). Only characters that appear in the UI/report
// sources plus printable ASCII are included.
struct FontGlyph {
  uint32_t codepoint;
  uint8_t w, h;
  int8_t xoff, yoff;  // from pen position (x) and from the top of the line box (y)
  uint8_t advance;
  uint32_t offset;    // into the face bitmap, 2 pixels per byte (high nibble first)
};

struct FontFace {
  int pixelSize;
  int lineHeight;
  int ascent;
  bool bold;
  const FontGlyph* glyphs;
  int count;
  const uint8_t* bitmap;
};

// Nearest generated size of the requested weight.
const FontFace& fontFace(int pixelSize, bool bold = false);
int textWidth(const FontFace& f, const std::string& utf8);
// Draws with the top-left corner of the line box at (x, y); returns the end x.
int drawText(Canvas& c, const FontFace& f, int x, int y, const std::string& utf8, uint32_t color);
int drawTextCentered(Canvas& c, const FontFace& f, int cx, int y, const std::string& utf8, uint32_t color);
int drawTextRight(Canvas& c, const FontFace& f, int right, int y, const std::string& utf8, uint32_t color);
// Shortens the text with an ellipsis so that it fits into maxWidth pixels.
std::string fitText(const FontFace& f, const std::string& utf8, int maxWidth);
bool hasGlyph(const FontFace& f, uint32_t codepoint);
uint32_t nextCodepoint(const std::string& s, size_t& i);

}  // namespace vm
