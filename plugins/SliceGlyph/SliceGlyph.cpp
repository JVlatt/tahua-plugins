#include "SliceGlyph.h"
#include <algorithm>
#include <cmath>
#include <map>
using namespace ffglqs;
using namespace ffglex;

static PluginInstance pluginInstance = Effect::CreatePlugin< SliceGlyph >( tahua::MakePluginInfo() );

namespace
{
// One float texture, rows of this many texels, holds every glyph:
// - its strokes, 2 texels each: a x, y, b x, y; then type, where it starts and ends along the
//   glyph (0 to 1), radius;
// - its grid of cells, 1 texel each: first entry in the index list, entry count;
// - the index list: the strokes near each cell, 4 per texel.
const int TEXELS_PER_ROW  = 1024;
const int FLOATS_PER_PRIM = 8;
const int VERTEX_FLOATS   = 13;
const size_t MAX_PRIMS    = 4096;// per glyph
const int FIELD_CELLS     = 96;  // across the longer side of a shape's distance field

enum Group
{
	GROUP_SLICE,
	GROUP_FAMILY,
	GROUP_ALL
};

enum Fit
{
	FIT_SHAPE,
	FIT_INSCRIBED,
	FIT_BOX
};

enum Animation
{
	ANIMATION_OFF,
	ANIMATION_WRITE,
	ANIMATION_WRITE_ERASE,
	ANIMATION_COMET
};

uint32_t Hash( uint32_t n )
{
	n = ( n ^ 61u ) ^ ( n >> 16 );
	n *= 9u;
	n ^= n >> 4;
	n *= 0x27d4eb2du;
	n ^= n >> 15;
	return n;
}

uint32_t Hash( const std::string& text )
{
	uint32_t h = 0x5bd1e995u;
	for( char ch : text )
		h = Hash( h ^ (uint8_t)ch );
	return h;
}

// Which pixels of a bounding box the triangles cover.
struct Mask
{
	int x0 = 0, y0 = 0, w = 0, h = 0;
	std::vector< char > bits;

