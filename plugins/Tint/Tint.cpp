#include "Tint.h"
using namespace ffglqs;

static PluginInstance pluginInstance = Effect::CreatePlugin< Tint >( tahua::MakePluginInfo() );

// Each parameter added in the constructor becomes a uniform of the same name.
// Also available: inputTexture, i_uv, resolution, time, deltaTime, frame, bpm, phase.
static const char fragmentShader[] = R"(
// Sam Hocevar's branchless RGB <-> HSV conversions.
vec3 rgb2hsv( vec3 c )
{
	vec4 K = vec4( 0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0 );
	vec4 p = mix( vec4( c.bg, K.wz ), vec4( c.gb, K.xy ), step( c.b, c.g ) );
	vec4 q = mix( vec4( p.xyw, c.r ), vec4( c.r, p.yzx ), step( p.x, c.r ) );
	float d = q.x - min( q.w, q.y );
	float e = 1.0e-10;
	return vec3( abs( q.z + ( q.w - q.y ) / ( 6.0 * d + e ) ), d / ( q.x + e ), q.x );
}

vec3 hsv2rgb( vec3 c )
{
	vec4 K = vec4( 1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0 );
	vec3 p = abs( fract( c.xxx + K.xyz ) * 6.0 - K.www );
	return c.z * mix( K.xxx, clamp( p - K.xxx, 0.0, 1.0 ), c.y );
}

void main()
{
	vec4 color = texture( inputTexture, i_uv );
	// Resolume textures are premultiplied: unpremultiply, process, premultiply again.
	if( color.a > 0.0 )
		color.rgb /= color.a;

	vec3 hsv = rgb2hsv( color.rgb );
	hsv.x    = fract( hsv.x + Hue );
	hsv.y    = clamp( hsv.y * Saturation, 0.0, 1.0 );
	vec3 rgb = hsv2rgb( hsv );
	if( Invert )
		rgb = vec3( 1.0 ) - rgb;

	fragColor = vec4( clamp( rgb, 0.0, 1.0 ) * color.a, color.a );
}
)";

Tint::Tint()
{
	SetFragmentShader( fragmentShader );
	AddParam( Param::Create( "Hue", 0.0f ) );
	AddParam( ParamRange::Create( "Saturation", 1.0f, { 0.0f, 2.0f } ) );
	AddParam( ParamBool::Create( "Invert", false ) );
}
