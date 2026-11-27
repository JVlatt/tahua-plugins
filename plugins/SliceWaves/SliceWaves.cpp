#include "SliceWaves.h"
#include <algorithm>
#include <cmath>
#include <WaveShapes.h>
using namespace ffglqs;
using namespace ffglex;

static PluginInstance pluginInstance = Effect::CreatePlugin< SliceWaves >( tahua::MakePluginInfo() );

namespace
{
const float PI = 3.14159265f;
// Edges are stored one per texel (start x, y, end x, y, normalised) in rows of this many texels.
const int TEXELS_PER_ROW = 1024;
const int VERTEX_FLOATS  = 15;
// Slices closer than this, in pixels of the layer, are neighbours. Between slices that are not,
// the Neighbour wave still jumps, but takes this many times longer per pixel.
const float NEIGHBOUR_GAP     = 8.0f;
const float NEIGHBOUR_PENALTY = 3.0f;

enum Offset
{
	OFFSET_RANDOM,
	OFFSET_ORDER,
	OFFSET_NEIGHBOUR
};

// 0 to 1, fixed for a given n.
float Hash( unsigned int n )
{
	n = ( n ^ 61u ) ^ ( n >> 16 );
	n *= 9u;
	n ^= n >> 4;
	n *= 0x27d4eb2du;
	n ^= n >> 15;
	return (float)( n & 0xffffffu ) / (float)0x1000000;
}

struct Point
{
	float x, y;
};

float Cross( Point o, Point a, Point b )
{
	return ( a.x - o.x ) * ( b.y - o.y ) - ( a.y - o.y ) * ( b.x - o.x );
}

// Convex hull, counter-clockwise (Andrew's monotone chain).
std::vector< Point > Hull( std::vector< Point > points )
{
	std::sort( points.begin(), points.end(), []( Point a, Point b ) { return a.x < b.x || ( a.x == b.x && a.y < b.y ); } );
	if( points.size() < 3 )
		return points;
	std::vector< Point > hull( 2 * points.size() );
	size_t k = 0;
	for( size_t i = 0; i < points.size(); ++i )
	{
		while( k >= 2 && Cross( hull[ k - 2 ], hull[ k - 1 ], points[ i ] ) <= 0.0f )
			--k;
		hull[ k++ ] = points[ i ];
	}
	for( size_t i = points.size() - 1, lower = k + 1; i-- > 0; )
	{
		while( k >= lower && Cross( hull[ k - 2 ], hull[ k - 1 ], points[ i ] ) <= 0.0f )
			--k;
		hull[ k++ ] = points[ i ];
	}
	hull.resize( k - 1 );
	return hull;
}

// A slice's own frame: its smallest enclosing rectangle, in pixels.
struct Frame
{
	float cx, cy, hx, hy, angle;
};

// The rectangle of least area has a side along a hull edge. Of its four orientations, the one
// within 45 degrees of upright is kept, so that a slice that is nearly level keeps up as up.
Frame SmallestFrame( const std::vector< Point >& points )
{
	std::vector< Point > hull = Hull( points );
	Frame best{ 0.0f, 0.0f, 0.5f, 0.5f, 0.0f };
	float bestArea = 1e30f;
	for( size_t i = 0; i < hull.size(); ++i )
	{
		Point a = hull[ i ], b = hull[ ( i + 1 ) % hull.size() ];
		float angle = std::atan2( b.y - a.y, b.x - a.x );
		float ux = std::cos( angle ), uy = std::sin( angle );
		float lowS = 1e30f, highS = -1e30f, lowT = 1e30f, highT = -1e30f;
		for( Point p : hull )
		{
			float s = p.x * ux + p.y * uy, t = -p.x * uy + p.y * ux;
			lowS = std::min( lowS, s ), highS = std::max( highS, s );
			lowT = std::min( lowT, t ), highT = std::max( highT, t );
		}
		float area = ( highS - lowS ) * ( highT - lowT );
		if( area < bestArea )
		{
			bestArea  = area;
			float s   = 0.5f * ( lowS + highS ), t = 0.5f * ( lowT + highT );
			best      = { s * ux - t * uy, s * uy + t * ux, 0.5f * ( highS - lowS ), 0.5f * ( highT - lowT ), angle };
		}
	}
	while( best.angle > 0.25f * PI )
		best.angle -= 0.5f * PI, std::swap( best.hx, best.hy );
	while( best.angle <= -0.25f * PI )
		best.angle += 0.5f * PI, std::swap( best.hx, best.hy );
	best.hx = std::max( best.hx, 0.5f );
	best.hy = std::max( best.hy, 0.5f );
	return best;
}

float SegmentDistance( Point p, Point a, Point b )
{
	float abx = b.x - a.x, aby = b.y - a.y;
	float t   = std::clamp( ( ( p.x - a.x ) * abx + ( p.y - a.y ) * aby ) / std::max( abx * abx + aby * aby, 1e-12f ), 0.0f, 1.0f );
	float dx = p.x - a.x - abx * t, dy = p.y - a.y - aby * t;
	return std::sqrt( dx * dx + dy * dy );
}

// A selected slice, in pixels of the layer.
struct Shape
{
	std::vector< std::vector< Point > > rings;
	Point low, high;// bounding box
	Frame frame;
	float depth = 0.0f;// distance from the outline to the deepest of the slice's samples
	float offset = 0.0f;
	unsigned int seed = 0;