	bool At( float px, float py ) const
	{
		int x = (int)std::floor( px ) - x0, y = (int)std::floor( py ) - y0;
		return x >= 0 && y >= 0 && x < w && y < h && bits[ (size_t)y * w + x ];
	}
};

// triangles: normalised x, y per vertex.
Mask Rasterise( const std::vector< float >& triangles, float width, float height )
{
	Mask mask;
	float low[ 2 ] = { 1e30f, 1e30f }, high[ 2 ] = { -1e30f, -1e30f };
	for( size_t i = 0; i + 1 < triangles.size(); i += 2 )
		for( int c = 0; c < 2; ++c )
		{
			float v   = triangles[ i + c ] * ( c == 0 ? width : height );
			low[ c ]  = std::min( low[ c ], v );
			high[ c ] = std::max( high[ c ], v );
		}
	if( low[ 0 ] > high[ 0 ] )
		return mask;
	mask.x0 = (int)std::floor( low[ 0 ] ) - 1;
	mask.y0 = (int)std::floor( low[ 1 ] ) - 1;
	mask.w  = (int)std::ceil( high[ 0 ] ) + 2 - mask.x0;
	mask.h  = (int)std::ceil( high[ 1 ] ) + 2 - mask.y0;
	mask.bits.assign( (size_t)mask.w * mask.h, 0 );
	for( size_t i = 0; i + 5 < triangles.size(); i += 6 )
	{
		float x[ 3 ], y[ 3 ];
		for( int k = 0; k < 3; ++k )
		{
			x[ k ] = triangles[ i + 2 * k ] * width;
			y[ k ] = triangles[ i + 2 * k + 1 ] * height;
		}
		int xa = std::max( (int)std::floor( std::min( { x[ 0 ], x[ 1 ], x[ 2 ] } ) ), mask.x0 );
		int xb = std::min( (int)std::ceil( std::max( { x[ 0 ], x[ 1 ], x[ 2 ] } ) ), mask.x0 + mask.w - 1 );
		int ya = std::max( (int)std::floor( std::min( { y[ 0 ], y[ 1 ], y[ 2 ] } ) ), mask.y0 );
		int yb = std::min( (int)std::ceil( std::max( { y[ 0 ], y[ 1 ], y[ 2 ] } ) ), mask.y0 + mask.h - 1 );
		for( int py = ya; py <= yb; ++py )
			for( int px = xa; px <= xb; ++px )
			{
				float cx = (float)px + 0.5f, cy = (float)py + 0.5f;
				float d0 = ( x[ 1 ] - x[ 0 ] ) * ( cy - y[ 0 ] ) - ( y[ 1 ] - y[ 0 ] ) * ( cx - x[ 0 ] );
				float d1 = ( x[ 2 ] - x[ 1 ] ) * ( cy - y[ 1 ] ) - ( y[ 2 ] - y[ 1 ] ) * ( cx - x[ 1 ] );
				float d2 = ( x[ 0 ] - x[ 2 ] ) * ( cy - y[ 2 ] ) - ( y[ 0 ] - y[ 2 ] ) * ( cx - x[ 2 ] );
				if( ( d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f ) || ( d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f ) )
					mask.bits[ (size_t)( py - mask.y0 ) * mask.w + ( px - mask.x0 ) ] = 1;
			}
	}
	return mask;
}

// Edges of the slices' outlines, in pixels: a x, y, b x, y each. Borders between two slices of
// the group, or inside another of its slices, with the group's shapes on both sides, are left out.
std::vector< float > GroupOutline( const std::vector< const PresetSlice* >& slices, const Mask& mask, float width, float height )
{
	std::vector< float > edges;
	for( const PresetSlice* slice : slices )
		for( const std::vector< float >& ring : slice->contours )
		{
			size_t n = ring.size() / 2;
			for( size_t i = 0; i < n; ++i )
			{
				size_t j = ( i + 1 ) % n;
				float ax = ring[ 2 * i ] * width, ay = ring[ 2 * i + 1 ] * height;
				float bx = ring[ 2 * j ] * width, by = ring[ 2 * j + 1 ] * height;
				float length = std::hypot( bx - ax, by - ay );
				if( length <= 0.0f )
					continue;
				if( slices.size() > 1 )
				{
					// 2 pixels either side of the middle of the edge.
					float mx = 0.5f * ( ax + bx ), my = 0.5f * ( ay + by );
					float nx = -( by - ay ) / length * 2.0f, ny = ( bx - ax ) / length * 2.0f;
					if( mask.At( mx + nx, my + ny ) && mask.At( mx - nx, my - ny ) )
						continue;
				}
				edges.insert( edges.end(), { ax, ay, bx, by } );
			}
		}
	return edges;
}

// Distance from p to the nearest of the edges.
float DistanceToSegments( const std::vector< float >& edges, float px, float py )
{
	float nearest = 1e30f;
	for( size_t i = 0; i + 3 < edges.size(); i += 4 )
	{
		float abx = edges[ i + 2 ] - edges[ i ], aby = edges[ i + 3 ] - edges[ i + 1 ];
		float apx = px - edges[ i ], apy = py - edges[ i + 1 ];
		float t   = std::min( std::max( ( apx * abx + apy * aby ) / std::max( abx * abx + aby * aby, 1e-12f ), 0.0f ), 1.0f );
		nearest   = std::min( nearest, std::hypot( apx - abx * t, apy - aby * t ) );
	}
	return nearest;
}

// Unused: the quickstart compiles it, but drawing is done by the shaders below.
const char unusedFragmentShader[] = R"(
void main()
{
	fragColor = vec4( 0.0 );
}
)";

// The slices' own triangles, so that a glyph never spills out of its slices.
const char glyphVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec2 position; // normalised
layout( location = 1 ) in vec3 placement;// glyph centre x, y in pixels, pixels per glyph unit
layout( location = 2 ) in vec2 gridHalf; // half size of the glyph's grid of cells, in glyph units
layout( location = 3 ) in vec2 gridSize; // cells across, up
layout( location = 4 ) in vec3 block;   // texel where its data starts; its cells and index list, from there
layout( location = 5 ) in float offset;  // 0 to 1, fixed per glyph
uniform vec2 resolution;
uniform float cycle;
uniform float spread;
out vec2 local;
flat out vec2 area;
flat out ivec2 grid;
flat out ivec3 start;
flat out float phase;
flat out float pixelSize;

void main()
{
	gl_Position = vec4( position * 2.0 - 1.0, 0.0, 1.0 );
	local       = ( position * resolution - placement.xy ) / placement.z;
	pixelSize   = 1.0 / placement.z;
	area        = gridHalf;
	grid        = ivec2( gridSize );
	start       = ivec3( block.x, block.x + block.y, block.x + block.z );
	phase       = fract( cycle + spread * offset );
}
)";

// Distance to each stroke near the pixel, in glyph units, and how much of it the animation shows.
const char glyphFragmentShader[] = R"(
#version 410 core
uniform sampler2D data;
uniform float halfWidth;// in pixels
uniform float glow;
uniform vec4 color;
uniform int animation;// 0 off, 1 write, 2 write + erase, 3 comet
uniform float drawPart;
uniform float tail;
uniform float flicker;
uniform int flickerStep;
in vec2 local;
flat in vec2 area;
flat in ivec2 grid;
flat in ivec3 start;// texel of its first stroke, first cell, index list
flat in float phase;
flat in float pixelSize;
out vec4 fragColor;

