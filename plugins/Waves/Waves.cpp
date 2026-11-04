#include "Waves.h"
#include <cmath>
using namespace ffglqs;

static PluginInstance pluginInstance = Source::CreatePlugin< Waves >( tahua::MakePluginInfo() );

// Each parameter added in the constructor becomes a uniform of the same name.
// Also available: i_uv, resolution, time, deltaTime, frame, bpm, phase.
// Output is a white mask carried by the alpha channel (premultiplied).
static const char fragmentShader[] = R"(
uniform float progress; // Position in the current cycle, 0 -> 1, set by Update().
uniform float noiseTime;// Continuous time for the Noise shape, set by Update().

// Ian McEwan / Ashima Arts 3D simplex noise (MIT).
vec3 mod289( vec3 x ) { return x - floor( x * ( 1.0 / 289.0 ) ) * 289.0; }
vec4 mod289( vec4 x ) { return x - floor( x * ( 1.0 / 289.0 ) ) * 289.0; }
vec4 permute( vec4 x ) { return mod289( ( ( x * 34.0 ) + 1.0 ) * x ); }
vec4 taylorInvSqrt( vec4 r ) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise( vec3 v )
{
	const vec2 C = vec2( 1.0 / 6.0, 1.0 / 3.0 );
	const vec4 D = vec4( 0.0, 0.5, 1.0, 2.0 );

	vec3 i  = floor( v + dot( v, C.yyy ) );
	vec3 x0 = v - i + dot( i, C.xxx );

	vec3 g  = step( x0.yzx, x0.xyz );
	vec3 l  = 1.0 - g;
	vec3 i1 = min( g.xyz, l.zxy );
	vec3 i2 = max( g.xyz, l.zxy );

	vec3 x1 = x0 - i1 + C.xxx;
	vec3 x2 = x0 - i2 + C.yyy;
	vec3 x3 = x0 - D.yyy;

	i      = mod289( i );
	vec4 p = permute( permute( permute( i.z + vec4( 0.0, i1.z, i2.z, 1.0 ) ) + i.y + vec4( 0.0, i1.y, i2.y, 1.0 ) ) + i.x + vec4( 0.0, i1.x, i2.x, 1.0 ) );

	float n_ = 0.142857142857;
	vec3 ns  = n_ * D.wyz - D.xzx;

	vec4 j  = p - 49.0 * floor( p * ns.z * ns.z );
	vec4 x_ = floor( j * ns.z );
	vec4 y_ = floor( j - 7.0 * x_ );

	vec4 x = x_ * ns.x + ns.yyyy;
	vec4 y = y_ * ns.x + ns.yyyy;
	vec4 h = 1.0 - abs( x ) - abs( y );

	vec4 b0 = vec4( x.xy, y.xy );
	vec4 b1 = vec4( x.zw, y.zw );
	vec4 s0 = floor( b0 ) * 2.0 + 1.0;
	vec4 s1 = floor( b1 ) * 2.0 + 1.0;
	vec4 sh = -step( h, vec4( 0.0 ) );

	vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
	vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

	vec3 p0 = vec3( a0.xy, h.x );
	vec3 p1 = vec3( a0.zw, h.y );
	vec3 p2 = vec3( a1.xy, h.z );
	vec3 p3 = vec3( a1.zw, h.w );

	vec4 norm = taylorInvSqrt( vec4( dot( p0, p0 ), dot( p1, p1 ), dot( p2, p2 ), dot( p3, p3 ) ) );
	p0 *= norm.x;
	p1 *= norm.y;
	p2 *= norm.z;
	p3 *= norm.w;

	vec4 m = max( 0.6 - vec4( dot( x0, x0 ), dot( x1, x1 ), dot( x2, x2 ), dot( x3, x3 ) ), 0.0 );
	m      = m * m;
	return 42.0 * dot( m * m, vec4( dot( p0, x0 ), dot( p1, x1 ), dot( p2, x2 ), dot( p3, x3 ) ) );
}

// 1 inside (d < 0), 0 outside, with an edge at least one pixel wide.
float fill( float d, float soft )
{
	float w = max( fwidth( d ), soft );
	return 1.0 - smoothstep( -w, w, d );
}

// Expanding fronts: wave k starts k * gap after the first one, every wave reaches
// Scale by the end of the cycle, and each later wave paints a darker level inside the previous one.
float pulse( float r, float reach, float soft )
{
	int n     = int( Echoes );
	float gap = n > 0 ? Trail / float( n ) : 0.0;
	float run = 1.0 - float( n ) * gap;
	float v   = 0.0;
	for( int k = 0; k <= 12; ++k )
	{
		if( k > n )
			break;
		float t      = ( progress - float( k ) * gap ) / run;
		float radius = t > 0.0 ? reach * ( 1.0 - pow( 1.0 - min( t, 1.0 ), 2.0 ) ) : -1.0;
		float level  = n > 0 ? 1.0 - float( k ) / float( n ) : 1.0;
		v            = mix( v, level, fill( r - radius, soft ) );
	}
	return v;
}

