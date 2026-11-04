#include "SliceEdges.h"
#include <algorithm>
#include <cmath>
using namespace ffglqs;
using namespace ffglex;

static PluginInstance pluginInstance = Effect::CreatePlugin< SliceEdges >( tahua::MakePluginInfo() );

namespace
{
// Each edge takes 2 texels: start x, y, end x, y; then where it starts and ends along its ring
// (0 to 1) and the ring's perimeter in pixels. Stored in rows of this many texels.
const int TEXELS_PER_ROW  = 1024;
const int FLOATS_PER_EDGE = 8;
const int VERTEX_FLOATS   = 9;

enum Trace
{
	TRACE_OFF,
	TRACE_FILL,
	TRACE_COMET
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

// Unused: the quickstart compiles it, but drawing is done by the shaders below.
const char unusedFragmentShader[] = R"(
void main()
{
	fragColor = vec4( 0.0 );
}
)";

// One quad per slice: its bounding box, grown by the margin the border can reach.
const char edgeVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec4 bounds;// min x, y, max x, y, normalised
layout( location = 1 ) in vec2 corner;
layout( location = 2 ) in vec2 edgeRange;// first edge, edge count
layout( location = 3 ) in float offset;  // 0 to 1, fixed per slice
uniform vec2 resolution;
uniform float margin;
uniform float cycle;
uniform float spread;
out vec2 pixel;
flat out int firstEdge;
flat out int edgeCount;
flat out float phase;

void main()
{
	vec2 low    = bounds.xy * resolution - margin;
	vec2 high   = bounds.zw * resolution + margin;
	pixel       = mix( low, high, corner );
	gl_Position = vec4( pixel / resolution * 2.0 - 1.0, 0.0, 1.0 );
	firstEdge   = int( edgeRange.x );
	edgeCount   = int( edgeRange.y );
	phase       = fract( cycle + spread * offset );
}
)";

// Signed distance to the slice outline, in pixels (negative inside), then a band around it.
const char edgeFragmentShader[] = R"(
#version 410 core
uniform sampler2D edges;
uniform vec2 resolution;
uniform float halfWidth;
uniform float overscan;
uniform float softness;
uniform vec4 color;
uniform int trace;// 0 off, 1 fill, 2 comet
uniform float traceLength;
in vec2 pixel;
flat in int firstEdge;
flat in int edgeCount;
flat in float phase;
out vec4 fragColor;

void main()
{
	float nearest   = 1e30;
	float along     = 0.0;// where the nearest point is along its ring, 0 to 1
	float perimeter = 1.0;
	bool inside     = false;
	for( int i = 0; i < edgeCount; ++i )
	{
		int k    = ( firstEdge + i ) * 2;
		vec4 e   = texelFetch( edges, ivec2( k % 1024, k / 1024 ), 0 );
		vec2 a   = e.xy * resolution;
		vec2 b   = e.zw * resolution;
		vec2 ab  = b - a;
		vec2 ap  = pixel - a;
		float t  = clamp( dot( ap, ab ) / max( dot( ab, ab ), 1e-12 ), 0.0, 1.0 );
		vec2 q   = ap - ab * t;
		float d2 = dot( q, q );
		if( d2 < nearest )
		{
			vec4 ring = texelFetch( edges, ivec2( ( k + 1 ) % 1024, ( k + 1 ) / 1024 ), 0 );
			nearest   = d2;
			along     = mix( ring.x, ring.y, t );
			perimeter = ring.z;
		}
		if( ( a.y > pixel.y ) != ( b.y > pixel.y ) && pixel.x < a.x + ( pixel.y - a.y ) * ab.x / ab.y )
			inside = !inside;
	}
	float d = sqrt( nearest ) * ( inside ? -1.0 : 1.0 );

	// At least one pixel of ramp, for antialiasing; a zero width draws nothing.
	float ramp     = max( softness, 1.0 );
	float coverage = smoothstep( halfWidth + ramp * 0.5, halfWidth - ramp * 0.5, abs( d - overscan ) );
	coverage *= clamp( halfWidth * 2.0, 0.0, 1.0 );

	// Fill: the outline is drawn up to phase. Comet: a stroke whose head is at phase, its tail
	// fading over traceLength of the ring behind it.
	if( trace == 1 )
		coverage *= clamp( ( phase - along ) * perimeter + 0.5, 0.0, 1.0 );
	else if( trace == 2 )
	{
		float behind = fract( phase - along );
		coverage *= traceLength >= 1.0 ? 1.0 : clamp( 1.0 - behind / max( traceLength, 1e-4 ), 0.0, 1.0 );
	}
	if( coverage <= 0.0 )
		discard;
	fragColor = vec4( color.rgb, 1.0 ) * color.a * coverage;
}
)";

}//End anonymous namespace