const float TAU = 6.28318531;

vec4 Fetch( int texel )
{
	return texelFetch( data, ivec2( texel % 1024, texel / 1024 ), 0 );
}

float Hash( uint n )
{
	n = ( n ^ 61u ) ^ ( n >> 16 );
	n *= 9u;
	n ^= n >> 4;
	n *= 0x27d4eb2du;
	n ^= n >> 15;
	return float( n & 0xffffffu ) / 16777216.0;
}

// 1 where the pen, at edge along the glyph, has passed.
float Reached( float edge, float along )
{
	return clamp( ( edge * 1.01 - along ) * 300.0, 0.0, 1.0 );
}

float Shown( float along )
{
	if( animation == 1 )
		return Reached( min( phase / drawPart, 1.0 ), along );
	if( animation == 2 )
	{
		float drawn  = min( phase / drawPart, 1.0 );
		float erased = clamp( ( phase - ( 1.0 - drawPart ) ) / drawPart, 0.0, 1.0 );
		return Reached( drawn, along ) * ( 1.0 - Reached( erased, along ) );
	}
	if( animation == 3 )
		return tail >= 1.0 ? 1.0 : clamp( 1.0 - fract( phase - along ) / max( tail, 1e-4 ), 0.0, 1.0 );
	return 1.0;
}

void main()
{
	if( any( greaterThan( abs( local ), area ) ) )
		discard;
	ivec2 cell = clamp( ivec2( ( local + area ) / ( 2.0 * area ) * vec2( grid ) ), ivec2( 0 ), grid - 1 );
	vec4 entry = Fetch( start.y + cell.y * grid.x + cell.x );
	int first  = int( entry.x );
	int count  = int( entry.y );

	float lineHalf   = halfWidth * pixelSize;// in glyph units
	float glowRadius = ( 2.0 + 30.0 * glow ) * pixelSize;
	float light      = 0.0;
	for( int i = 0; i < count; ++i )
	{
		int index = first + i;
		int prim  = int( Fetch( start.z + index / 4 )[ index % 4 ] );// within the glyph
		vec4 g    = Fetch( start.x + prim * 2 );
		vec4 h    = Fetch( start.x + prim * 2 + 1 );
		int type  = int( h.x );
		float d;// signed distance to the stroke's edge
		float t;// where along the stroke, 0 to 1
		if( type == 0 )
		{
			vec2 ab = g.zw - g.xy;
			vec2 ap = local - g.xy;
			t       = clamp( dot( ap, ab ) / max( dot( ab, ab ), 1e-12 ), 0.0, 1.0 );
			d       = length( ap - ab * t ) - lineHalf;
		}
		else if( type == 1 )
		{
			vec2 v      = local - g.xy;
			float sweep = g.w - g.z;
			float span  = abs( sweep );
			float rel   = mod( ( atan( v.y, v.x ) - g.z ) * sign( sweep ), TAU );
			if( rel <= span )
			{
				t = rel / max( span, 1e-6 );
				d = abs( length( v ) - h.w ) - lineHalf;
			}
			else
			{
				float d0 = length( v - h.w * vec2( cos( g.z ), sin( g.z ) ) );
				float d1 = length( v - h.w * vec2( cos( g.w ), sin( g.w ) ) );
				t        = d0 < d1 ? 0.0 : 1.0;
				d        = min( d0, d1 ) - lineHalf;
			}
		}
		else
		{
			t = 0.5;
			d = length( local - g.xy ) - max( h.w, lineHalf * 1.5 );
		}

		float shown = Shown( mix( h.y, h.z, t ) );
		if( flicker > 0.0 && Hash( uint( start.x + prim ) * 1973u + uint( flickerStep ) * 9277u ) < flicker )
			shown = 0.0;
		float cover = clamp( 0.5 - d / pixelSize, 0.0, 1.0 );
		float halo  = glow * exp( -max( d, 0.0 ) / glowRadius );
		light       = max( light, max( cover, halo ) * shown );
	}
	if( light <= 0.0 )
		discard;
	fragColor = vec4( color.rgb, 1.0 ) * color.a * light;
}
)";

}//End anonymous namespace