	float Distance( Point p ) const
	{
		float nearest = 1e30f;
		for( const auto& ring : rings )
			for( size_t i = 0; i < ring.size(); ++i )
				nearest = std::min( nearest, SegmentDistance( p, ring[ i ], ring[ ( i + 1 ) % ring.size() ] ) );
		return nearest;
	}
	bool Contains( Point p ) const
	{
		bool inside = false;
		for( const auto& ring : rings )
			for( size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++ )
				if( ( ring[ i ].y > p.y ) != ( ring[ j ].y > p.y ) &&
				    p.x < ring[ i ].x + ( p.y - ring[ i ].y ) * ( ring[ j ].x - ring[ i ].x ) / ( ring[ j ].y - ring[ i ].y ) )
					inside = !inside;
		return inside;
	}
};

bool Touch( const Shape& a, const Shape& b )
{
	if( a.low.x > b.high.x + NEIGHBOUR_GAP || b.low.x > a.high.x + NEIGHBOUR_GAP ||
	    a.low.y > b.high.y + NEIGHBOUR_GAP || b.low.y > a.high.y + NEIGHBOUR_GAP )
		return false;
	for( const auto& ring : a.rings )
		for( Point p : ring )
			if( b.Distance( p ) < NEIGHBOUR_GAP || b.Contains( p ) )
				return true;
	for( const auto& ring : b.rings )
		for( Point p : ring )
			if( a.Contains( p ) )
				return true;
	return false;
}

// Neighbour offsets: the length of the shortest path from the slice under the origin to each
// slice, through the centres of slices that touch, 0 to 1. Dense Dijkstra: every pair is linked,
// those that do not touch at NEIGHBOUR_PENALTY times their distance, so that islands are reached too.
void NeighbourOffsets( std::vector< Shape >& shapes, Point origin )
{
	size_t n = shapes.size();
	std::vector< char > touch( n * n, 0 );
	for( size_t i = 0; i < n; ++i )
		for( size_t j = i + 1; j < n; ++j )
			touch[ i * n + j ] = touch[ j * n + i ] = Touch( shapes[ i ], shapes[ j ] ) ? 1 : 0;

	size_t start   = 0;
	float closest  = 1e30f;
	for( size_t i = 0; i < n; ++i )
	{
		float dx = shapes[ i ].frame.cx - origin.x, dy = shapes[ i ].frame.cy - origin.y;
		float d  = shapes[ i ].Contains( origin ) ? -1.0f : dx * dx + dy * dy;
		if( d < closest )
			closest = d, start = i;
	}

	std::vector< float > distance( n, 1e30f );
	std::vector< char > done( n, 0 );
	distance[ start ] = 0.0f;
	for( size_t step = 0; step < n; ++step )
	{
		size_t u = n;
		for( size_t i = 0; i < n; ++i )
			if( !done[ i ] && ( u == n || distance[ i ] < distance[ u ] ) )
				u = i;
		done[ u ] = 1;
		for( size_t v = 0; v < n; ++v )
		{
			if( done[ v ] )
				continue;
			float dx = shapes[ u ].frame.cx - shapes[ v ].frame.cx, dy = shapes[ u ].frame.cy - shapes[ v ].frame.cy;
			float w  = std::sqrt( dx * dx + dy * dy ) * ( touch[ u * n + v ] ? 1.0f : NEIGHBOUR_PENALTY );
			distance[ v ] = std::min( distance[ v ], distance[ u ] + w );
		}
	}
	float farthest = *std::max_element( distance.begin(), distance.end() );
	for( size_t i = 0; i < n; ++i )
		shapes[ i ].offset = farthest > 0.0f ? distance[ i ] / farthest : 0.0f;
}

// Unused: the quickstart compiles it, but drawing is done by the shaders below.
const char unusedFragmentShader[] = R"(
void main()
{
	fragColor = vec4( 0.0 );
}
)";