SliceEdges::SliceEdges()
{
	SetFragmentShader( unusedFragmentShader );

	AddSlicesFileParam();
	// In pixels of the layer. Names become shader uniforms, so the spaced versions are display names only.
	AddParam( ParamRange::Create( "Width", 2.0f, { 0.0f, 50.0f } ) );
	AddParam( ParamRange::Create( "Overscan", 0.0f, { -50.0f, 50.0f } ) );
	AddParam( ParamRange::Create( "Softness", 0.0f, { 0.0f, 50.0f } ) );
	AddHueColorParam( "Color" );
	AddParam( pulseBeats = ParamOption::Create( "PulseBeats", { { "1/4", 0.25f }, { "1/2", 0.5f }, { "1", 1.0f }, { "2", 2.0f }, { "4", 4.0f }, { "8", 8.0f }, { "16", 16.0f } }, 2 ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Pulse Beats", false );
	AddParam( ParamRange::Create( "PulseWidth", 0.0f, { 0.0f, 50.0f } ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Pulse Width", false );
	AddParam( ParamRange::Create( "PulseFade", 0.0f, { 0.0f, 1.0f } ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Pulse Fade", false );
	AddParam( ParamRange::Create( "Decay", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( trace = ParamOption::Create( "Trace", { { "Off", (float)TRACE_OFF }, { "Fill", (float)TRACE_FILL }, { "Comet", (float)TRACE_COMET } } ) );
	AddParam( traceBeats = ParamOption::Create( "TraceBeats", { { "1/4", 0.25f }, { "1/2", 0.5f }, { "1", 1.0f }, { "2", 2.0f }, { "4", 4.0f }, { "8", 8.0f }, { "16", 16.0f } }, 4 ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Trace Beats", false );
	AddParam( ParamRange::Create( "Length", 0.25f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Spread", 0.0f, { 0.0f, 1.0f } ) );
	AddSelectionParams();
}

FFResult SliceEdges::Init()
{
	if( !edgeShader.Compile( edgeVertexShader, edgeFragmentShader ) )
		return FF_FAIL;
	return FF_SUCCESS;
}

void SliceEdges::Update()
{
	if( !UpdateSelection().changed )
		return;

	// Per slice: 6 vertices of bounds (4), corner (2), edge range (2), trace offset (1); and its edges.
	std::vector< float > vertices, edges;
	float width  = (float)currentViewport.width;
	float height = (float)currentViewport.height;
	for( const PresetScreen& screen : preset.screens )
	{
		if( !IsSelected( screen ) )
			continue;
		for( const PresetSlice& slice : screen.slices )
		{
			if( !IsSelected( screen, slice ) )
				continue;
			float first = (float)( edges.size() / FLOATS_PER_EDGE );
			float low[ 2 ] = { 1e30f, 1e30f }, high[ 2 ] = { -1e30f, -1e30f };
			for( const std::vector< float >& ring : slice.contours )
			{
				size_t n = ring.size() / 2;
				// Lengths in pixels of the layer, so that the trace runs at an even speed.
				std::vector< float > lengths;
				float perimeter = 0.0f;
				for( size_t i = 0; i < n; ++i )
				{
					size_t j = ( i + 1 ) % n;
					float dx = ( ring[ 2 * j ] - ring[ 2 * i ] ) * width;
					float dy = ( ring[ 2 * j + 1 ] - ring[ 2 * i + 1 ] ) * height;
					lengths.push_back( std::sqrt( dx * dx + dy * dy ) );
					perimeter += lengths.back();
				}
				if( perimeter <= 0.0f )
					continue;
				float along = 0.0f;
				for( size_t i = 0; i < n; ++i )
				{
					size_t j = ( i + 1 ) % n;
					edges.insert( edges.end(), { ring[ 2 * i ], ring[ 2 * i + 1 ], ring[ 2 * j ], ring[ 2 * j + 1 ],
					                             along / perimeter, ( along + lengths[ i ] ) / perimeter, perimeter, 0.0f } );
					along += lengths[ i ];
					for( int c = 0; c < 2; ++c )
					{
						low[ c ]  = std::min( low[ c ], ring[ 2 * i + c ] );
						high[ c ] = std::max( high[ c ], ring[ 2 * i + c ] );
					}
				}
			}
			float count = (float)( edges.size() / FLOATS_PER_EDGE ) - first;
			if( count == 0.0f )
				continue;
			// Fixed per slice: where it starts its trace when Spread is up.
			float offset = Hash( (unsigned int)( &screen - preset.screens.data() ) * 65536u + (unsigned int)( &slice - screen.slices.data() ) );
			const float corners[ 6 ][ 2 ] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
			for( const auto& corner : corners )
				vertices.insert( vertices.end(), { low[ 0 ], low[ 1 ], high[ 0 ], high[ 1 ], corner[ 0 ], corner[ 1 ], first, count, offset } );
		}
	}
	ReleaseSlices();
	if( !vertices.empty() )
		UploadSlices( vertices, edges );
}

double SliceEdges::CountBeats()
{
	// barPhase runs 0 -> 1 over each bar of 4 beats: a big drop means a new bar.
	if( barPhase + 0.5f < lastBarPhase )
		bars += 1.0;
	lastBarPhase = barPhase;
	return ( bars + barPhase ) * 4.0;
}

float SliceEdges::PulseEnvelope( double beats )
{
	double pulses = beats / pulseBeats->GetRealValue();
	float progress = (float)( pulses - std::floor( pulses ) );
	// Decay 1 fades linearly over the whole period; lower values drop faster after the hit.
	float decay = GetParam( "Decay" )->GetValue();
	return std::pow( 1.0f - progress, 1.0f + 9.0f * ( 1.0f - decay ) );
}

FFResult SliceEdges::Render( ProcessOpenGLStruct* )
{
	double beats   = CountBeats();
	float envelope = PulseEnvelope( beats );
	double cycles  = beats / traceBeats->GetRealValue();

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( vertexCount == 0 )
		return FF_SUCCESS;

	float halfWidth = 0.5f * ( GetParam( "Width" )->GetValue() + GetParam( "PulseWidth" )->GetValue() * envelope );
	float overscan  = GetParam( "Overscan" )->GetValue();
	float softness  = GetParam( "Softness" )->GetValue();
	float opacity   = 1.0f - GetParam( "PulseFade" )->GetValue() * ( 1.0f - envelope );

	float hue = GetParam( "Color" )->GetValue();
	float rgb[ 3 ];
	HSVtoRGB( hue == 1.0f ? 0.0f : hue,
	          GetParam( "Color_saturation" )->GetValue(),
	          GetParam( "Color_brightness" )->GetValue(),
	          rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );

	ScopedShaderBinding shaderBinding( edgeShader.GetGLID() );
	ScopedSamplerActivation activateSampler0( 0 );
	Scoped2DTextureBinding edgesBinding( edgesTexture );
	edgeShader.Set( "edges", 0 );
	edgeShader.Set( "resolution", (float)currentViewport.width, (float)currentViewport.height );
	edgeShader.Set( "margin", std::abs( overscan ) + halfWidth + softness + 1.0f );
	edgeShader.Set( "halfWidth", halfWidth );
	edgeShader.Set( "overscan", overscan );
	edgeShader.Set( "softness", softness );
	edgeShader.Set( "color", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], GetParam( "Color_alpha" )->GetValue() * opacity );
	edgeShader.Set( "trace", (int)trace->GetRealValue() );
	edgeShader.Set( "cycle", (float)( cycles - std::floor( cycles ) ) );
	edgeShader.Set( "traceLength", GetParam( "Length" )->GetValue() );
	edgeShader.Set( "spread", GetParam( "Spread" )->GetValue() );

	// Slices that touch, or the same slice on several screens, overlap: keep the brightest
	// instead of adding them up, so shared borders are not drawn twice as strong.
	glEnable( GL_BLEND );
	glBlendEquation( GL_MAX );
	ScopedVAOBinding vaoBinding( vao );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	glBlendEquation( GL_FUNC_ADD );
	glDisable( GL_BLEND );
	return FF_SUCCESS;
}

void SliceEdges::Clean()
{
	ReleaseSlices();
	ClearPreset();
	edgeShader.FreeGLResources();
}

void SliceEdges::UploadSlices( const std::vector< float >& vertices, const std::vector< float >& edges )
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
	const GLsizei stride = VERTEX_FLOATS * sizeof( float );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 4, GL_FLOAT, GL_FALSE, stride, (void*)0 );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, stride, (void*)( 4 * sizeof( float ) ) );
	glEnableVertexAttribArray( 2 );
	glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, stride, (void*)( 6 * sizeof( float ) ) );
	glEnableVertexAttribArray( 3 );
	glVertexAttribPointer( 3, 1, GL_FLOAT, GL_FALSE, stride, (void*)( 8 * sizeof( float ) ) );
}

void SliceEdges::ReleaseSlices()
{
	glDeleteTextures( 1, &edgesTexture );
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	edgesTexture = vao = vbo = 0;
	vertexCount             = 0;
}