SliceGlyph::SliceGlyph()
{
	SetFragmentShader( unusedFragmentShader );

	AddSlicesFileParam();
	// Names become shader uniforms, so the spaced versions are display names only.
	std::vector< ParamOption::Option > styles;
	for( int i = 0; i <= glyphs::STYLE_COUNT; ++i )
		styles.push_back( { glyphs::STYLE_NAMES[ i ], (float)i } );
	AddParam( style = ParamOption::Create( "Style", styles, glyphs::MIX ) );
	AddParam( ParamRange::Create( "Complexity", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Seed", 0.0f, { 0.0f, 100.0f } ) );
	AddParam( group = ParamOption::Create( "Group", { { "Slice", (float)GROUP_SLICE }, { "Family", (float)GROUP_FAMILY }, { "All", (float)GROUP_ALL } } ) );
	AddParam( fit = ParamOption::Create( "Fit", { { "Shape", (float)FIT_SHAPE }, { "Inscribed", (float)FIT_INSCRIBED }, { "Box", (float)FIT_BOX } } ) );
	AddParam( ParamRange::Create( "Size", 0.85f, { 0.1f, 1.5f } ) );
	AddParam( ParamRange::Create( "Thickness", 2.0f, { 0.0f, 20.0f } ) );// in pixels of the layer
	AddParam( ParamRange::Create( "Glow", 0.0f, { 0.0f, 1.0f } ) );
	AddHueColorParam( "Color" );
	AddParam( animation = ParamOption::Create( "Animation", { { "Off", (float)ANIMATION_OFF }, { "Write", (float)ANIMATION_WRITE }, { "Write + Erase", (float)ANIMATION_WRITE_ERASE }, { "Comet", (float)ANIMATION_COMET } }, ANIMATION_WRITE_ERASE ) );
	AddParam( cycleBeats = ParamOption::Create( "CycleBeats", { { "1", 1.0f }, { "2", 2.0f }, { "4", 4.0f }, { "8", 8.0f }, { "16", 16.0f }, { "32", 32.0f } }, 2 ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Cycle Beats", false );
	AddParam( ParamRange::Create( "Draw", 0.4f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Length", 0.3f, { 0.0f, 1.0f } ) );
	AddParam( change = ParamBool::Create( "Change", true ) );
	AddParam( ParamRange::Create( "Spread", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Flicker", 0.0f, { 0.0f, 1.0f } ) );
	AddSelectionParams();
}

FFResult SliceGlyph::Init()
{
	if( !glyphShader.Compile( glyphVertexShader, glyphFragmentShader ) )
		return FF_FAIL;
	return FF_SUCCESS;
}

void SliceGlyph::Update()
{
	bool selectionChanged = UpdateSelection().changed;
	float groupValue      = group->GetRealValue();
	float fitValue        = fit->GetRealValue();
	float size            = GetParam( "Size" )->GetValue();
	if( selectionChanged || currentViewport.width != placedWidth || currentViewport.height != placedHeight ||
	    groupValue != placedGroup || fitValue != placedFit || size != placedSize )
	{
		placedWidth  = currentViewport.width;
		placedHeight = currentViewport.height;
		placedGroup  = groupValue;
		placedFit    = fitValue;
		placedSize   = size;
		PlaceSlots();
	}
}

void SliceGlyph::PlaceSlots()
{
	slots.clear();
	glyphsDirty  = true;
	float width  = (float)currentViewport.width;
	float height = (float)currentViewport.height;

	// Slices sharing one glyph: each slice alone, every slice of a family of a screen, or all of them.
	std::vector< std::vector< const PresetSlice* > > groups;
	std::vector< uint32_t > seeds;
	std::map< std::string, size_t > groupOf;
	for( const PresetScreen& screen : preset.screens )
	{
		if( !IsSelected( screen ) )
			continue;
		std::string screenKey = std::to_string( &screen - preset.screens.data() );
		for( const PresetSlice& slice : screen.slices )
		{
			if( !IsSelected( screen, slice ) || slice.triangles.empty() )
				continue;
			std::string key = placedGroup == (float)GROUP_ALL      ? std::string( "*" )
			                : placedGroup == (float)GROUP_FAMILY ? screenKey + ">" + slice.family
			                                                     : screenKey + "#" + std::to_string( &slice - screen.slices.data() );
			auto found = groupOf.find( key );
			if( found != groupOf.end() )
			{
				groups[ found->second ].push_back( &slice );
				continue;
			}
			groupOf[ key ] = groups.size();
			groups.push_back( { &slice } );
			seeds.push_back( Hash( key ) );
		}
	}

	for( size_t g = 0; g < groups.size(); ++g )
	{
		Slot slot;
		for( const PresetSlice* slice : groups[ g ] )
			slot.triangles.insert( slot.triangles.end(), slice->triangles.begin(), slice->triangles.end() );
		float low[ 2 ] = { 1e30f, 1e30f }, high[ 2 ] = { -1e30f, -1e30f };
		for( size_t i = 0; i < slot.triangles.size(); i += 2 )
			for( int c = 0; c < 2; ++c )
			{
				float v   = slot.triangles[ i + c ] * ( c == 0 ? width : height );
				low[ c ]  = std::min( low[ c ], v );
				high[ c ] = std::max( high[ c ], v );
			}
		float half[ 2 ] = { 0.5f * ( high[ 0 ] - low[ 0 ] ), 0.5f * ( high[ 1 ] - low[ 1 ] ) };
		float smallest  = std::max( std::min( half[ 0 ], half[ 1 ] ), 1e-3f );
		slot.center[ 0 ] = 0.5f * ( low[ 0 ] + high[ 0 ] );
		slot.center[ 1 ] = 0.5f * ( low[ 1 ] + high[ 1 ] );

		if( placedFit == (float)FIT_BOX )
		{
			// The glyph spans the bounding box; the shapes clip it.
			slot.scale    = smallest * placedSize;
			slot.box[ 0 ] = half[ 0 ] / smallest;
			slot.box[ 1 ] = half[ 1 ] / smallest;
		}
		else
		{
			Mask mask                  = Rasterise( slot.triangles, width, height );
			std::vector< float > edges = GroupOutline( groups[ g ], mask, width, height );
			if( placedFit == (float)FIT_SHAPE )
			{
				// The bounding box in glyph units, and the distance to the outline sampled over it.
				slot.scale    = smallest;
				slot.box[ 0 ] = half[ 0 ] / smallest;
				slot.box[ 1 ] = half[ 1 ] / smallest;
				slot.shaped   = true;
				glyphs::Field& f = slot.field;
				float spanX = slot.box[ 0 ] + 0.05f, spanY = slot.box[ 1 ] + 0.05f;
				// About 4 pixels per cell: small slices get a small grid.
				float across = std::min( std::max( 2.0f * std::max( spanX, spanY ) * slot.scale / 4.0f, 12.0f ), (float)FIELD_CELLS );
				f.step       = 2.0f * std::max( spanX, spanY ) / across;
				f.cols   = (int)std::ceil( 2.0f * spanX / f.step ) + 1;
				f.rows   = (int)std::ceil( 2.0f * spanY / f.step ) + 1;
				f.x0     = -spanX;
				f.y0     = -spanY;
				f.size   = placedSize;
				f.values.resize( (size_t)f.cols * f.rows );
				for( int j = 0; j < f.rows; ++j )
					for( int i = 0; i < f.cols; ++i )
					{
						float px = slot.center[ 0 ] + ( f.x0 + (float)i * f.step ) * slot.scale;
						float py = slot.center[ 1 ] + ( f.y0 + (float)j * f.step ) * slot.scale;
						float d  = DistanceToSegments( edges, px, py ) / slot.scale;
						f.values[ (size_t)j * f.cols + i ] = mask.At( px, py ) ? d : -d;
					}
			}
			else
			{
				// Largest circle inside the shapes, centred on one of their sample points.
				float radius = 0.0f;
				for( const PresetSlice* slice : groups[ g ] )
					for( size_t i = 0; i + 1 < slice->samples.size(); i += 2 )
					{
						float px = slice->samples[ i ] * width, py = slice->samples[ i + 1 ] * height;
						float d  = DistanceToSegments( edges, px, py );
						if( d > radius )
						{
							radius           = d;
							slot.center[ 0 ] = px;
							slot.center[ 1 ] = py;
						}
					}
				if( radius <= 0.0f || radius >= 1e29f )
					radius = smallest;
				slot.scale = radius * placedSize;
				// Area styles stay within the circle.
				slot.box[ 0 ] = slot.box[ 1 ] = 0.72f;
			}
		}
		slot.seed   = seeds[ g ];
		slot.offset = (float)( Hash( slot.seed ) & 0xffffffu ) / (float)0x1000000;
		slots.push_back( std::move( slot ) );
	}
}

void SliceGlyph::BuildBlock( Slot& slot )
{
	if( slot.stale )
	{
		uint32_t glyphSeed = Hash( slot.seed ^ Hash( (uint32_t)builtSeed * 7919u + (uint32_t)slot.cycle * 2654435761u ) );
		slot.glyph = glyphs::Generate( (glyphs::Style)(int)builtStyle, glyphSeed, builtComplexity, slot.box[ 0 ], slot.box[ 1 ],
		                               slot.shaped ? &slot.field : nullptr );
		if( slot.glyph.size() > MAX_PRIMS )
			slot.glyph.resize( MAX_PRIMS );
		slot.stale = false;
	}

	// Strokes (2 texels each), then cells (1 texel each), then the index list (4 per texel).
	const std::vector< glyphs::Prim >& glyph = slot.glyph;
	slot.block.clear();
	if( glyph.empty() )
	{
		// One empty cell: nothing to draw.
		slot.gridHalf[ 0 ] = slot.gridHalf[ 1 ] = 1e-6f;
		slot.grid[ 0 ] = slot.grid[ 1 ] = 1;
		slot.cellOffset  = 0;
		slot.indexOffset = 1;
		slot.block       = { 0.0f, 0.0f, 0.0f, 0.0f };
		return;
	}

	// How far from a stroke a pixel can still be lit: line, halo, antialiasing; in glyph units.
	float reach  = 0.5f * builtThickness + ( builtGlow > 0.0f ? 4.0f * ( 2.0f + 30.0f * builtGlow ) : 0.0f ) + 2.0f;
	float margin = reach / slot.scale;
	std::vector< float > bounds;// low x, y, high x, y per stroke
	float extent[ 2 ] = { 0.0f, 0.0f };
	for( const glyphs::Prim& p : glyph )
	{
		slot.block.insert( slot.block.end(), { p.ax, p.ay, p.bx, p.by, (float)p.type, p.alongStart, p.alongEnd, p.radius } );
		float lx, ly, hx, hy;
		if( p.type == glyphs::LINE )
		{
			lx = std::min( p.ax, p.bx );
			hx = std::max( p.ax, p.bx );
			ly = std::min( p.ay, p.by );
			hy = std::max( p.ay, p.by );
		}
		else
		{
			lx = p.ax - p.radius;
			hx = p.ax + p.radius;
			ly = p.ay - p.radius;
			hy = p.ay + p.radius;
		}
		bounds.insert( bounds.end(), { lx - margin, ly - margin, hx + margin, hy + margin } );
		extent[ 0 ] = std::max( { extent[ 0 ], std::abs( lx ), std::abs( hx ) } );
		extent[ 1 ] = std::max( { extent[ 1 ], std::abs( ly ), std::abs( hy ) } );
	}

	// A grid over the glyph: each cell lists the strokes that can light it.
	float* half  = slot.gridHalf;
	half[ 0 ]    = extent[ 0 ] + margin;
	half[ 1 ]    = extent[ 1 ] + margin;
	float longer = std::max( half[ 0 ], half[ 1 ] );
	int across   = std::min( std::max( (int)std::ceil( std::sqrt( (float)glyph.size() ) * 1.5f ), 1 ), 96 );
	int* grid    = slot.grid;
	grid[ 0 ]    = std::max( 1, (int)std::round( across * half[ 0 ] / longer ) );
	grid[ 1 ]    = std::max( 1, (int)std::round( across * half[ 1 ] / longer ) );
	std::vector< std::vector< int > > lists( (size_t)grid[ 0 ] * grid[ 1 ] );
	auto Cell = [ & ]( float v, int axis ) {
		return std::min( std::max( (int)std::floor( ( v + half[ axis ] ) / ( 2.0f * half[ axis ] ) * (float)grid[ axis ] ), 0 ), grid[ axis ] - 1 );
	};
	for( size_t k = 0; k < glyph.size(); ++k )
	{
		int x0 = Cell( bounds[ 4 * k ], 0 ), y0 = Cell( bounds[ 4 * k + 1 ], 1 );
		int x1 = Cell( bounds[ 4 * k + 2 ], 0 ), y1 = Cell( bounds[ 4 * k + 3 ], 1 );
		for( int y = y0; y <= y1; ++y )
			for( int x = x0; x <= x1; ++x )
				lists[ (size_t)y * grid[ 0 ] + x ].push_back( (int)k );
	}
	slot.cellOffset = (int)( slot.block.size() / 4 );
	std::vector< float > indices;
	for( const std::vector< int >& list : lists )
	{
		slot.block.insert( slot.block.end(), { (float)indices.size(), (float)list.size(), 0.0f, 0.0f } );
		for( int index : list )
			indices.push_back( (float)index );
	}
	slot.indexOffset = (int)( slot.block.size() / 4 );
	slot.block.insert( slot.block.end(), indices.begin(), indices.end() );
	slot.block.resize( ( slot.block.size() + 3 ) / 4 * 4, 0.0f );
}

void SliceGlyph::WriteVertices( const Slot& slot, std::vector< float >& out ) const
{
	// Per triangle vertex: position (2), centre and scale (3), grid half size (2), grid size (2),
	// block start, cell offset, index offset (3), cycle offset (1).
	for( size_t i = 0; i + 1 < slot.triangles.size(); i += 2 )
		out.insert( out.end(), { slot.triangles[ i ], slot.triangles[ i + 1 ], slot.center[ 0 ], slot.center[ 1 ], slot.scale,
		                         slot.gridHalf[ 0 ], slot.gridHalf[ 1 ], (float)slot.grid[ 0 ], (float)slot.grid[ 1 ],
		                         (float)slot.blockStart, (float)slot.cellOffset, (float)slot.indexOffset, slot.offset } );
}

void SliceGlyph::Layout()
{
	// Each slot gets room to grow, so that its next glyphs can usually be written in place.
	std::vector< float > vertices;
	int texels = 0;
	for( Slot& slot : slots )
	{
		int size         = (int)( slot.block.size() / 4 );
		slot.blockStart  = texels;
		slot.capacity    = size + size / 2 + 64;
		slot.firstVertex = (int)( vertices.size() / VERTEX_FLOATS );
		texels += slot.capacity;
		WriteVertices( slot, vertices );
	}
	Release();
	if( vertices.empty() )
		return;
	std::vector< float > data( (size_t)texels * 4, 0.0f );
	for( const Slot& slot : slots )
		std::copy( slot.block.begin(), slot.block.end(), data.begin() + (size_t)slot.blockStart * 4 );
	Upload( vertices, data );
}

bool SliceGlyph::Rewrite( Slot& slot )
{
	if( (int)( slot.block.size() / 4 ) > slot.capacity )
		return false;
	// Its texels, a range of whole and partial rows.
	{
		Scoped2DTextureBinding textureBinding( dataTexture );
		int texel          = slot.blockStart;
		int count          = (int)( slot.block.size() / 4 );
		const float* value = slot.block.data();
		while( count > 0 )
		{
			int x = texel % TEXELS_PER_ROW, y = texel / TEXELS_PER_ROW;
			int n;
			if( x != 0 || count < TEXELS_PER_ROW )
			{
				n = std::min( TEXELS_PER_ROW - x, count );
				glTexSubImage2D( GL_TEXTURE_2D, 0, x, y, n, 1, GL_RGBA, GL_FLOAT, value );
			}
			else
			{
				int rows = count / TEXELS_PER_ROW;
				n        = rows * TEXELS_PER_ROW;
				glTexSubImage2D( GL_TEXTURE_2D, 0, 0, y, TEXELS_PER_ROW, rows, GL_RGBA, GL_FLOAT, value );
			}
			texel += n;
			value += (size_t)n * 4;
			count -= n;
		}
	}
	// Its vertices: grid and offsets may have changed.
	std::vector< float > vertices;
	WriteVertices( slot, vertices );
	ScopedVBOBinding vboBinding( vbo );
	glBufferSubData( GL_ARRAY_BUFFER, (GLintptr)slot.firstVertex * VERTEX_FLOATS * sizeof( float ), vertices.size() * sizeof( float ), vertices.data() );
	return true;
}

double SliceGlyph::CountBeats()
{
	// barPhase runs 0 -> 1 over each bar of 4 beats: a big drop means a new bar.
	if( barPhase + 0.5f < lastBarPhase )
		bars += 1.0;
	lastBarPhase = barPhase;
	return ( bars + barPhase ) * 4.0;
}

FFResult SliceGlyph::Render( ProcessOpenGLStruct* )
{
	double beats  = CountBeats();
	double cycles = beats / cycleBeats->GetRealValue();
	double whole  = std::floor( cycles );
	float frac    = (float)( cycles - whole );
	float spread  = GetParam( "Spread" )->GetValue();

	// A new glyph when the style changes, or each cycle with Change on. Thickness and glow change
	// how far a stroke reaches, which the grid of cells depends on.
	float styleValue = style->GetRealValue();
	float complexity = GetParam( "Complexity" )->GetValue();
	float seed       = std::floor( GetParam( "Seed" )->GetValue() );
	float thickness  = GetParam( "Thickness" )->GetValue();
	float glow       = GetParam( "Glow" )->GetValue();
	bool regenerate  = styleValue != builtStyle || complexity != builtComplexity || seed != builtSeed;
	if( regenerate || thickness != builtThickness || glow != builtGlow )
	{
		builtStyle      = styleValue;
		builtComplexity = complexity;
		builtSeed       = seed;
		builtThickness  = thickness;
		builtGlow       = glow;
		glyphsDirty     = true;
	}
	bool changing = change->GetValue() > 0.5f;
	std::vector< Slot* > changed;
	for( Slot& slot : slots )
	{
		// Same float sum as the vertex shader's phase, so both wrap on the same frame.
		long long cycle = changing ? (long long)whole + (long long)std::floor( frac + spread * slot.offset ) : 0;
		if( cycle != slot.cycle || regenerate )
		{
			slot.cycle = cycle;
			slot.stale = true;
			changed.push_back( &slot );
		}
	}
	if( glyphsDirty )
	{
		for( Slot& slot : slots )
			BuildBlock( slot );
		Layout();
		glyphsDirty = false;
	}
	else
		for( Slot* slot : changed )
		{
			BuildBlock( *slot );
			if( !Rewrite( *slot ) )
			{
				// Outgrew its room: lay everything out again, the slots still waiting included.
				for( Slot& other : slots )
					if( other.stale )
						BuildBlock( other );
				Layout();
				break;
			}
		}

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( vertexCount == 0 )
		return FF_SUCCESS;

	float hue = GetParam( "Color" )->GetValue();
	float rgb[ 3 ];
	HSVtoRGB( hue == 1.0f ? 0.0f : hue,
	          GetParam( "Color_saturation" )->GetValue(),
	          GetParam( "Color_brightness" )->GetValue(),
	          rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );

	ScopedShaderBinding shaderBinding( glyphShader.GetGLID() );
	ScopedSamplerActivation activateSampler0( 0 );
	Scoped2DTextureBinding dataBinding( dataTexture );
	glyphShader.Set( "data", 0 );
	glyphShader.Set( "resolution", (float)currentViewport.width, (float)currentViewport.height );
	glyphShader.Set( "cycle", frac );
	glyphShader.Set( "spread", spread );
	glyphShader.Set( "halfWidth", 0.5f * thickness );
	glyphShader.Set( "glow", glow );
	glyphShader.Set( "color", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], GetParam( "Color_alpha" )->GetValue() );
	glyphShader.Set( "animation", (int)animation->GetRealValue() );
	glyphShader.Set( "drawPart", std::max( GetParam( "Draw" )->GetValue(), 0.01f ) );
	glyphShader.Set( "tail", GetParam( "Length" )->GetValue() );
	glyphShader.Set( "flicker", GetParam( "Flicker" )->GetValue() );
	glyphShader.Set( "flickerStep", (int)std::fmod( std::floor( beats * 4.0 ), 1e6 ) );

	// The same slice on several screens overlaps itself: keep the brightest instead of adding up.
	glEnable( GL_BLEND );
	glBlendEquation( GL_MAX );
	ScopedVAOBinding vaoBinding( vao );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	glBlendEquation( GL_FUNC_ADD );
	glDisable( GL_BLEND );
	return FF_SUCCESS;
}

void SliceGlyph::Clean()
{
	Release();
	ClearPreset();
	glyphShader.FreeGLResources();
}

void SliceGlyph::Upload( const std::vector< float >& vertices, const std::vector< float >& data )
{
	vertexCount = (GLsizei)( vertices.size() / VERTEX_FLOATS );

	// Padded to whole rows of TEXELS_PER_ROW texels.
	int texelCount = (int)( ( data.size() + 3 ) / 4 );
	int rows       = ( texelCount + TEXELS_PER_ROW - 1 ) / TEXELS_PER_ROW;
	std::vector< float > texels( data );
	texels.resize( (size_t)rows * TEXELS_PER_ROW * 4, 0.0f );
	glGenTextures( 1, &dataTexture );
	{
		Scoped2DTextureBinding textureBinding( dataTexture );
		// texelFetch only, but the texture must still be complete: no mipmaps.
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, TEXELS_PER_ROW, rows, 0, GL_RGBA, GL_FLOAT, texels.data() );
	}

	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
	ScopedVAOBinding vaoBinding( vao );
	ScopedVBOBinding vboBinding( vbo );
	// Rewritten slot by slot as glyphs change.
	glBufferData( GL_ARRAY_BUFFER, vertices.size() * sizeof( float ), vertices.data(), GL_DYNAMIC_DRAW );
	const GLsizei stride = VERTEX_FLOATS * sizeof( float );
	const GLint sizes[]  = { 2, 3, 2, 2, 3, 1 };
	size_t offset        = 0;
	for( GLuint attribute = 0; attribute < 6; ++attribute )
	{
		glEnableVertexAttribArray( attribute );
		glVertexAttribPointer( attribute, sizes[ attribute ], GL_FLOAT, GL_FALSE, stride, (void*)( offset * sizeof( float ) ) );
		offset += sizes[ attribute ];
	}
}

void SliceGlyph::Release()
{
	glDeleteTextures( 1, &dataTexture );
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	dataTexture = vao = vbo = 0;
	vertexCount             = 0;
}