// One quad per slice: its bounding box, one pixel larger for antialiasing.
const char waveVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec4 bounds;   // min x, y, max x, y, normalised
layout( location = 1 ) in vec2 corner;
layout( location = 2 ) in vec2 edgeRange;// first edge, edge count
layout( location = 3 ) in vec4 frame;    // centre x, y, half width, half height, in pixels
layout( location = 4 ) in vec3 extra;    // frame angle, time offset 0 -> 1, deepest point in pixels
uniform vec2 resolution;
uniform float cycle;
uniform float delay;
out vec2 pixel;
flat out int firstEdge;
flat out int edgeCount;
flat out vec4 sliceFrame;
flat out float sliceAngle;
flat out float slicePhase;
flat out float sliceDepth;

void main()
{
	vec2 low    = bounds.xy * resolution - 1.0;
	vec2 high   = bounds.zw * resolution + 1.0;
	pixel       = mix( low, high, corner );
	gl_Position = vec4( pixel / resolution * 2.0 - 1.0, 0.0, 1.0 );
	firstEdge   = int( edgeRange.x );
	edgeCount   = int( edgeRange.y );
	sliceFrame  = frame;
	sliceAngle  = extra.x;
	slicePhase  = fract( cycle - delay * extra.y );
	sliceDepth  = extra.z;
}
)";

// Followed by tahua::waveShapes, which reads these, then by waveFragmentMain.
const char waveFragmentHeader[] = R"(
#version 410 core
uniform sampler2D edges;
uniform vec2 resolution;
uniform float noiseTime;
uniform vec4 color;
uniform float Shape, Scale, Thickness, Softness, Echoes, Trail, Spread, Attack, Direction, CenterX, CenterY, Sides;
uniform bool Mirror;
uniform bool Invert;
in vec2 pixel;
flat in int firstEdge;
flat in int edgeCount;
flat in vec4 sliceFrame;
flat in float sliceAngle;
flat in float slicePhase;
flat in float sliceDepth;
out vec4 fragColor;
float progress;// This slice's phase in the cycle, set in main() before WaveShape() reads it.
)";

