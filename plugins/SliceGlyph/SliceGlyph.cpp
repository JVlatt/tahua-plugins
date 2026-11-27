#include "SliceGlyph.h"
#include <algorithm>
#include <cmath>
#include "Glyphs.h"
using namespace ffglqs;
using namespace ffglex;

static PluginInstance pluginInstance = Effect::CreatePlugin< SliceGlyph >( tahua::MakePluginInfo() );

namespace
{
// Each stroke takes 2 texels: a x, y, b x, y; then type, where it starts and ends along the glyph
// (0 to 1), radius. Stored in rows of this many texels.
const int TEXELS_PER_ROW  = 1024;
const int FLOATS_PER_PRIM = 8;
const int VERTEX_FLOATS   = 10;
const size_t MAX_PRIMS    = 128;// per glyph

enum Fit
{
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

// Distance in pixels from p to the nearest edge of the slice outline.
float DistanceToOutline( const PresetSlice& slice, float px, float py, float width, float height )
{
	float nearest = 1e30f;
	for( const std::vector< float >& ring : slice.contours )
	{
		size_t n = ring.size() / 2;
		for( size_t i = 0; i < n; ++i )
		{
			size_t j  = ( i + 1 ) % n;
			float ax  = ring[ 2 * i ] * width, ay = ring[ 2 * i + 1 ] * height;
			float abx = ring[ 2 * j ] * width - ax, aby = ring[ 2 * j + 1 ] * height - ay;
			float apx = px - ax, apy = py - ay;
			float t   = std::min( std::max( ( apx * abx + apy * aby ) / std::max( abx * abx + aby * aby, 1e-12f ), 0.0f ), 1.0f );
			nearest   = std::min( nearest, std::hypot( apx - abx * t, apy - aby * t ) );
		}
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

// The slice's own triangles, so that a glyph never spills out of its slice.
const char glyphVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec2 position; // normalised
layout( location = 1 ) in vec3 placement;// glyph centre x, y in pixels, pixels per glyph unit
layout( location = 2 ) in vec2 primRange;// first stroke, stroke count
layout( location = 3 ) in float offset;  // 0 to 1, fixed per slice
layout( location = 4 ) in vec2 extent;   // half size of the glyph, in glyph units
uniform vec2 resolution;
uniform float cycle;
uniform float spread;
out vec2 local;
flat out int firstPrim;
flat out int primCount;
flat out float phase;
flat out float pixelSize;
flat out vec2 bounds;

void main()
{
	gl_Position = vec4( position * 2.0 - 1.0, 0.0, 1.0 );
	local       = ( position * resolution - placement.xy ) / placement.z;
	pixelSize   = 1.0 / placement.z;
	firstPrim   = int( primRange.x );
	primCount   = int( primRange.y );
	phase       = fract( cycle + spread * offset );
	bounds      = extent;
}
)";

// Distance to each stroke in glyph units, and how much of it the animation shows.
const char glyphFragmentShader[] = R"(
#version 410 core
uniform sampler2D prims;
uniform float halfWidth;// in pixels
uniform float glow;
uniform vec4 color;
uniform int animation;// 0 off, 1 write, 2 write + erase, 3 comet
uniform float drawPart;
uniform float tail;
uniform float flicker;
uniform int flickerStep;
in vec2 local;
flat in int firstPrim;
flat in int primCount;
flat in float phase;
flat in float pixelSize;
flat in vec2 bounds;
out vec4 fragColor;

const float TAU = 6.28318531;

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
	float lineHalf   = halfWidth * pixelSize;// in glyph units
	float glowRadius = ( 2.0 + 30.0 * glow ) * pixelSize;
	// The halo fades to nothing about four radii away.
	float margin = lineHalf + ( glow > 0.0 ? 4.0 * glowRadius : 0.0 ) + 2.0 * pixelSize;
	if( any( greaterThan( abs( local ), bounds + margin ) ) )
		discard;

	float light      = 0.0;
	for( int i = 0; i < primCount; ++i )
	{
		int k  = ( firstPrim + i ) * 2;
		vec4 g = texelFetch( prims, ivec2( k % 1024, k / 1024 ), 0 );
		vec4 h = texelFetch( prims, ivec2( ( k + 1 ) % 1024, ( k + 1 ) / 1024 ), 0 );
		int type = int( h.x );
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
		if( flicker > 0.0 && Hash( uint( firstPrim + i ) * 1973u + uint( flickerStep ) * 9277u ) < flicker )
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
	AddParam( fit = ParamOption::Create( "Fit", { { "Inscribed", (float)FIT_INSCRIBED }, { "Box", (float)FIT_BOX } } ) );
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
	float fitValue        = fit->GetRealValue();
	float size            = GetParam( "Size" )->GetValue();
	if( selectionChanged || currentViewport.width != placedWidth || currentViewport.height != placedHeight ||
	    fitValue != placedFit || size != placedSize )
	{
		placedWidth  = currentViewport.width;
		placedHeight = currentViewport.height;
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
	for( const PresetScreen& screen : preset.screens )
	{
		if( !IsSelected( screen ) )
			continue;
		for( const PresetSlice& slice : screen.slices )
		{
			if( !IsSelected( screen, slice ) || slice.triangles.empty() )
				continue;
			Slot slot;
			slot.triangles = slice.triangles;
			float low[ 2 ] = { 1e30f, 1e30f }, high[ 2 ] = { -1e30f, -1e30f };
			for( size_t i = 0; i < slice.triangles.size(); i += 2 )
				for( int c = 0; c < 2; ++c )
				{
					float v   = slice.triangles[ i + c ] * ( c == 0 ? width : height );
					low[ c ]  = std::min( low[ c ], v );
					high[ c ] = std::max( high[ c ], v );
				}
			float half[ 2 ] = { 0.5f * ( high[ 0 ] - low[ 0 ] ), 0.5f * ( high[ 1 ] - low[ 1 ] ) };
			float smallest  = std::max( std::min( half[ 0 ], half[ 1 ] ), 1e-3f );
			slot.center[ 0 ] = 0.5f * ( low[ 0 ] + high[ 0 ] );
			slot.center[ 1 ] = 0.5f * ( low[ 1 ] + high[ 1 ] );
			if( placedFit == (float)FIT_BOX )
			{
				// The glyph spans the bounding box; the slice's shape clips it.
				slot.scale    = smallest * placedSize;
				slot.box[ 0 ] = half[ 0 ] / smallest;
				slot.box[ 1 ] = half[ 1 ] / smallest;
			}
			else
			{
				// Largest circle inside the shape, centred on one of its sample points.
				float radius = 0.0f;
				for( size_t i = 0; i + 1 < slice.samples.size(); i += 2 )
				{
					float px = slice.samples[ i ] * width, py = slice.samples[ i + 1 ] * height;
					float d  = DistanceToOutline( slice, px, py, width, height );
					if( d > radius )
					{
						radius           = d;
						slot.center[ 0 ] = px;
						slot.center[ 1 ] = py;
					}
				}
				if( radius <= 0.0f )
					radius = smallest;
				slot.scale = radius * placedSize;
				// Area styles stay within the circle.
				slot.box[ 0 ] = slot.box[ 1 ] = 0.72f;
			}
			slot.seed   = Hash( (uint32_t)( &screen - preset.screens.data() ) * 65536u + (uint32_t)( &slice - screen.slices.data() ) );
			slot.offset = (float)( Hash( slot.seed ) & 0xffffffu ) / (float)0x1000000;
			slots.push_back( std::move( slot ) );
		}
	}
}

void SliceGlyph::BuildGlyphs()
{
	// Per triangle vertex: position (2), centre and scale (3), stroke range (2), offset (1), extent (2).
	std::vector< float > vertices, texels;
	glyphs::Style styleValue = (glyphs::Style)(int)builtStyle;
	uint32_t seedValue       = (uint32_t)builtSeed;
	for( const Slot& slot : slots )
	{
		uint32_t glyphSeed = Hash( slot.seed ^ Hash( seedValue * 7919u + (uint32_t)slot.cycle * 2654435761u ) );
		std::vector< glyphs::Prim > prims = glyphs::Generate( styleValue, glyphSeed, builtComplexity, slot.box[ 0 ], slot.box[ 1 ] );
		if( prims.size() > MAX_PRIMS )
			prims.resize( MAX_PRIMS );
		if( prims.empty() )
			continue;
		float first     = (float)( texels.size() / FLOATS_PER_PRIM );
		float extent[ 2 ] = { 0.0f, 0.0f };
		for( const glyphs::Prim& p : prims )
		{
			texels.insert( texels.end(), { p.ax, p.ay, p.bx, p.by, (float)p.type, p.alongStart, p.alongEnd, p.radius } );
			if( p.type == glyphs::LINE )
			{
				extent[ 0 ] = std::max( { extent[ 0 ], std::abs( p.ax ), std::abs( p.bx ) } );
				extent[ 1 ] = std::max( { extent[ 1 ], std::abs( p.ay ), std::abs( p.by ) } );
			}
			else
			{
				extent[ 0 ] = std::max( extent[ 0 ], std::abs( p.ax ) + p.radius );
				extent[ 1 ] = std::max( extent[ 1 ], std::abs( p.ay ) + p.radius );
			}
		}
		for( size_t i = 0; i + 1 < slot.triangles.size(); i += 2 )
			vertices.insert( vertices.end(), { slot.triangles[ i ], slot.triangles[ i + 1 ], slot.center[ 0 ], slot.center[ 1 ], slot.scale,
			                                   first, (float)prims.size(), slot.offset, extent[ 0 ], extent[ 1 ] } );
	}
	Release();
	if( !vertices.empty() )
		Upload( vertices, texels );
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

	// A new glyph when the style changes, or each cycle with Change on.
	float styleValue = style->GetRealValue();
	float complexity = GetParam( "Complexity" )->GetValue();
	float seed       = std::floor( GetParam( "Seed" )->GetValue() );
	if( styleValue != builtStyle || complexity != builtComplexity || seed != builtSeed )
	{
		builtStyle      = styleValue;
		builtComplexity = complexity;
		builtSeed       = seed;
		glyphsDirty     = true;
	}
	bool changing = change->GetValue() > 0.5f;
	for( Slot& slot : slots )
	{
		// Same float sum as the vertex shader's phase, so both wrap on the same frame.
		long long cycle = changing ? (long long)whole + (long long)std::floor( frac + spread * slot.offset ) : 0;
		if( cycle != slot.cycle )
		{
			slot.cycle  = cycle;
			glyphsDirty = true;
		}
	}
	if( glyphsDirty )
	{
		BuildGlyphs();
		glyphsDirty = false;
	}

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( vertexCount == 0 )
		return FF_SUCCESS;

	float halfWidth = 0.5f * GetParam( "Thickness" )->GetValue();
	float glow      = GetParam( "Glow" )->GetValue();
	float hue       = GetParam( "Color" )->GetValue();
	float rgb[ 3 ];
	HSVtoRGB( hue == 1.0f ? 0.0f : hue,
	          GetParam( "Color_saturation" )->GetValue(),
	          GetParam( "Color_brightness" )->GetValue(),
	          rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );

	ScopedShaderBinding shaderBinding( glyphShader.GetGLID() );
	ScopedSamplerActivation activateSampler0( 0 );
	Scoped2DTextureBinding primsBinding( primsTexture );
	glyphShader.Set( "prims", 0 );
	glyphShader.Set( "resolution", (float)currentViewport.width, (float)currentViewport.height );
	glyphShader.Set( "cycle", frac );
	glyphShader.Set( "spread", spread );
	glyphShader.Set( "halfWidth", halfWidth );
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

void SliceGlyph::Upload( const std::vector< float >& vertices, const std::vector< float >& prims )
{
	vertexCount = (GLsizei)( vertices.size() / VERTEX_FLOATS );

	// Padded to whole rows of TEXELS_PER_ROW texels.
	int texelCount = (int)( prims.size() / 4 );
	int rows       = ( texelCount + TEXELS_PER_ROW - 1 ) / TEXELS_PER_ROW;
	std::vector< float > texels( prims );
	texels.resize( (size_t)rows * TEXELS_PER_ROW * 4, 0.0f );
	glGenTextures( 1, &primsTexture );
	{
		Scoped2DTextureBinding textureBinding( primsTexture );
		// texelFetch only, but the texture must still be complete: no mipmaps.
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, TEXELS_PER_ROW, rows, 0, GL_RGBA, GL_FLOAT, texels.data() );
	}

	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
	ScopedVAOBinding vaoBinding( vao );
	ScopedVBOBinding vboBinding( vbo );
	glBufferData( GL_ARRAY_BUFFER, vertices.size() * sizeof( float ), vertices.data(), GL_STATIC_DRAW );
	const GLsizei stride = VERTEX_FLOATS * sizeof( float );
	const GLint sizes[]  = { 2, 3, 2, 1, 2 };
	size_t offset        = 0;
	for( GLuint attribute = 0; attribute < 5; ++attribute )
	{
		glEnableVertexAttribArray( attribute );
		glVertexAttribPointer( attribute, sizes[ attribute ], GL_FLOAT, GL_FALSE, stride, (void*)( offset * sizeof( float ) ) );
		offset += sizes[ attribute ];
	}
}

void SliceGlyph::Release()
{
	glDeleteTextures( 1, &primsTexture );
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	primsTexture = vao = vbo = 0;
	vertexCount              = 0;
}
