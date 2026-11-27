#pragma once
#include <cstdint>
#include <vector>

// Procedural symbols drawn as strokes: lines, arcs and dots.
namespace glyphs
{
enum Style
{
	FUTHARK,
	BINDRUNE,
	RUNE_CIRCLE,
	STAVE,
	OGHAM,
	CIRCUIT,
	CHIP,
	SIGIL,
	CONSTELLATION,
	MAZE,
	HUD,
	STYLE_COUNT,
	MIX = STYLE_COUNT// a random style per glyph
};
extern const char* const STYLE_NAMES[ STYLE_COUNT + 1 ];

enum PrimType
{
	LINE,// a to b
	ARC, // centre a, radius, from angle bx to angle by (radians, counter-clockwise when by > bx)
	DOT  // filled disc: centre a, radius
};

struct Prim
{
	float ax, ay, bx, by;
	float radius;
	PrimType type;
	// Where the stroke starts and ends along the whole glyph, 0 to 1, in drawing order.
	float alongStart = 0.0f;
	float alongEnd   = 0.0f;
};

// A shape for the glyph to follow: its signed distance to the outline (positive inside), sampled on
// a grid, in glyph units.
struct Field
{
	int cols   = 0;
	int rows   = 0;
	float x0   = 0.0f;// where value 0 is
	float y0   = 0.0f;
	float step = 1.0f;// grid spacing
	std::vector< float > values;// rows of cols values, from the bottom
	float size = 1.0f;// how much of its room each packed symbol takes

	// Bilinear; negative outside the grid.
	float At( float x, float y ) const;
};

// In glyph units, y up. Without a shape, symbol styles (runes, sigils...) fit the [-1, 1] square and
// area styles (circuit, maze...) fill [-boxX, boxX] x [-boxY, boxY]. With one, every style follows it.
// The same arguments give the same glyph.
std::vector< Prim > Generate( Style style, uint32_t seed, float complexity, float boxX, float boxY, const Field* shape = nullptr );
}//End namespace glyphs