const char waveFragmentMain[] = R"(
void main()
{
	// Signed distance to the slice outline, in pixels (negative inside).
	float nearest = 1e30;
	bool inside   = false;
	for( int i = 0; i < edgeCount; ++i )
	{
		int k    = firstEdge + i;
		vec4 e   = texelFetch( edges, ivec2( k % 1024, k / 1024 ), 0 );
		vec2 a   = e.xy * resolution;
		vec2 b   = e.zw * resolution;
		vec2 ab  = b - a;
		vec2 ap  = pixel - a;
		vec2 q   = ap - ab * clamp( dot( ap, ab ) / max( dot( ab, ab ), 1e-12 ), 0.0, 1.0 );
		nearest  = min( nearest, dot( q, q ) );
		if( ( a.y > pixel.y ) != ( b.y > pixel.y ) && pixel.x < a.x + ( pixel.y - a.y ) * ab.x / ab.y )
			inside = !inside;
	}
	float d        = sqrt( nearest ) * ( inside ? -1.0 : 1.0 );
	float coverage = clamp( 0.5 - d, 0.0, 1.0 );
	if( coverage <= 0.0 )
		discard;

	// The slice's own frame, turned with it, its height 1.
	vec2 axis    = vec2( cos( sliceAngle ), sin( sliceAngle ) );
	vec2 rel     = pixel - sliceFrame.xy;
	vec2 local   = vec2( dot( rel, axis ), dot( rel, vec2( -axis.y, axis.x ) ) );
	float height = 2.0 * sliceFrame.w;
	progress     = slicePhase;
	contourDepth = -d / height;
	contourMax   = sliceDepth / height;

	float v = WaveShape( local / ( 2.0 * sliceFrame.zw ) + 0.5, sliceFrame.z / sliceFrame.w );
	if( Invert )
		v = 1.0 - v;
	fragColor = vec4( color.rgb, 1.0 ) * color.a * v * coverage;
}
)";

// Uniforms of the same name in tahua::waveShapes.
const char* const shapeFloats[] = { "Shape", "Scale", "Thickness", "Softness", "Echoes", "Trail", "Spread", "Attack", "Direction", "CenterX", "CenterY", "Sides" };

}//End anonymous namespace