// Rings, Ring and Line at phase t of their cycle. r: distance to the centre, s: position along
// Direction (Line), extent: half the frame along Direction.
float thin( int shape, float t, float r, float s, float extent, float reach, float soft )
{
	if( shape == 0 )// Rings: concentric bands moving outward, Thickness = white share of each period.
	{
		float freq = Scale * 4.0;
		float b    = fract( r * freq - t + Thickness * 0.5 );
		return fill( ( abs( b - 0.5 ) - Thickness * 0.5 ) / freq, soft );
	}
	float halfWidth = Thickness * 0.075;
	if( shape == 1 )// Ring: one ring growing from the centre to Scale.
	{
		float radius = mix( -halfWidth, reach + halfWidth, t );
		return fill( abs( r - radius ) - halfWidth, soft );
	}
	// Line: one line crossing the whole frame towards Direction, from edge to edge. Mirror: two
	// lines leaving the centre in opposite directions.
	float position = mix( Mirror ? -halfWidth : -extent - halfWidth, extent + halfWidth, t );
	return fill( abs( ( Mirror ? abs( s ) : s ) - position ) - halfWidth, soft );
}

// The front plus Echoes copies following it, fading out, the last one Trail of a cycle behind.
// Copies wrap around the cycle, so the loop stays seamless.
float trail( int shape, float r, float s, float extent, float reach, float soft )
{
	int n     = int( Echoes );
	float gap = n > 0 ? Trail / float( n ) : 0.0;
	float v   = 0.0;
	for( int k = 0; k <= 12; ++k )
	{
		if( k > n )
			break;
		float level = 1.0 - float( k ) / float( n + 1 );
		v           = max( v, thin( shape, fract( progress - float( k ) * gap ), r, s, extent, reach, soft ) * level );
	}
	return v;
}

void main()
{
	float aspect = resolution.x / resolution.y;
	vec2 p       = ( i_uv - vec2( CenterX, CenterY ) ) * vec2( aspect, 1.0 );// Screen height = 1.
	float r      = length( p );
	float soft   = Softness * 0.2;
	float reach  = Scale * 1.05;// Scale 1 reaches the corners of a 16:9 frame from the centre.
	int shape    = int( Shape );
	float v      = 0.0;

	if( shape == 0 || shape == 1 || shape == 6 )// Rings, Ring, Line, with their echoes.
	{
		float angle  = radians( Direction );
		vec2 dir     = vec2( cos( angle ), sin( angle ) );
		float s      = dot( ( i_uv - 0.5 ) * vec2( aspect, 1.0 ), dir );
		float extent = 0.5 * ( aspect * abs( dir.x ) + abs( dir.y ) );
		v            = trail( shape, r, s, extent, reach, soft );
	}
	else if( shape == 2 )// Noise: a contour band of slowly evolving noise.
	{
		vec2 q  = p * Scale * 2.0;
		float n = snoise( vec3( q, noiseTime ) ) + 0.5 * snoise( vec3( q * 2.0 + 17.0, noiseTime * 1.3 ) );
		v       = fill( abs( n ) - Thickness * 0.3, soft );
	}
	else if( shape == 3 )// Pulse: a disc snapping open, followed by darker echoes.
	{
		v = pulse( r, reach, soft );
	}
	else if( shape == 4 )// Lateral: same as Pulse, the front being the overlap of two discs centred left and right.
	{
		vec2 c = vec2( Spread, 0.0 );
		v      = pulse( max( length( p - c ), length( p + c ) ) - Spread, reach, soft );
	}
	else// Flat: the whole frame fades in over Attack, then out over the rest of the cycle.
	{
		float a = Attack;
		v       = progress < a ? smoothstep( 0.0, a, progress ) : 1.0 - smoothstep( a, 1.0, progress );
	}

	if( Invert )
		v = 1.0 - v;
	v         = clamp( v, 0.0, 1.0 );
	fragColor = vec4( v, v, v, v );
}
)";

Waves::Waves()
{
	SetFragmentShader( fragmentShader );
	AddParam( ParamOption::Create( "Shape", { { "Rings", 0.0f }, { "Ring", 1.0f }, { "Noise", 2.0f }, { "Pulse", 3.0f }, { "Lateral", 4.0f }, { "Flat", 5.0f }, { "Line", 6.0f } } ) );
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
}

void Waves::Update()
{
	// barPhase runs 0 -> 1 over each bar of 4 beats: a big drop means a new bar.
	if( barPhase + 0.5f < lastBarPhase )
		bars += 1.0;
	lastBarPhase = barPhase;

	if( sync->GetValue() )
		cycles = ( bars + barPhase ) * 4.0 / beats->GetRealValue();
	else
		cycles += deltaTime * speed->GetValue();

	shader.Set( "progress", (float)( cycles - std::floor( cycles ) ) );
	shader.Set( "noiseTime", (float)std::fmod( cycles * 0.25, 1000.0 ) );
}