SliceWaves::SliceWaves()
{
	SetFragmentShader( unusedFragmentShader );

	AddSlicesFileParam();
	// The parameters of Tahua Waves, in its order, with Contour as an extra shape.
	AddParam( ParamOption::Create( "Shape", { { "Rings", 0.0f }, { "Ring", 1.0f }, { "Noise", 2.0f }, { "Pulse", 3.0f }, { "Lateral", 4.0f }, { "Flat", 5.0f }, { "Line", 6.0f }, { "Radar", 7.0f }, { "Lines", 8.0f }, { "Contour", 9.0f } } ) );
	AddParam( sync = ParamBool::Create( "Sync", false ) );
	AddParam( beats = ParamOption::Create( "Beats", { { "1/4", 0.25f }, { "1/2", 0.5f }, { "1", 1.0f }, { "2", 2.0f }, { "4", 4.0f }, { "8", 8.0f }, { "16", 16.0f } }, 2 ) );
	AddParam( speed = ParamRange::Create( "Speed", 0.5f, { 0.0f, 4.0f } ) );
	AddParam( ParamRange::Create( "Scale", 1.0f, { 0.1f, 4.0f } ) );
	AddParam( ParamRange::Create( "Thickness", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Softness", 0.0f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Echoes", 8.0f, { 0.0f, 12.0f } ) );
	AddParam( ParamRange::Create( "Trail", 0.4f, { 0.0f, 0.9f } ) );
	AddParam( ParamRange::Create( "Spread", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Attack", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Direction", 270.0f, { 0.0f, 360.0f } ) );
	AddParam( ParamBool::Create( "Mirror", false ) );
	AddParam( Param::Create( "CenterX", 0.5f ) );
	AddParam( Param::Create( "CenterY", 0.5f ) );
	AddParam( ParamBool::Create( "Invert", false ) );
	AddParam( ParamOption::Create( "Sides", { { "Circle", 0.0f }, { "Triangle", 3.0f }, { "Square", 4.0f }, { "Pentagon", 5.0f }, { "Hexagon", 6.0f }, { "Octagon", 8.0f } } ) );
	// When each slice runs its cycle.
	AddParam( offset = ParamOption::Create( "Offset", { { "Random", (float)OFFSET_RANDOM }, { "Order", (float)OFFSET_ORDER }, { "Neighbour", (float)OFFSET_NEIGHBOUR } } ) );
	AddParam( ParamRange::Create( "Delay", 0.0f, { 0.0f, 1.0f } ) );
	AddParam( Param::Create( "OriginX", 0.5f ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Origin X", false );
	AddParam( Param::Create( "OriginY", 0.5f ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Origin Y", false );
	AddHueColorParam( "Color" );
	AddSelectionParams();
}

FFResult SliceWaves::Init()
{
	std::string fragment = std::string( waveFragmentHeader ) + tahua::waveShapes + waveFragmentMain;
	if( !waveShader.Compile( waveVertexShader, fragment.c_str() ) )
		return FF_FAIL;
	return FF_SUCCESS;
}

void SliceWaves::Update()
{
	bool changed = UpdateSelection().changed;
	float mode   = offset->GetValue();
	float x      = GetParam( "OriginX" )->GetValue();
	float y      = GetParam( "OriginY" )->GetValue();
	if( changed || mode != builtOffset || ( mode == OFFSET_NEIGHBOUR && ( x != builtOriginX || y != builtOriginY ) ) )
		BuildSlices();
}

void SliceWaves::BuildSlices()
{
	builtOffset  = offset->GetValue();
	builtOriginX = GetParam( "OriginX" )->GetValue();
	builtOriginY = GetParam( "OriginY" )->GetValue();
	float width  = (float)currentViewport.width;
	float height = (float)currentViewport.height;

	// The selected slices, in pixels.
	std::vector< Shape > shapes;
	std::vector< const PresetSlice* > sources;
	for( const PresetScreen& screen : preset.screens )
	{
		if( !IsSelected( screen ) )
			continue;
		for( const PresetSlice& slice : screen.slices )
		{
			if( !IsSelected( screen, slice ) )
				continue;
			Shape shape;
			shape.low  = { 1e30f, 1e30f };
			shape.high = { -1e30f, -1e30f };
			std::vector< Point > all;
			for( const std::vector< float >& ring : slice.contours )
			{
				std::vector< Point > points;
				for( size_t i = 0; i + 1 < ring.size(); i += 2 )
				{
					Point p = { ring[ i ] * width, ring[ i + 1 ] * height };
					points.push_back( p );
					shape.low  = { std::min( shape.low.x, p.x ), std::min( shape.low.y, p.y ) };
					shape.high = { std::max( shape.high.x, p.x ), std::max( shape.high.y, p.y ) };
				}
				if( points.size() >= 3 )
				{
					all.insert( all.end(), points.begin(), points.end() );
					shape.rings.push_back( std::move( points ) );
				}
			}
			if( shape.rings.empty() )
				continue;
			shape.frame = SmallestFrame( all );
			for( size_t i = 0; i + 1 < slice.samples.size(); i += 2 )
				shape.depth = std::max( shape.depth, shape.Distance( { slice.samples[ i ] * width, slice.samples[ i + 1 ] * height } ) );
			shape.seed = (unsigned int)( &screen - preset.screens.data() ) * 65536u + (unsigned int)( &slice - screen.slices.data() );
			shapes.push_back( std::move( shape ) );
		}
	}

	if( builtOffset == OFFSET_NEIGHBOUR && !shapes.empty() )
		NeighbourOffsets( shapes, { builtOriginX * width, builtOriginY * height } );
	else
		for( size_t i = 0; i < shapes.size(); ++i )
			shapes[ i ].offset = builtOffset == OFFSET_ORDER ? (float)i / (float)shapes.size() : Hash( shapes[ i ].seed );

	// Per slice: 6 vertices of bounds (4), corner (2), edge range (2), frame (4), angle, offset,
	// depth (3); and its edges.
	std::vector< float > vertices, edges;
	for( const Shape& shape : shapes )
	{
		float first = (float)( edges.size() / 4 );
		for( const auto& ring : shape.rings )
			for( size_t i = 0; i < ring.size(); ++i )
			{
				const Point &a = ring[ i ], &b = ring[ ( i + 1 ) % ring.size() ];
				edges.insert( edges.end(), { a.x / width, a.y / height, b.x / width, b.y / height } );
			}
		float count = (float)( edges.size() / 4 ) - first;
		const Frame& f = shape.frame;
		const float corners[ 6 ][ 2 ] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
		for( const auto& corner : corners )
			vertices.insert( vertices.end(), { shape.low.x / width, shape.low.y / height, shape.high.x / width, shape.high.y / height,
			                                   corner[ 0 ], corner[ 1 ], first, count,
			                                   f.cx, f.cy, f.hx, f.hy, f.angle, shape.offset, shape.depth } );
	}
	ReleaseSlices();
	if( !vertices.empty() )
		UploadSlices( vertices, edges );
}

FFResult SliceWaves::Render( ProcessOpenGLStruct* )
{
	// Same clock as Tahua Waves: barPhase runs 0 -> 1 over each bar of 4 beats, a big drop means a new bar.
	if( barPhase + 0.5f < lastBarPhase )
		bars += 1.0;
	lastBarPhase = barPhase;
	if( sync->GetValue() )
		cycles = ( bars + barPhase ) * 4.0 / beats->GetRealValue();
	else
		cycles += deltaTime * speed->GetValue();

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

	ScopedShaderBinding shaderBinding( waveShader.GetGLID() );
	ScopedSamplerActivation activateSampler0( 0 );
	Scoped2DTextureBinding edgesBinding( edgesTexture );
	waveShader.Set( "edges", 0 );
	waveShader.Set( "resolution", (float)currentViewport.width, (float)currentViewport.height );
	waveShader.Set( "cycle", (float)( cycles - std::floor( cycles ) ) );
	waveShader.Set( "noiseTime", (float)std::fmod( cycles * 0.25, 1000.0 ) );
	waveShader.Set( "delay", GetParam( "Delay" )->GetValue() );
	waveShader.Set( "color", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], GetParam( "Color_alpha" )->GetValue() );
	for( const char* name : shapeFloats )
		waveShader.Set( name, GetParam( name )->GetValue() );
	waveShader.Set( "Mirror", GetParam( "Mirror" )->GetValue() != 0.0f ? 1 : 0 );
	waveShader.Set( "Invert", GetParam( "Invert" )->GetValue() != 0.0f ? 1 : 0 );

	// The same area on several screens overlaps: keep the brightest instead of adding them up.
	glEnable( GL_BLEND );
	glBlendEquation( GL_MAX );
	ScopedVAOBinding vaoBinding( vao );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	glBlendEquation( GL_FUNC_ADD );
	glDisable( GL_BLEND );
	return FF_SUCCESS;
}

void SliceWaves::Clean()
{
	ReleaseSlices();
	ClearPreset();
	waveShader.FreeGLResources();
}

void SliceWaves::UploadSlices( const std::vector< float >& vertices, const std::vector< float >& edges )
{
	vertexCount = (GLsizei)( vertices.size() / VERTEX_FLOATS );

	// Padded to whole rows of TEXELS_PER_ROW texels.
	int texelCount = (int)( edges.size() / 4 );
	int rows       = ( texelCount + TEXELS_PER_ROW - 1 ) / TEXELS_PER_ROW;
	std::vector< float > texels( edges );
	texels.resize( (size_t)rows * TEXELS_PER_ROW * 4, 0.0f );
	glGenTextures( 1, &edgesTexture );
	{
		Scoped2DTextureBinding textureBinding( edgesTexture );
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
	const GLsizei stride       = VERTEX_FLOATS * sizeof( float );
	const int sizes[]          = { 4, 2, 2, 4, 3 };
	size_t start               = 0;
	for( GLuint attribute = 0; attribute < 5; ++attribute )
	{
		glEnableVertexAttribArray( attribute );
		glVertexAttribPointer( attribute, sizes[ attribute ], GL_FLOAT, GL_FALSE, stride, (void*)( start * sizeof( float ) ) );
		start += sizes[ attribute ];
	}
}

void SliceWaves::ReleaseSlices()
{
	glDeleteTextures( 1, &edgesTexture );
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	edgesTexture = vao = vbo = 0;
	vertexCount             = 0;
}
