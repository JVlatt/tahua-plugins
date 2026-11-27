#include "Stage3D.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <locale>
#include <sstream>
#include <ffglex/FFGLScopedFBOBinding.h>
using namespace ffglqs;
using namespace ffglex;

static PluginInstance pluginInstance = Effect::CreatePlugin< Stage3D >( tahua::MakePluginInfo() );

namespace
{
enum Mode
{
	MODE_TEXTURE,
	MODE_COLOR,
	MODE_CHASE
};

// Ready-made Chase moves: Move X, Y, Z, Rotate X, Y, Z, Scale, then the curve.
struct MovePreset
{
	const char* name;
	float move[ 7 ];
	int curve;
	float elasticity, speed;
};
const MovePreset MOVE_PRESETS[] = {
	{ "Pop Out", { 0.0f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 1.2f }, ChaseLevels::CURVE_ELASTIC, 0.6f, 2.0f },
	{ "Flip", { 0.0f, 0.0f, 0.1f, 180.0f, 0.0f, 0.0f, 1.0f }, ChaseLevels::CURVE_SMOOTH, 0.0f, 1.5f },
	{ "Spin", { 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 180.0f, 1.0f }, ChaseLevels::CURVE_ELASTIC, 0.4f, 1.5f },
	{ "Fall", { 0.0f, -0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }, ChaseLevels::CURVE_BOUNCE, 0.5f, 2.0f },
	{ "Zoom", { 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 2.0f }, ChaseLevels::CURVE_ELASTIC, 0.5f, 2.0f },
	{ "Shrink", { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, ChaseLevels::CURVE_SMOOTH, 0.0f, 2.0f },
	{ "Tilt", { 0.0f, 0.0f, 0.1f, 0.0f, 45.0f, 0.0f, 1.0f }, ChaseLevels::CURVE_ELASTIC, 0.3f, 1.5f },
	{ "Lift", { 0.0f, 0.15f, 0.2f, -30.0f, 0.0f, 0.0f, 1.0f }, ChaseLevels::CURVE_ELASTIC, 0.2f, 1.5f },
};

// Ready-made materials, in the order of the Material parameters: Base Color (hue, saturation,
// brightness, alpha), Emissive, Emissive Color (same), Bounce, Spread, Specular, Glossiness,
// Fresnel, Metallic, Reflection, Iridescence.
struct MaterialPreset
{
	const char* name;
	float values[ 17 ];
};
const MaterialPreset MATERIAL_PRESETS[] = {
	{ "Default", { 0.0f, 0.0f, 0.25f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 0.05f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f } },
	{ "Chrome", { 0.0f, 0.0f, 0.9f, 1.0f, 0.6f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.05f, 2.0f, 0.9f, 0.3f, 1.0f, 0.9f, 0.0f } },
	{ "Mirror", { 0.0f, 0.0f, 1.0f, 1.0f, 0.3f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.05f, 3.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f } },
	{ "Gold", { 0.12f, 0.7f, 0.9f, 1.0f, 0.5f, 0.1f, 0.3f, 1.0f, 1.0f, 1.0f, 0.05f, 2.0f, 0.75f, 0.2f, 1.0f, 0.7f, 0.0f } },
	{ "Copper", { 0.05f, 0.65f, 0.8f, 1.0f, 0.5f, 0.05f, 0.3f, 1.0f, 1.0f, 1.0f, 0.05f, 1.5f, 0.6f, 0.2f, 1.0f, 0.6f, 0.0f } },
	{ "Brushed Steel", { 0.0f, 0.0f, 0.6f, 1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.05f, 1.0f, 0.35f, 0.1f, 1.0f, 0.6f, 0.0f } },
	{ "Glass", { 0.55f, 0.1f, 0.3f, 0.15f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 0.05f, 3.0f, 0.9f, 0.8f, 0.0f, 0.5f, 0.0f } },
	{ "Plastic", { 0.6f, 0.7f, 0.6f, 1.0f, 0.8f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 0.05f, 1.0f, 0.6f, 0.1f, 0.0f, 0.15f, 0.0f } },
	{ "Matte", { 0.0f, 0.0f, 0.15f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 0.05f, 0.2f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f } },
	{ "Pearl", { 0.1f, 0.1f, 0.9f, 1.0f, 0.6f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.05f, 1.0f, 0.7f, 0.4f, 0.0f, 0.3f, 0.4f } },
	{ "Holographic", { 0.75f, 0.3f, 0.8f, 0.6f, 0.8f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.05f, 2.0f, 0.8f, 0.5f, 0.5f, 0.5f, 1.0f } },
	{ "Neon Tube", { 0.0f, 0.0f, 0.05f, 1.0f, 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 6.0f, 0.1f, 0.5f, 0.8f, 0.2f, 0.0f, 0.1f, 0.0f } },
};

// The light's animations, on top of its parameters, in time with the host's tempo.
enum LightAnimation
{
	ANIMATION_NONE,
	ANIMATION_ORBIT,      // turns around the vertical axis through the origin
	ANIMATION_SWEEP,      // swings from side to side
	ANIMATION_PULSE,      // flashes at each cycle, then fades
	ANIMATION_STROBE,     // on at the start of each cycle only
	ANIMATION_FLICKER,    // dims at random
	ANIMATION_COLOR_CYCLE,// goes round the colour wheel and back
	ANIMATION_POLICE,     // red on one side, blue on the other, flashing
	ANIMATION_DISCO,      // orbits, jumping through 8 colours
};
const char* const ANIMATION_NAMES[] = { "None", "Orbit", "Sweep", "Pulse", "Strobe", "Flicker", "Color Cycle", "Police", "Disco" };
// Animation Rate: the length of a cycle, and how many fit in a 4/4 bar.
const char* const RATE_NAMES[] = { "4 Bars", "2 Bars", "1 Bar", "1/2 Bar", "1 Beat", "1/2 Beat", "1/4 Beat" };
const float CYCLES_PER_BAR[]   = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };

// Ready-made lights, in the order of the Light parameters: X, Y, Z, Intensity, Global
// Illumination, Point Color (hue, saturation, brightness, alpha), Global Color (same), Softness,
// Animation, Animation Rate, Animation Amount.
struct LightPreset
{
	const char* name;
	float values[ 17 ];
};
const LightPreset LIGHT_PRESETS[] = {
	{ "Flat", { 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Studio", { 0.6f, 1.0f, 1.5f, 2.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.75f, 1.0f, 0.7f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Soft Box", { 0.0f, 0.4f, 1.2f, 1.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.9f, 1.0f, 1.0f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Spotlight", { 0.0f, 1.5f, 0.6f, 4.0f, 0.0f, 0.1f, 0.15f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.2f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Backlight", { 0.0f, 0.5f, -1.5f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.4f, 1.0f, 0.3f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Sunset", { -1.5f, 0.2f, 0.8f, 3.0f, 1.0f, 0.07f, 0.8f, 1.0f, 1.0f, 0.75f, 0.4f, 0.5f, 1.0f, 0.6f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Night", { 0.8f, 1.2f, 0.6f, 2.5f, 1.0f, 0.58f, 0.4f, 1.0f, 1.0f, 0.64f, 0.7f, 0.35f, 1.0f, 0.5f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Neon", { -1.0f, 0.0f, 0.8f, 3.5f, 1.0f, 0.85f, 0.9f, 1.0f, 1.0f, 0.5f, 0.8f, 0.4f, 1.0f, 0.4f, ANIMATION_NONE, 2.0f, 1.0f } },
	{ "Orbit", { 1.2f, 0.8f, 0.0f, 3.0f, 1.0f, 0.08f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.6f, ANIMATION_ORBIT, 1.0f, 1.0f } },
	{ "Searchlight", { 0.0f, 1.0f, 1.0f, 4.0f, 1.0f, 0.12f, 0.1f, 1.0f, 1.0f, 0.62f, 0.5f, 0.2f, 1.0f, 0.2f, ANIMATION_SWEEP, 2.0f, 0.7f } },
	{ "Beat Pulse", { 0.0f, 0.5f, 1.2f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.6f, ANIMATION_PULSE, 4.0f, 0.9f } },
	{ "Strobe", { 0.0f, 0.5f, 1.2f, 5.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, ANIMATION_STROBE, 5.0f, 1.0f } },
	{ "Candle", { 0.3f, 0.2f, 0.6f, 2.5f, 1.0f, 0.07f, 0.75f, 1.0f, 1.0f, 0.05f, 0.6f, 0.3f, 1.0f, 0.8f, ANIMATION_FLICKER, 4.0f, 0.4f } },
	{ "Rainbow", { 0.0f, 0.6f, 1.2f, 3.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.4f, 1.0f, 0.6f, ANIMATION_COLOR_CYCLE, 0.0f, 1.0f } },
	{ "Police", { 1.0f, 0.6f, 0.8f, 4.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.25f, 1.0f, 0.4f, ANIMATION_POLICE, 3.0f, 0.8f } },
	{ "Disco", { 1.0f, 0.8f, 0.5f, 4.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.75f, 0.5f, 0.2f, 1.0f, 0.5f, ANIMATION_DISCO, 2.0f, 1.0f } },
};

const float PI = 3.14159265358979f;

// Unused: the quickstart compiles it, but drawing is done by ChaseLevels and the shaders below.
const char unusedFragmentShader[] = R"(
void main()
{
	fragColor = vec4( 0.0 );
}
)";


// One screen: a plane centred on its origin, aspect x 1 before the model matrix places and
// scales it. Slices sit on it and extrude backwards, towards -z: depth 1 is on the screen
// (front faces), depth 0 at the extrusion. Slice -1 is the screen's backdrop, just behind the
// extrusion. A slice's centre carries its family's offset in z, which moves the whole slice
// forwards or backwards. In Chase mode, each slice moves from where it is towards its target transform
// (move, turn, scale, in the screen's axes, around the slice's centre) as its level goes to 1,
// following the level's spring.
// Shared by the slices and their edges, after their #version line.
const char placeGLSL[] = R"(
uniform float aspect;
uniform float extrude;
uniform int mode;
uniform sampler2D levels;
uniform vec3 move;
uniform vec3 turn;
uniform float scale;

// Same order as the screens: around z, then x, then y.
mat3 Rotation( vec3 degrees )
{
	vec3 c = cos( radians( degrees ) ), s = sin( radians( degrees ) );
	mat3 x = mat3( 1.0, 0.0, 0.0, 0.0, c.x, s.x, 0.0, -s.x, c.x );
	mat3 y = mat3( c.y, 0.0, -s.y, 0.0, 1.0, 0.0, s.y, 0.0, c.y );
	mat3 z = mat3( c.z, s.z, 0.0, -s.z, c.z, 0.0, 0.0, 0.0, 1.0 );
	return y * x * z;
}

// Where a point of a slice (or of the backdrop, slice -1) is on its screen, chase move included.
vec3 Place( vec2 outputPosition, float depth, float slice, vec3 centre )
{
	vec2 screen   = vec2( aspect, 1.0 );
	vec3 position = vec3( ( outputPosition - 0.5 ) * screen, slice < 0.0 ? -extrude - 0.002 : ( depth - 1.0 ) * extrude + centre.z );
	if( slice >= 0.0 && mode == 2 )
	{
		float level = texelFetch( levels, ivec2( 0, int( slice ) ), 0 ).g;// the spring's: it overshoots
		vec3 pivot  = vec3( ( centre.xy - 0.5 ) * screen, centre.z );
		position    = pivot + move * level + Rotation( turn * level ) * ( ( position - pivot ) * max( mix( 1.0, scale, level ), 0.0 ) );
	}
	return position;
}
)";

const char screenVertexShader[] = R"(
layout( location = 0 ) in vec2 outputPosition;
layout( location = 1 ) in vec2 inputPosition;
layout( location = 2 ) in float slice;
layout( location = 3 ) in float depth;
layout( location = 4 ) in float side;
layout( location = 5 ) in vec3 centre;
layout( location = 6 ) in float family;// a random value per family name
uniform mat4 mvp;
uniform mat4 modelView;
uniform vec2 maxUV;
out vec2 uv;
out vec2 spillUV;// in the spill texture, which has no padding
out vec3 viewPosition;
out vec2 vOutput;// where on the screen, 0 to 1
out float vDepth;// 1 on the front faces, 0 at the back of the extrusion
flat out int vSlice;
flat out int vSide;
flat out float vFamily;

void main()
{
	vec3 position = Place( outputPosition, depth, slice, centre );
	gl_Position   = mvp * vec4( position, 1.0 );
	viewPosition  = ( modelView * vec4( position, 1.0 ) ).xyz;
	uv            = inputPosition * maxUV;
	spillUV       = inputPosition;
	vOutput       = outputPosition;
	vDepth        = depth;
	vSlice        = int( slice );
	vSide         = int( side );
	vFamily       = family;
}
)";

// The edges of the slices: their outline on the front and at the back of the extrusion, and the
// corners in between, as bands a constant number of pixels wide. Each band is one segment, a to
// b, drawn as two triangles: end says which point a vertex stands at, across which side.
const char edgeVertexShader[] = R"(
layout( location = 0 ) in vec3 a;// output x, y, depth
layout( location = 1 ) in vec3 b;
layout( location = 2 ) in float slice;
layout( location = 3 ) in vec3 centre;
layout( location = 4 ) in float end;
layout( location = 5 ) in float across;
uniform mat4 mvp;
uniform vec2 viewport;// in pixels
uniform float width;  // of the band, in pixels
out float vAcross;

void main()
{
	vAcross = across;
	// Flat slices have no back nor corners.
	if( extrude <= 0.0 && ( a.z < 0.5 || b.z < 0.5 ) )
	{
		gl_Position = vec4( 2.0, 2.0, 2.0, 1.0 );
		return;
	}
	vec4 clipA = mvp * vec4( Place( a.xy, a.z, slice, centre ), 1.0 );
	vec4 clipB = mvp * vec4( Place( b.xy, b.z, slice, centre ), 1.0 );
	vec2 along = ( clipB.xy / clipB.w - clipA.xy / clipA.w ) * viewport;
	along      = length( along ) > 1e-4 ? normalize( along ) : vec2( 1.0, 0.0 );
	// Out sideways, and a half width past each end so that the bands close at the corners.
	vec2 offset = ( vec2( -along.y, along.x ) * across + along * ( 2.0 * end - 1.0 ) ) * width * 0.5;
	vec4 clip   = end < 0.5 ? clipA : clipB;
	gl_Position = clip + vec4( offset / viewport * 2.0 * clip.w, 0.0, 0.0 );
}
)";

// A sharp line core, and a halo that fades out to the band's edge. Premultiplied.
const char edgeFragmentShader[] = R"(
#version 410 core
uniform vec4 edgeColor;
uniform float width;// of the band, in pixels
uniform float core; // of the line, in pixels
uniform float glow;
uniform float opacity;
in float vAcross;
out vec4 fragColor;

void main()
{
	float fromCentre = abs( vAcross ) * width * 0.5;
	float line       = clamp( core * 0.5 - fromCentre + 0.5, 0.0, 1.0 );
	float halo       = glow * pow( 1.0 - abs( vAcross ), 2.0 );
	fragColor        = edgeColor * max( line, halo ) * opacity;
}
)";

// Textures are premultiplied, and so is what is drawn: blended with ONE, ONE_MINUS_SRC_ALPHA.
// Sides and back faces are lit by a light at the camera, so the volume reads; front faces keep
// their colours untouched.
// In Texture mode, the slices are made of the base colour, lit, and the layer glows on top of
// it. The glow spills onto what is around it: every face is lit by the layer blurred around
// where the face takes it from, so the sides of a slice light up where the glow reaches its
// outline. The spill follows the layer, not the distances in the scene, and casts no shadows.
const char screenFragmentShader[] = R"(
#version 410 core
uniform sampler2D inputTexture;
uniform sampler2D spill;// the layer, mipmapped
uniform int mode;
uniform vec4 color;
uniform float backdrop;
uniform vec3 lightPosition;// in view space
uniform float lightIntensity;
uniform vec3 lightColor;
uniform vec3 globalColor;
uniform float softness;
uniform int globalIllumination;
uniform vec4 baseColor;
uniform vec3 emissive;// the layer's glow, per channel
uniform float bounce;// 0 when the glow lights nothing
uniform float spread;// blur radius, in layer heights
uniform float specular;
uniform float glossiness;
uniform float fresnel;
uniform float metallic;
uniform float reflection;
uniform float iridescence;
uniform vec4 color2;  // the other end of the gradient, and the other colour of the variations
uniform int gradient; // 0 none, 1 vertical, 2 horizontal, 3 diagonal, 4 radial, 5 depth
uniform int variation;// 0 none, 1 alternate, 2 random hue, 3 family hue, 4 random mix
in vec2 vOutput;
in float vDepth;
flat in float vFamily;
in vec2 uv;
in vec2 spillUV;
in vec3 viewPosition;
flat in int vSlice;
flat in int vSide;
out vec4 fragColor;

vec3 HueShift( vec3 rgb, float turn )
{
	// To hue, saturation, value and back, with the hue turned.
	vec4 k = vec4( 0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0 );
	vec4 p = mix( vec4( rgb.bg, k.wz ), vec4( rgb.gb, k.xy ), step( rgb.b, rgb.g ) );
	vec4 q = mix( vec4( p.xyw, rgb.r ), vec4( rgb.r, p.yzx ), step( p.x, rgb.r ) );
	float d = q.x - min( q.w, q.y );
	vec3 hsv = vec3( abs( q.z + ( q.w - q.y ) / ( 6.0 * d + 1e-10 ) ), d / ( q.x + 1e-10 ), q.x );
	vec3 c   = clamp( abs( fract( hsv.x + turn + vec3( 0.0, 2.0 / 3.0, 1.0 / 3.0 ) ) * 6.0 - 3.0 ) - 1.0, 0.0, 1.0 );
	return hsv.z * mix( vec3( 1.0 ), c, hsv.y );
}

// The slice's colour here: first its colour and color2 picked or varied per slice or family,
// then the gradient from one to the other.
vec4 Fill( vec4 a )
{
	vec4 b       = color2;
	float random = fract( sin( float( vSlice ) * 12.9898 + 78.233 ) * 43758.5453 );
	if( variation == 1 && vSlice % 2 == 1 )
	{
		vec4 swap = a;
		a         = b;
		b         = swap;
	}
	else if( variation == 2 || variation == 3 )
	{
		float turn = variation == 2 ? random : vFamily;
		a.rgb      = HueShift( a.rgb, turn );
		b.rgb      = HueShift( b.rgb, turn );
	}
	else if( variation == 4 )
		a = mix( a, b, random );
	float t = gradient == 1 ? vOutput.y : gradient == 2 ? vOutput.x : gradient == 3 ? 0.5 * ( vOutput.x + vOutput.y ) :
	          gradient == 4 ? min( length( vOutput - 0.5 ) * 1.4142, 1.0 ) : gradient == 5 ? 1.0 - vDepth : 0.0;
	return mix( a, b, t );
}

void main()
{
	if( vSlice < 0 )
	{
		if( backdrop <= 0.0 )
			discard;
		fragColor = vec4( vec3( 0.25 ), 1.0 ) * backdrop;
		return;
	}
	vec4 fill = Fill( mode == 0 ? baseColor : color );
	// The face's normal, turned towards the camera: the side that is seen is the side lit.
	vec3 normal = normalize( cross( dFdx( viewPosition ), dFdy( viewPosition ) ) );
	if( dot( normal, viewPosition ) > 0.0 )
		normal = -normal;
	// Base shading, unless turned off so that only the point light shows the slices.
	// Softness lightens the sides towards the front faces.
	float facing = abs( dot( normal, normalize( viewPosition ) ) );
	vec3 light   = globalColor * ( globalIllumination == 0 ? 0.0 : vSide != 0 ? mix( 0.3 + 0.5 * facing, 0.75 + 0.25 * facing, softness ) : 1.0 );

	// Point light, added on top in its colour: diffuse, fading with the square of the distance.
	// Softness wraps it around the faces it barely reaches, and makes it fade slower.
	vec3 toLight = lightPosition - viewPosition;
	float lightDistance = length( toLight );
	float diffuse       = max( ( dot( normal, toLight / lightDistance ) + softness ) / ( 1.0 + softness ), 0.0 );
	float falloff       = 1.0 / ( 1.0 + lightDistance * lightDistance * ( 1.0 - 0.75 * softness ) );
	light += lightColor * lightIntensity * diffuse * falloff;

	// In Color and Chase modes (where the layer is the mask) the slices are filled with the colour.
	if( mode != 0 )
	{
		vec4 surface = vec4( fill.rgb, 1.0 ) * fill.a;
		fragColor    = vec4( surface.rgb * light, surface.a );
		return;
	}

	// The spill: the centre and two rings of taps, read from the mip level whose texels are as
	// far apart as the taps, so that the blur is smooth at any radius. The outer ring weighs
	// less: the light fades away from the glow.
	vec3 received = light;
	if( bounce > 0.0 )
	{
		vec2 size   = vec2( textureSize( spill, 0 ) );
		vec2 radius = vec2( spread * size.y / size.x, spread );
		float lod   = log2( max( spread * size.y * 0.5, 1.0 ) );
		vec3 sum    = textureLod( spill, spillUV, lod ).rgb;
		for( int i = 0; i < 6; ++i )
		{
			float a  = float( i ) * 1.04719755;// 60 degrees
			vec2 dir = vec2( cos( a ), sin( a ) );
			sum += textureLod( spill, spillUV + 0.5 * radius * dir, lod ).rgb;
			sum += 0.5 * textureLod( spill, spillUV + radius * vec2( cos( a + 0.5236 ), sin( a + 0.5236 ) ), lod + 1.0 ).rgb;
		}
		received += bounce * emissive * sum / 10.0;
	}
	// Iridescence: a rainbow that turns with the angle the face is seen from. It tints the base
	// colour, and what the face reflects. Metal reflects in its own colour and scatters none.
	vec3 toEye   = -normalize( viewPosition );
	float seen   = max( dot( normal, toEye ), 0.0 );
	vec3 rainbow = 0.5 + 0.5 * cos( 6.2831853 * ( vec3( 0.0, 0.33, 0.67 ) + 1.5 * seen + 0.5 * viewPosition.y ) );
	vec3 base    = fill.rgb * mix( vec3( 1.0 ), 2.0 * rainbow, 0.5 * iridescence );
	vec3 tint    = mix( mix( vec3( 1.0 ), base, metallic ), rainbow, iridescence );

	vec4 layer   = texture( inputTexture, uv );
	vec4 surface = vec4( base * fill.a * received * ( 1.0 - metallic ) + layer.rgb * emissive, fill.a + layer.a * ( 1.0 - fill.a ) );

	// Glass. Seen edge-on, the face turns into a mirror of the light it receives, opaque.
	surface = mix( surface, vec4( received * tint, 1.0 ), fresnel * pow( 1.0 - seen, 3.0 ) );

	// Reflection of the surroundings: the layer, as if it stood around the scene, blurrier as
	// the surface is rougher, over a sky in the base lighting's colour. As opaque as it is bright.
	vec3 mirrored = vec3( 0.0 );
	if( reflection > 0.0 )
	{
		vec3 r       = reflect( -toEye, normal );
		float levels = log2( float( textureSize( spill, 0 ).y ) );
		vec3 sky     = globalColor * ( 0.15 + 0.5 * clamp( r.y * 0.5 + 0.5, 0.0, 1.0 ) );
		mirrored     = reflection * tint * ( sky + textureLod( spill, 0.5 + 0.5 * r.xy, ( 1.0 - glossiness ) * 0.8 * levels ).rgb );
	}
	// The point light's highlight, in the colour the surface reflects, as opaque as it is bright.
	vec3 halfway   = normalize( toLight / lightDistance + toEye );
	vec3 highlight = tint * lightColor * specular * lightIntensity * pow( max( dot( normal, halfway ), 0.0 ), exp2( 1.0 + 10.0 * glossiness ) ) / ( 1.0 + lightDistance * lightDistance );
	vec3 added     = highlight + mirrored;
	fragColor      = vec4( surface.rgb + added, min( surface.a + max( added.r, max( added.g, added.b ) ), 1.0 ) );
}
)";

// Copies the layer into the spill target, texel for texel, before its mipmaps are made. Its
// premultiplied colour: transparent or dark parts glow little.
const char spillVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec4 vPosition;

void main()
{
	gl_Position = vPosition;
}
)";

const char spillFragmentShader[] = R"(
#version 410 core
uniform sampler2D inputTexture;
out vec4 fragColor;

void main()
{
	fragColor = texelFetch( inputTexture, ivec2( gl_FragCoord.xy ), 0 );
}
)";

const int FLOATS_PER_VERTEX = 11;// output x, y, input x, y, slice, depth, side, centre x, y, z, family
const int FLOATS_PER_EDGE   = 12;// a x, y, depth, b x, y, depth, slice, centre x, y, z, end, across

// Options of the Gradient and Variation parameters, as the screen shader numbers them.
const char* const GRADIENT_NAMES[]  = { "None", "Vertical", "Horizontal", "Diagonal", "Radial", "Depth" };
const char* const VARIATION_NAMES[] = { "None", "Alternate", "Random Hue", "Family Hue", "Random Mix" };

// A value in [0, 1) that stays the same for a name, from one run or machine to the next.
float NameHash( const std::string& name )
{
	unsigned int hash = 2166136261u;// FNV-1a
	for( unsigned char c : name )
		hash = ( hash ^ c ) * 16777619u;
	return ( hash & 0xffffffu ) / 16777216.0f;
}
// --- Column-major 4x4 matrices, as OpenGL expects them ---------------------------------------

struct Vec3
{
	float x, y, z;
};

Vec3 operator-( Vec3 a, Vec3 b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 Cross( Vec3 a, Vec3 b ) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Dot( Vec3 a, Vec3 b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Normalize( Vec3 v )
{
	float length = std::sqrt( Dot( v, v ) );
	return { v.x / length, v.y / length, v.z / length };
}

struct Mat4
{
	float m[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

	Mat4 operator*( const Mat4& b ) const
	{
		Mat4 r;
		for( int c = 0; c < 4; ++c )
			for( int row = 0; row < 4; ++row )
			{
				r.m[ c * 4 + row ] = 0.0f;
				for( int k = 0; k < 4; ++k )
					r.m[ c * 4 + row ] += m[ k * 4 + row ] * b.m[ c * 4 + k ];
			}
		return r;
	}
	Vec3 Transform( Vec3 p ) const
	{
		return { m[ 0 ] * p.x + m[ 4 ] * p.y + m[ 8 ] * p.z + m[ 12 ],
			     m[ 1 ] * p.x + m[ 5 ] * p.y + m[ 9 ] * p.z + m[ 13 ],
			     m[ 2 ] * p.x + m[ 6 ] * p.y + m[ 10 ] * p.z + m[ 14 ] };
	}
};

Mat4 Translate( float x, float y, float z )
{
	Mat4 r;
	r.m[ 12 ] = x;
	r.m[ 13 ] = y;
	r.m[ 14 ] = z;
	return r;
}

Mat4 Scale( float x, float y, float z )
{
	Mat4 r;
	r.m[ 0 ]  = x;
	r.m[ 5 ]  = y;
	r.m[ 10 ] = z;
	return r;
}

// Rotation of degrees around axis 0 (x), 1 (y) or 2 (z).
Mat4 Rotate( int axis, float degrees )
{
	float c = std::cos( degrees * PI / 180.0f ), s = std::sin( degrees * PI / 180.0f );
	int a = ( axis + 1 ) % 3, b = ( axis + 2 ) % 3;
	Mat4 r;
	r.m[ a * 4 + a ] = c;
	r.m[ a * 4 + b ] = s;
	r.m[ b * 4 + a ] = -s;
	r.m[ b * 4 + b ] = c;
	return r;
}

Mat4 Perspective( float fovYDegrees, float aspect, float zNear, float zFar )
{
	float f = 1.0f / std::tan( fovYDegrees * PI / 360.0f );
	Mat4 r;
	r.m[ 0 ]  = f / aspect;
	r.m[ 5 ]  = f;
	r.m[ 10 ] = ( zFar + zNear ) / ( zNear - zFar );
	r.m[ 11 ] = -1.0f;
	r.m[ 14 ] = 2.0f * zFar * zNear / ( zNear - zFar );
	r.m[ 15 ] = 0.0f;
	return r;
}

Mat4 Orthographic( float halfWidth, float halfHeight, float zNear, float zFar )
{
	Mat4 r;
	r.m[ 0 ]  = 1.0f / halfWidth;
	r.m[ 5 ]  = 1.0f / halfHeight;
	r.m[ 10 ] = -2.0f / ( zFar - zNear );
	r.m[ 14 ] = -( zFar + zNear ) / ( zFar - zNear );
	return r;
}

Mat4 LookAt( Vec3 eye, Vec3 target, Vec3 up )
{
	Vec3 f = Normalize( target - eye );
	Vec3 s = Normalize( Cross( f, up ) );
	Vec3 u = Cross( s, f );
	Mat4 r;
	r.m[ 0 ]  = s.x;
	r.m[ 4 ]  = s.y;
	r.m[ 8 ]  = s.z;
	r.m[ 1 ]  = u.x;
	r.m[ 5 ]  = u.y;
	r.m[ 9 ]  = u.z;
	r.m[ 2 ]  = -f.x;
	r.m[ 6 ]  = -f.y;
	r.m[ 10 ] = -f.z;
	r.m[ 12 ] = -Dot( s, eye );
	r.m[ 13 ] = -Dot( u, eye );
	r.m[ 14 ] = Dot( f, eye );
	return r;
}

// --- Saved placements: "name=x,y,z,rotX,rotY,rotZ,size|name=..." --------------------------

const char SEPARATOR = '|';
using Placement = Stage3D::Placement;

std::map< std::string, Placement > ParsePlacements( const std::string& text )
{
	std::map< std::string, Placement > placements;
	std::stringstream stream( text );
	std::string entry;
	while( std::getline( stream, entry, SEPARATOR ) )
	{
		size_t equals = entry.rfind( '=' );
		if( equals == std::string::npos || equals == 0 )
			continue;
		// The C++ classic locale: a host running in French must not turn 1.5 into 1,5.
		std::istringstream values( entry.substr( equals + 1 ) );
		values.imbue( std::locale::classic() );
		Placement p;
		char comma;
		values >> p.x >> comma >> p.y >> comma >> p.z >> comma >> p.rotX >> comma >> p.rotY >> comma >> p.rotZ >> comma >> p.size;
		if( values )
			placements[ entry.substr( 0, equals ) ] = p;
	}
	return placements;
}

std::string JoinPlacements( const std::map< std::string, Placement >& placements )
{
	std::ostringstream text;
	text.imbue( std::locale::classic() );
	for( const auto& entry : placements )
	{
		const Placement& p = entry.second;
		if( text.tellp() > 0 )
			text << SEPARATOR;
		text << entry.first << '=' << p.x << ',' << p.y << ',' << p.z << ',' << p.rotX << ',' << p.rotY << ',' << p.rotZ << ',' << p.size;
	}
	return text.str();
}

}//End anonymous namespace

bool Stage3D::Placement::operator==( const Placement& o ) const
{
	return x == o.x && y == o.y && z == o.z && rotX == o.rotX && rotY == o.rotY && rotZ == o.rotZ && size == o.size;
}

Stage3D::Stage3D()
{
	SetFragmentShader( unusedFragmentShader );

	AddSlicesFileParam();

	// What the slices show. Names become shader uniforms, so the spaced versions are display
	// names only.
	modeIndex = (unsigned int)params.size();
	AddParam( ParamOption::Create( "Mode", { { "Texture", 0.0f }, { "Color", 1.0f }, { "Chase", 2.0f } } ) );
	thresholdIndex = (unsigned int)params.size();
	AddParam( ParamRange::Create( "Threshold", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Smooth", 0.0f, { 0.0f, 10.0f } ) );// seconds to the target and back
	// How the slices follow that move (ChaseLevels::Curve), how much they overshoot or bounce,
	// and how fast, in Hz.
	curveIndex = (unsigned int)params.size();
	AddParam( ParamOption::Create( "Curve", { { "Linear", 0.0f }, { "Smooth", 1.0f }, { "Elastic", 2.0f }, { "Bounce", 3.0f } } ) );
	AddParam( ParamRange::Create( "Elasticity", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "CurveSpeed", 2.0f, { 0.5f, 8.0f } ) );
	SetParamDisplayName( curveIndex + 2, "Speed", false );
	// Picking a ready-made move sets the move parameters and the curve, then goes back to the
	// first entry: what is restored with a composition is the parameters, never the pick.
	movePresetIndex = (unsigned int)params.size();
	std::vector< ParamOption::Option > movePresets = { { "Choose...", 0.0f } };
	for( const MovePreset& movePreset : MOVE_PRESETS )
		movePresets.push_back( { movePreset.name, (float)movePresets.size() } );
	AddParam( ParamOption::Create( "MovePreset", movePresets ) );
	SetParamDisplayName( movePresetIndex, "Move Preset", false );
	// Where a slice goes when the mask is on it, in its screen's axes (z towards the front),
	// around its centre. Move is in screen heights.
	AddParam( ParamRange::Create( "MoveX", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "MoveY", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "MoveZ", 0.2f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "TurnX", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "TurnY", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "TurnZ", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "Scale", 1.0f, { 0.0f, 3.0f } ) );
	const char* moveNames[] = { "Move X", "Move Y", "Move Z", "Rotate X", "Rotate Y", "Rotate Z", "Scale" };
	for( unsigned int i = 0; i < 7; ++i )
		SetParamDisplayName( (unsigned int)params.size() - 7 + i, moveNames[ i ], false );
	colorIndex = (unsigned int)params.size();
	AddHueColorParam( "Color" );
	for( unsigned int i = thresholdIndex; i < colorIndex + 4; ++i )
		SetParamVisibility( i, false, false );// Texture mode: neither chase nor color
	AddParam( ParamRange::Create( "Backdrop", 0.2f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Extrude", 0.0f, { 0.0f, 1.0f } ) );// slice thickness, in screen heights

	// The slices' colour (Base Color, or Color) and Color 2: a gradient from one to the other,
	// and variations per slice or per family. All modes.
	unsigned int firstGradient = (unsigned int)params.size();
	std::vector< ParamOption::Option > gradients, variations;
	for( const char* name : GRADIENT_NAMES )
		gradients.push_back( { name, (float)gradients.size() } );
	for( const char* name : VARIATION_NAMES )
		variations.push_back( { name, (float)variations.size() } );
	AddParam( ParamOption::Create( "Gradient", gradients ) );
	AddParam( ParamOption::Create( "Variation", variations ) );
	AddParam( Param::Create( "Color2", FF_TYPE_HUE, 0.6f ) );
	AddParam( Param::Create( "Color2_saturation", FF_TYPE_SATURATION, 0.8f ) );
	AddParam( Param::Create( "Color2_brightness", FF_TYPE_BRIGHTNESS, 0.8f ) );
	AddParam( Param::Create( "Color2_alpha", FF_TYPE_ALPHA, 1.0f ) );
	SetParamDisplayName( firstGradient + 2, "Color 2", false );
	for( unsigned int i = firstGradient; i < params.size(); ++i )
		SetParamGroup( i, "Gradient" );

	// Edges: lines of Edge Width pixels (at 1080 high) along the slices' outlines, front and back,
	// and their sharp corners; 0 hides them. Edge Glow adds a halo. See Through is how much of
	// the edges hidden behind slices shows through them.
	unsigned int firstEdge = (unsigned int)params.size();
	AddParam( ParamRange::Create( "EdgeWidth", 0.0f, { 0.0f, 10.0f } ) );
	AddHueColorParam( "EdgeColor" );
	AddParam( ParamRange::Create( "EdgeGlow", 0.0f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "SeeThrough", 0.3f, { 0.0f, 1.0f } ) );
	const char* edgeNames[] = { "Edge Width", "Edge Color", "", "", "", "Edge Glow", "See Through" };
	for( unsigned int i = 0; i < 7; ++i )
	{
		if( *edgeNames[ i ] )
			SetParamDisplayName( firstEdge + i, edgeNames[ i ], false );
		SetParamGroup( firstEdge + i, "Edges" );
	}

	// Texture mode: the slices are made of the base colour, lit, and the layer glows on top of
	// it (Emissive, tinted by its colour), spilling onto what is around it (Bounce, Spread). Base Color's
	// alpha is the opacity of the slices; it starts dark grey, so that the glow shows. Material
	// Preset works like Move Preset.
	materialPresetIndex = (unsigned int)params.size();
	std::vector< ParamOption::Option > materialPresets = { { "Choose...", 0.0f } };
	for( const MaterialPreset& materialPreset : MATERIAL_PRESETS )
		materialPresets.push_back( { materialPreset.name, (float)materialPresets.size() } );
	AddParam( ParamOption::Create( "MaterialPreset", materialPresets ) );
	SetParamDisplayName( materialPresetIndex, "Material Preset", false );
	materialIndex = (unsigned int)params.size();
	AddParam( Param::Create( "BaseColor", FF_TYPE_HUE, 0.0f ) );
	AddParam( Param::Create( "BaseColor_saturation", FF_TYPE_SATURATION, 0.0f ) );
	AddParam( Param::Create( "BaseColor_brightness", FF_TYPE_BRIGHTNESS, 0.25f ) );
	AddParam( Param::Create( "BaseColor_alpha", FF_TYPE_ALPHA, 1.0f ) );
	SetParamDisplayName( materialIndex, "Base Color", false );
	AddParam( ParamRange::Create( "Emissive", 1.0f, { 0.0f, 2.0f } ) );
	AddHueColorParam( "EmissiveColor" );
	SetParamDisplayName( materialIndex + 5, "Emissive Color", false );
	AddParam( ParamRange::Create( "Bounce", 2.0f, { 0.0f, 10.0f } ) );
	AddParam( ParamRange::Create( "Spread", 0.05f, { 0.0f, 0.5f } ) );// how far the glow spills, in layer heights
	// Glass: a highlight of the point light (Specular, sharper with Glossiness), and faces that
	// turn opaque and reflect the light they receive as they are seen edge-on (Fresnel).
	AddParam( ParamRange::Create( "Specular", 0.0f, { 0.0f, 5.0f } ) );
	AddParam( ParamRange::Create( "Glossiness", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Fresnel", 0.0f, { 0.0f, 1.0f } ) );
	// Metal: reflects in the base colour, and does not scatter it. Reflection: mirrors the layer
	// around the scene, blurred by the lack of Glossiness. Iridescence: colours that turn with
	// the angle the slices are seen from.
	AddParam( ParamRange::Create( "Metallic", 0.0f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Reflection", 0.0f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Iridescence", 0.0f, { 0.0f, 1.0f } ) );
	for( unsigned int i = materialPresetIndex; i < params.size(); ++i )
		SetParamGroup( i, "Material" );

	// A point light in the scene, added to the base shading; off at intensity 0. Light Preset
	// works like Move Preset. Softness wraps the light around the faces, lightens the shaded
	// sides and carries the point light further.
	unsigned int firstLight = (unsigned int)params.size();
	lightPresetIndex        = firstLight;
	std::vector< ParamOption::Option > lightPresets = { { "Choose...", 0.0f } };
	for( const LightPreset& lightPreset : LIGHT_PRESETS )
		lightPresets.push_back( { lightPreset.name, (float)lightPresets.size() } );
	AddParam( ParamOption::Create( "LightPreset", lightPresets ) );
	SetParamDisplayName( lightPresetIndex, "Light Preset", false );
	++firstLight;
	AddParam( ParamRange::Create( "LightX", 0.0f, { -2.0f, 2.0f } ) );
	AddParam( ParamRange::Create( "LightY", 0.5f, { -2.0f, 2.0f } ) );
	AddParam( ParamRange::Create( "LightZ", 1.0f, { -2.0f, 2.0f } ) );
	AddParam( ParamRange::Create( "LightIntensity", 0.0f, { 0.0f, 5.0f } ) );
	AddParam( ParamBool::Create( "GlobalIllumination", true ) );
	AddHueColorParam( "LightColor" );// its alpha dims the light, like Intensity
	AddHueColorParam( "GlobalColor" );// the base shading's; its alpha dims it
	const char* lightNames[] = { "X", "Y", "Z", "Intensity", "Global Illumination", "Point Color" };
	for( unsigned int i = 0; i < 6; ++i )
		SetParamDisplayName( firstLight + i, lightNames[ i ], false );
	SetParamDisplayName( firstLight + 9, "Global Color", false );
	AddParam( ParamRange::Create( "Softness", 0.0f, { 0.0f, 1.0f } ) );
	// Animation of the point light (LightAnimation), its cycle (Animation Rate) and how strong
	// it is (Animation Amount).
	std::vector< ParamOption::Option > animations, rates;
	for( const char* name : ANIMATION_NAMES )
		animations.push_back( { name, (float)animations.size() } );
	for( const char* name : RATE_NAMES )
		rates.push_back( { name, (float)rates.size() } );
	AddParam( ParamOption::Create( "LightAnimation", animations ) );
	AddParam( ParamOption::Create( "AnimationRate", rates, 2 ) );
	AddParam( ParamRange::Create( "AnimationAmount", 1.0f, { 0.0f, 1.0f } ) );
	const char* animationNames[] = { "Animation", "Animation Rate", "Animation Amount" };
	for( unsigned int i = 0; i < 3; ++i )
		SetParamDisplayName( (unsigned int)params.size() - 3 + i, animationNames[ i ], false );
	for( unsigned int i = lightPresetIndex; i < params.size(); ++i )
		SetParamGroup( i, "Light" );

	// Orbit around the scene's origin, in scene units: the view does not follow the screens.
	unsigned int firstCamera = (unsigned int)params.size();
	AddParam( ParamRange::Create( "Yaw", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "Pitch", 0.0f, { -89.0f, 89.0f } ) );
	AddParam( ParamRange::Create( "Distance", 2.5f, { 0.0f, 10.0f } ) );
	// 0 is flat (orthographic), 1 a 90 degree field of view. Distance keeps the framing.
	AddParam( ParamRange::Create( "Perspective", 0.5f, { 0.0f, 1.0f } ) );
	for( unsigned int i = firstCamera; i < params.size(); ++i )
		SetParamGroup( i, "Camera" );

	// Moves, turns and scales all the screens together, around the centre of their bounds.
	unsigned int firstTotal = (unsigned int)params.size();
	AddParam( ParamRange::Create( "TotalX", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "TotalY", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "TotalZ", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "TotalRotX", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "TotalRotY", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "TotalRotZ", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "TotalSize", 1.0f, { 0.05f, 10.0f } ) );
	const char* totalNames[] = { "Total X", "Total Y", "Total Z", "Total Rot X", "Total Rot Y", "Total Rot Z", "Total Size" };
	for( unsigned int i = 0; i < 7; ++i )
		SetParamDisplayName( firstTotal + i, totalNames[ i ], false );

	// The placement parameters edit the screen picked in Edit Screen.
	editScreenIndex = AddScreenOption( "EditScreen", "Edit Screen" );
	firstPlacement = (unsigned int)params.size();
	AddParam( ParamRange::Create( "PosX", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "PosY", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "PosZ", 0.0f, { -1.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "RotX", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "RotY", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "RotZ", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "Size", 1.0f, { 0.05f, 10.0f } ) );
	const char* displayNames[] = { "X", "Y", "Z", "Rot X", "Rot Y", "Rot Z", "Size" };
	for( unsigned int i = 0; i < 7; ++i )
		SetParamDisplayName( firstPlacement + i, displayNames[ i ], false );
	for( unsigned int i = firstTotal; i < params.size(); ++i )
		SetParamGroup( i, "Placement" );

	// Every edited screen's placement, by name. Hidden: Edit Screen and the parameters above
	// are the UI.
	placementsIndex = (unsigned int)params.size();
	placementsText  = ParamText::create( "Placements" );
	AddParam( placementsText );
	SetParamVisibility( placementsIndex, false, false );

	AddSelectionParams( true );// each family's z offset, in screen heights, on each screen
}

FFResult Stage3D::Init()
{
	if( !levels.Init() )
		return FF_FAIL;
	const std::string version = "#version 410 core\n";
	if( !screenShader.Compile( version + placeGLSL + screenVertexShader, std::string( screenFragmentShader ) ) )
		return FF_FAIL;
	if( !edgeShader.Compile( version + placeGLSL + edgeVertexShader, std::string( edgeFragmentShader ) ) )
		return FF_FAIL;
	if( !spillShader.Compile( spillVertexShader, spillFragmentShader ) )
		return FF_FAIL;
	return FF_SUCCESS;
}

void Stage3D::Update()
{
	SelectionUpdate selection = UpdateSelection();
	if( selection.presetLoaded )
		ShowScreenNames( editScreenIndex );
	UpdatePlacement( selection.presetLoaded );
	UpdateModeVisibility();
	ApplyMovePreset();
	ApplyLightPreset();
	ApplyMaterialPreset();
	if( !selection.changed )
		return;
	ReleaseScreens();
	UploadScreens();
}

void Stage3D::UpdatePlacement( bool reload )
{
	// The host restores the saved placements (composition load, undo), or Edit Screen picks
	// another screen: the placement parameters follow. Otherwise, a placement parameter that
	// differs from what was shown was moved, and the edited screen takes the new placement.
	bool restored = placementsText->text != readPlacements;
	if( restored )
	{
		placements     = ParsePlacements( placementsText->text );
		readPlacements = placementsText->text;
	}
	int count = (int)std::min( preset.screens.size(), (size_t)MAX_SCREENS );
	int edit  = count > 0 ? std::clamp( (int)params[ editScreenIndex ]->GetValue(), 0, count - 1 ) : -1;
	if( reload || restored || edit != editedScreen )
	{
		editedScreen = edit;
		if( edit >= 0 )
			ShowPlacementParams( PlacementOf( edit ) );
		return;
	}
	if( edit < 0 )
		return;

	Placement placement = ReadPlacementParams();
	if( placement == shownPlacement )
		return;
	shownPlacement                            = placement;
	placements[ preset.screens[ edit ].name ] = placement;
	readPlacements                            = JoinPlacements( placements );
	placementsText->text                      = readPlacements;
	RaiseParamEvent( placementsIndex, FF_EVENT_FLAG_VALUE );
}

void Stage3D::UpdateModeVisibility()
{
	int mode = (int)params[ modeIndex ]->GetValue();
	for( unsigned int i = thresholdIndex; i < colorIndex; ++i )
		SetParamVisibility( i, mode == MODE_CHASE, true );
	for( unsigned int i = colorIndex; i < colorIndex + 4; ++i )
		SetParamVisibility( i, mode != MODE_TEXTURE, true );
	for( unsigned int i = materialPresetIndex; i < materialIndex + 17; ++i )
		SetParamVisibility( i, mode == MODE_TEXTURE, true );
}

void Stage3D::ApplyMovePreset()
{
	int choice = (int)params[ movePresetIndex ]->GetValue();
	if( choice < 1 || choice > (int)std::size( MOVE_PRESETS ) )
		return;
	const MovePreset& movePreset = MOVE_PRESETS[ choice - 1 ];
	for( unsigned int i = 0; i < 7; ++i )
		SetAndShow( movePresetIndex + 1 + i, movePreset.move[ i ] );
	SetAndShow( curveIndex, (float)movePreset.curve );
	SetAndShow( curveIndex + 1, movePreset.elasticity );
	SetAndShow( curveIndex + 2, movePreset.speed );
	SetAndShow( movePresetIndex, 0.0f );
}

void Stage3D::ApplyLightPreset()
{
	int choice = (int)params[ lightPresetIndex ]->GetValue();
	if( choice < 1 || choice > (int)std::size( LIGHT_PRESETS ) )
		return;
	for( unsigned int i = 0; i < 17; ++i )
		SetAndShow( lightPresetIndex + 1 + i, LIGHT_PRESETS[ choice - 1 ].values[ i ] );
	SetAndShow( lightPresetIndex, 0.0f );
}

void Stage3D::ApplyMaterialPreset()
{
	int choice = (int)params[ materialPresetIndex ]->GetValue();
	if( choice < 1 || choice > (int)std::size( MATERIAL_PRESETS ) )
		return;
	for( unsigned int i = 0; i < 17; ++i )
		SetAndShow( materialIndex + i, MATERIAL_PRESETS[ choice - 1 ].values[ i ] );
	SetAndShow( materialPresetIndex, 0.0f );
}

namespace
{
// A repeatable random value in [0, 1) for each n.
float Hash( int n )
{
	unsigned int x = (unsigned int)n * 0x27d4eb2du;
	x ^= x >> 15;
	x *= 0x85ebca6bu;
	x ^= x >> 13;
	return ( x & 0xffffffu ) / 16777216.0f;
}
}//End anonymous namespace

Stage3D::LightState Stage3D::AnimateLight()
{
	auto value = [ this ]( const char* name ) { return GetParam( name )->GetValue(); };
	LightState light{ value( "LightX" ), value( "LightY" ), value( "LightZ" ), value( "LightIntensity" ) * value( "LightColor_alpha" ),
		              value( "LightColor" ) == 1.0f ? 0.0f : value( "LightColor" ), value( "LightColor_saturation" ), value( "LightColor_brightness" ) };

	// The bars played so far, counted as the host's bar phase wraps: cycles can last several bars.
	if( barPhase < lastBarPhase - 0.5f )
		++bars;
	lastBarPhase = barPhase;
	int animation = (int)value( "LightAnimation" );
	if( animation == ANIMATION_NONE )
		return light;

	int rate      = std::clamp( (int)value( "AnimationRate" ), 0, (int)std::size( CYCLES_PER_BAR ) - 1 );
	float amount  = value( "AnimationAmount" );
	double cycles = ( bars + barPhase ) * CYCLES_PER_BAR[ rate ];
	float phase   = (float)( cycles - std::floor( cycles ) );
	auto orbit    = [ &light ]( float angle ) {
		float c = std::cos( angle ), s = std::sin( angle ), x = light.x;
		light.x = x * c + light.z * s;
		light.z = light.z * c - x * s;
	};
	switch( animation )
	{
	case ANIMATION_ORBIT:
		orbit( 2.0f * PI * phase );
		break;
	case ANIMATION_SWEEP:
		light.x += 2.0f * amount * std::sin( 2.0f * PI * phase );
		break;
	case ANIMATION_PULSE:
		light.intensity *= 1.0f - amount * ( 1.0f - std::exp( -6.0f * phase ) );
		break;
	case ANIMATION_STROBE:
		light.intensity *= phase < 0.15f ? 1.0f : 1.0f - amount;
		break;
	case ANIMATION_FLICKER:
	{
		// 8 random levels per cycle, blended into each other.
		double steps = cycles * 8.0;
		int step     = (int)std::floor( steps );
		float t      = (float)( steps - step );
		light.intensity *= 1.0f - amount * ( Hash( step ) + ( Hash( step + 1 ) - Hash( step ) ) * t * t * ( 3.0f - 2.0f * t ) );
		break;
	}
	case ANIMATION_COLOR_CYCLE:
	{
		float hue = light.hue + amount * ( 0.5f - 0.5f * std::cos( 2.0f * PI * phase ) );
		light.hue = hue - std::floor( hue );
		break;
	}
	case ANIMATION_POLICE:
	{
		bool red         = phase < 0.5f;
		light.hue        = red ? 0.0f : 0.64f;
		light.saturation = 1.0f;
		light.x          = red ? std::abs( light.x ) : -std::abs( light.x );
		float flash      = phase * 4.0f - std::floor( phase * 4.0f );// two flashes per colour
		light.intensity *= flash < 0.5f ? 1.0f : 1.0f - amount;
		break;
	}
	case ANIMATION_DISCO:
	{
		orbit( 2.0f * PI * phase );
		float hue = light.hue + amount * std::floor( phase * 8.0f ) / 8.0f;
		light.hue = hue - std::floor( hue );
		break;
	}
	}
	return light;
}

void Stage3D::SetAndShow( unsigned int index, float value )
{
	params[ index ]->SetValue( value );
	RaiseParamEvent( index, FF_EVENT_FLAG_VALUE );
}

Stage3D::Placement Stage3D::PlacementOf( int screen ) const
{
	auto saved = placements.find( preset.screens[ screen ].name );
	if( saved != placements.end() )
		return saved->second;

	// Not placed yet: every screen of the preset centred on the origin, 1 unit high, stacked
	// from the front (first screen) to the back (last screen).
	const float SPACING = 0.1f;
	Placement placement;
	placement.z = ( ( preset.screens.size() - 1 ) / 2.0f - screen ) * SPACING;
	return placement;
}

Stage3D::Placement Stage3D::ReadPlacementParams() const
{
	auto value = [ this ]( unsigned int i ) { return params[ firstPlacement + i ]->GetValue(); };
	return { value( 0 ), value( 1 ), value( 2 ), value( 3 ), value( 4 ), value( 5 ), value( 6 ) };
}

void Stage3D::ShowPlacementParams( const Placement& placement )
{
	const float values[] = { placement.x, placement.y, placement.z, placement.rotX, placement.rotY, placement.rotZ, placement.size };
	for( unsigned int i = 0; i < 7; ++i )
	{
		if( params[ firstPlacement + i ]->GetValue() == values[ i ] )
			continue;
		params[ firstPlacement + i ]->SetValue( values[ i ] );
		RaiseParamEvent( firstPlacement + i, FF_EVENT_FLAG_VALUE );
	}
	shownPlacement = placement;
}

FFResult Stage3D::Render( ProcessOpenGLStruct* inputTextures )
{
	if( inputTextures->numInputTextures < 1 || inputTextures->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *inputTextures->inputTextures[ 0 ];

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( draws.empty() )
		return FF_SUCCESS;

	int mode = (int)params[ modeIndex ]->GetValue();
	if( mode == MODE_CHASE && sliceCount > 0 )
	{
		float smooth = GetParam( "Smooth" )->GetValue();
		levels.Update( input, { GetParam( "Threshold" )->GetValue(), smooth, smooth, deltaTime, (int)params[ curveIndex ]->GetValue(),
		                        GetParam( "CurveSpeed" )->GetValue(), GetParam( "Elasticity" )->GetValue() },
		               quad );
	}

	GLsizei width = currentViewport.width, height = currentViewport.height;
	if( scene.GetWidth() != (GLuint)width || scene.GetHeight() != (GLuint)height )
	{
		scene.Release();
		if( !scene.Initialise( width, height ) )
			return FF_FAIL;
	}

	// Orbit camera around the origin. Distance sets the framing: the height seen at the origin
	// is what a 45 degree field of view sees from that distance. Perspective sets the field of
	// view; the camera moves back or forward so that the framing stays.
	float aspect      = (float)width / (float)height;
	float halfHeight  = std::max( GetParam( "Distance" )->GetValue(), 0.01f ) * std::tan( 22.5f * PI / 180.0f );
	float fov         = GetParam( "Perspective" )->GetValue() * 90.0f;
	bool flat         = fov < 0.5f;// below that, the camera is so far that it is as good as flat
	float dist        = flat ? 50.0f : halfHeight / std::tan( fov * PI / 360.0f );
	float yaw         = GetParam( "Yaw" )->GetValue() * PI / 180.0f;
	float pitch       = GetParam( "Pitch" )->GetValue() * PI / 180.0f;
	Vec3 eye          = { dist * std::cos( pitch ) * std::sin( yaw ), dist * std::sin( pitch ), dist * std::cos( pitch ) * std::cos( yaw ) };
	Mat4 view         = LookAt( eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } );
	float zNear       = std::max( 0.05f, dist - 50.0f ), zFar = dist + 50.0f;
	Mat4 projection   = flat ? Orthographic( halfHeight * aspect, halfHeight, zNear, zFar ) : Perspective( fov, aspect, zNear, zFar );

	// The bounds of everything drawn, as placed: the visible backdrops and the slices, with their
	// family offset and extrusion, but not their Chase moves.
	float extrude = GetParam( "Extrude" )->GetValue();
	std::vector< Mat4 > models;
	Vec3 low = { 1e9f, 1e9f, 1e9f }, high = { -1e9f, -1e9f, -1e9f };
	auto addBox = [ & ]( const Mat4& model, float x0, float y0, float x1, float y1, float z0, float z1 ) {
		for( int corner = 0; corner < 8; ++corner )
		{
			Vec3 c = model.Transform( { corner & 1 ? x1 : x0, corner & 2 ? y1 : y0, corner & 4 ? z1 : z0 } );
			low    = { std::min( low.x, c.x ), std::min( low.y, c.y ), std::min( low.z, c.z ) };
			high   = { std::max( high.x, c.x ), std::max( high.y, c.y ), std::max( high.z, c.z ) };
		}
	};
	bool backdrops = GetParam( "Backdrop" )->GetValue() > 0.0f;
	for( const ScreenDraw& draw : draws )
	{
		Placement p = PlacementOf( draw.screen );
		models.push_back( Translate( p.x, p.y, p.z ) * Rotate( 1, p.rotY ) * Rotate( 0, p.rotX ) * Rotate( 2, p.rotZ ) * Scale( p.size, p.size, p.size ) );
		float halfWidth = preset.screens[ draw.screen ].aspect * 0.5f;
		if( backdrops )
			addBox( models.back(), -halfWidth, -0.5f, halfWidth, 0.5f, -extrude - 0.002f, -extrude - 0.002f );
	}
	for( const SliceDraw& s : sliceDraws )
		addBox( models[ s.draw ], s.low[ 0 ], s.low[ 1 ], s.high[ 0 ], s.high[ 1 ], s.centre[ 2 ] - extrude, s.centre[ 2 ] );
	if( low.x > high.x )
		low = high = { 0.0f, 0.0f, 0.0f };

	// Moves, turns and scales all the screens together, around the centre of their bounds.
	auto total = [ this ]( const char* name ) { return GetParam( name )->GetValue(); };
	float totalSize = total( "TotalSize" );
	Vec3 pivot      = { ( low.x + high.x ) * 0.5f, ( low.y + high.y ) * 0.5f, ( low.z + high.z ) * 0.5f };
	Mat4 world      = view * Translate( pivot.x + total( "TotalX" ), pivot.y + total( "TotalY" ), pivot.z + total( "TotalZ" ) ) *
	             Rotate( 1, total( "TotalRotY" ) ) * Rotate( 0, total( "TotalRotX" ) ) * Rotate( 2, total( "TotalRotZ" ) ) *
	             Scale( totalSize, totalSize, totalSize ) * Translate( -pivot.x, -pivot.y, -pivot.z );

	std::vector< Mat4 > modelViews;
	for( const Mat4& model : models )
		modelViews.push_back( world * model );
	// The see-through backdrops go last, from the farthest to the nearest.
	std::vector< size_t > order( draws.size() );
	std::vector< float > depth( draws.size() );
	for( size_t i = 0; i < draws.size(); ++i )
	{
		order[ i ] = i;
		depth[ i ] = modelViews[ i ].Transform( { 0.0f, 0.0f, 0.0f } ).z;
	}
	std::sort( order.begin(), order.end(), [ & ]( size_t a, size_t b ) { return depth[ a ] < depth[ b ]; } );

	auto hueColor = [ this ]( const std::string& name, float rgb[ 3 ] ) {
		float hue = GetParam( name )->GetValue();
		HSVtoRGB( hue == 1.0f ? 0.0f : hue,
		          GetParam( name + "_saturation" )->GetValue(),
		          GetParam( name + "_brightness" )->GetValue(),
		          rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );
	};
	float rgb[ 3 ], base[ 3 ], emission[ 3 ];
	hueColor( "Color", rgb );
	hueColor( "BaseColor", base );
	hueColor( "EmissiveColor", emission );
	float globalColor[ 3 ], color2[ 3 ];
	hueColor( "GlobalColor", globalColor );
	hueColor( "Color2", color2 );
	LightState lightState = AnimateLight();
	float lightColor[ 3 ];
	HSVtoRGB( lightState.hue, lightState.saturation, lightState.brightness, lightColor[ 0 ], lightColor[ 1 ], lightColor[ 2 ] );
	FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );

	// Texture mode: the layer copied without its padding, and mipmapped, for its glow to spill
	// and to be reflected.
	float bounce     = GetParam( "Bounce" )->GetValue();
	float reflection = GetParam( "Reflection" )->GetValue();
	bool spilled     = mode == MODE_TEXTURE && ( bounce > 0.0f || reflection > 0.0f );
	if( spilled )
	{
		if( spill.GetWidth() != input.Width || spill.GetHeight() != input.Height )
		{
			spill.Release();
			if( !spill.Initialise( input.Width, input.Height ) )
				return FF_FAIL;
			Scoped2DTextureBinding textureBinding( spill.GetTextureInfo().Handle );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		}
		{
			ScopedFBOBinding spillBinding( spill.GetGLID(), ScopedFBOBinding::RB_REVERT );
			glViewport( 0, 0, input.Width, input.Height );
			ScopedShaderBinding spillShaderBinding( spillShader.GetGLID() );
			Scoped2DTextureBinding inputBinding( input.Handle );
			spillShader.Set( "inputTexture", 0 );
			quad.Draw();
		}
		Scoped2DTextureBinding textureBinding( spill.GetTextureInfo().Handle );
		glGenerateMipmap( GL_TEXTURE_2D );
	}

	{
		ScopedFBOBinding fboBinding( scene.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, width, height );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		glEnable( GL_DEPTH_TEST );
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

		ScopedShaderBinding shaderBinding( screenShader.GetGLID() );
		// Bound by hand: ScopedSamplerActivation returns to unit 0, not to the previous unit.
		const GLuint textures[] = { input.Handle, mode == MODE_CHASE && sliceCount > 0 ? levels.Texture() : 0,
			                        spilled ? spill.GetTextureInfo().Handle : 0 };
		for( GLuint unit = 0; unit < 3; ++unit )
		{
			glActiveTexture( GL_TEXTURE0 + unit );
			glBindTexture( GL_TEXTURE_2D, textures[ unit ] );
		}
		screenShader.Set( "inputTexture", 0 );
		screenShader.Set( "levels", 1 );
		screenShader.Set( "spill", 2 );
		screenShader.Set( "mode", mode );
		screenShader.Set( "color", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], GetParam( "Color_alpha" )->GetValue() );
		screenShader.Set( "backdrop", GetParam( "Backdrop" )->GetValue() );
		Vec3 light = view.Transform( { lightState.x, lightState.y, lightState.z } );
		screenShader.Set( "lightPosition", light.x, light.y, light.z );
		screenShader.Set( "lightIntensity", lightState.intensity );
		screenShader.Set( "lightColor", lightColor[ 0 ], lightColor[ 1 ], lightColor[ 2 ] );
		float global = GetParam( "GlobalColor_alpha" )->GetValue();
		screenShader.Set( "globalColor", globalColor[ 0 ] * global, globalColor[ 1 ] * global, globalColor[ 2 ] * global );
		screenShader.Set( "softness", GetParam( "Softness" )->GetValue() );
		screenShader.Set( "globalIllumination", GetParam( "GlobalIllumination" )->GetValue() > 0.5f ? 1 : 0 );
		// Emissive Color's alpha scales the glow, like Emissive.
		float emissive = GetParam( "Emissive" )->GetValue() * GetParam( "EmissiveColor_alpha" )->GetValue();
		screenShader.Set( "baseColor", base[ 0 ], base[ 1 ], base[ 2 ], GetParam( "BaseColor_alpha" )->GetValue() );
		screenShader.Set( "emissive", emission[ 0 ] * emissive, emission[ 1 ] * emissive, emission[ 2 ] * emissive );
		screenShader.Set( "bounce", spilled ? bounce : 0.0f );
		screenShader.Set( "spread", GetParam( "Spread" )->GetValue() );
		screenShader.Set( "specular", GetParam( "Specular" )->GetValue() );
		screenShader.Set( "glossiness", GetParam( "Glossiness" )->GetValue() );
		screenShader.Set( "fresnel", GetParam( "Fresnel" )->GetValue() );
		screenShader.Set( "metallic", GetParam( "Metallic" )->GetValue() );
		screenShader.Set( "reflection", spilled ? reflection : 0.0f );
		screenShader.Set( "iridescence", GetParam( "Iridescence" )->GetValue() );
		screenShader.Set( "extrude", extrude );
		screenShader.Set( "maxUV", maxCoords.s, maxCoords.t );
		screenShader.Set( "move", GetParam( "MoveX" )->GetValue(), GetParam( "MoveY" )->GetValue(), GetParam( "MoveZ" )->GetValue() );
		screenShader.Set( "turn", GetParam( "TurnX" )->GetValue(), GetParam( "TurnY" )->GetValue(), GetParam( "TurnZ" )->GetValue() );
		screenShader.Set( "scale", GetParam( "Scale" )->GetValue() );
		int gradient  = (int)GetParam( "Gradient" )->GetValue();
		int variation = (int)GetParam( "Variation" )->GetValue();
		screenShader.Set( "color2", color2[ 0 ], color2[ 1 ], color2[ 2 ], GetParam( "Color2_alpha" )->GetValue() );
		screenShader.Set( "gradient", gradient );
		screenShader.Set( "variation", variation );

		ScopedVAOBinding vaoBinding( vao );
		GLint mvp = screenShader.FindUniform( "mvp" ), modelView = screenShader.FindUniform( "modelView" );
		auto setScreen = [ & ]( size_t i ) {
			Mat4 matrix = projection * modelViews[ i ];
			glUniformMatrix4fv( mvp, 1, GL_FALSE, matrix.m );
			glUniformMatrix4fv( modelView, 1, GL_FALSE, modelViews[ i ].m );
			screenShader.Set( "aspect", preset.screens[ draws[ i ].screen ].aspect );
		};
		float opacity = GetParam( mode == MODE_TEXTURE ? "BaseColor_alpha" : "Color_alpha" )->GetValue();
		if( gradient != 0 || variation != 0 )
			opacity = std::min( opacity, GetParam( "Color2_alpha" )->GetValue() );
		if( opacity >= 1.0f )
		{
			for( size_t i = 0; i < draws.size(); ++i )
			{
				setScreen( i );
				GLint first = draws[ i ].first + 6, end = extrude > 0.0f ? draws[ i ].end : draws[ i ].sidesFirst;
				glDrawArrays( GL_TRIANGLES, first, end - first );
			}
			glDepthMask( GL_FALSE );
			for( size_t i : order )
			{
				setScreen( i );
				glDrawArrays( GL_TRIANGLES, draws[ i ].first, 6 );
			}
		}
		else
		{
			// See-through slices must not hide what is behind them: they and the backdrops are all
			// drawn from the farthest to the nearest, sorted by their centres, without writing depth.
			// Within a slice, its face away from the camera goes first. Chase moves are not sorted.
			struct Item
			{
				float depth;
				int slice;// in sliceDraws, or -1 for the backdrop of draw
				size_t draw;
			};
			std::vector< Item > items;
			for( size_t i = 0; i < draws.size(); ++i )
				items.push_back( { modelViews[ i ].Transform( { 0.0f, 0.0f, -extrude - 0.002f } ).z, -1, i } );
			for( size_t s = 0; s < sliceDraws.size(); ++s )
			{
				const float* c = sliceDraws[ s ].centre;
				items.push_back( { modelViews[ sliceDraws[ s ].draw ].Transform( { c[ 0 ], c[ 1 ], c[ 2 ] - extrude * 0.5f } ).z, (int)s, (size_t)sliceDraws[ s ].draw } );
			}
			std::sort( items.begin(), items.end(), []( const Item& a, const Item& b ) { return a.depth < b.depth; } );

			glDepthMask( GL_FALSE );
			for( const Item& item : items )
			{
				setScreen( item.draw );
				if( item.slice < 0 )
				{
					glDrawArrays( GL_TRIANGLES, draws[ item.draw ].first, 6 );
					continue;
				}
				const SliceDraw& slice = sliceDraws[ item.slice ];
				if( extrude <= 0.0f )
				{
					glDrawArrays( GL_TRIANGLES, slice.frontFirst, slice.frontEnd - slice.frontFirst );
					continue;
				}
				const float* c = slice.centre;
				bool fromFront = modelViews[ item.draw ].Transform( { c[ 0 ], c[ 1 ], c[ 2 ] } ).z > modelViews[ item.draw ].Transform( { c[ 0 ], c[ 1 ], c[ 2 ] - extrude } ).z;
				GLint farFirst = fromFront ? slice.backFirst : slice.frontFirst, farEnd = fromFront ? slice.backEnd : slice.frontEnd;
				GLint nearFirst = fromFront ? slice.frontFirst : slice.backFirst, nearEnd = fromFront ? slice.frontEnd : slice.backEnd;
				glDrawArrays( GL_TRIANGLES, farFirst, farEnd - farFirst );
				glDrawArrays( GL_TRIANGLES, slice.sidesFirst, slice.backFirst - slice.sidesFirst );
				glDrawArrays( GL_TRIANGLES, nearFirst, nearEnd - nearFirst );
			}
		}

		// The edges, over the slices: first those that slices hide, dimmed by See Through, then
		// those in sight. Pulled a little towards the camera, so that the faces they border do not
		// hide them. See-through slices write no depth: all their edges are in sight.
		float edgeWidth = GetParam( "EdgeWidth" )->GetValue() * height / 1080.0f;
		if( edgeWidth > 0.0f && edgeVao != 0 )
		{
			ScopedShaderBinding edgeBinding( edgeShader.GetGLID() );
			ScopedVAOBinding edgeVaoBinding( edgeVao );
			float glow = GetParam( "EdgeGlow" )->GetValue(), edgeColor[ 3 ];
			hueColor( "EdgeColor", edgeColor );
			float alpha = GetParam( "EdgeColor_alpha" )->GetValue();
			edgeShader.Set( "levels", 1 );
			edgeShader.Set( "mode", mode );
			edgeShader.Set( "extrude", extrude );
			edgeShader.Set( "move", GetParam( "MoveX" )->GetValue(), GetParam( "MoveY" )->GetValue(), GetParam( "MoveZ" )->GetValue() );
			edgeShader.Set( "turn", GetParam( "TurnX" )->GetValue(), GetParam( "TurnY" )->GetValue(), GetParam( "TurnZ" )->GetValue() );
			edgeShader.Set( "scale", GetParam( "Scale" )->GetValue() );
			edgeShader.Set( "viewport", (float)width, (float)height );
			edgeShader.Set( "width", edgeWidth * ( 1.0f + 4.0f * glow ) + 1.0f );// a pixel more for smooth sides
			edgeShader.Set( "core", edgeWidth );
			edgeShader.Set( "glow", glow );
			edgeShader.Set( "edgeColor", edgeColor[ 0 ] * alpha, edgeColor[ 1 ] * alpha, edgeColor[ 2 ] * alpha, alpha );
			GLint edgeMvp = edgeShader.FindUniform( "mvp" );
			glDepthMask( GL_FALSE );
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( -1.0f, -4.0f );
			float seeThrough = GetParam( "SeeThrough" )->GetValue();
			for( int pass = seeThrough > 0.0f ? 0 : 1; pass < 2; ++pass )
			{
				glDepthFunc( pass == 0 ? GL_GREATER : GL_LEQUAL );
				edgeShader.Set( "opacity", pass == 0 ? seeThrough : 1.0f );
				for( size_t i = 0; i < draws.size(); ++i )
				{
					Mat4 matrix = projection * modelViews[ i ];
					glUniformMatrix4fv( edgeMvp, 1, GL_FALSE, matrix.m );
					edgeShader.Set( "aspect", preset.screens[ draws[ i ].screen ].aspect );
					glDrawArrays( GL_TRIANGLES, draws[ i ].edgesFirst, draws[ i ].edgesEnd - draws[ i ].edgesFirst );
				}
			}
			glDepthFunc( GL_LESS );
			glDisable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( 0.0f, 0.0f );
		}
		glDepthMask( GL_TRUE );

		for( GLuint unit = 3; unit-- > 0; )
		{
			glActiveTexture( GL_TEXTURE0 + unit );
			glBindTexture( GL_TEXTURE_2D, 0 );
		}
		glDisable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ZERO );
		glDisable( GL_DEPTH_TEST );
	}

	// The host's target is bound again for drawing: copy the scene into it.
	glBindFramebuffer( GL_READ_FRAMEBUFFER, scene.GetGLID() );
	glViewport( currentViewport.x, currentViewport.y, width, height );
	glBlitFramebuffer( 0, 0, width, height, currentViewport.x, currentViewport.y, currentViewport.x + width, currentViewport.y + height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, inputTextures->HostFBO );
	return FF_SUCCESS;
}

void Stage3D::Clean()
{
	ReleaseScreens();
	ClearPreset();
	editedScreen = -1;
	scene.Release();
	levels.FreeGLResources();
	screenShader.FreeGLResources();
	edgeShader.FreeGLResources();
	spill.Release();
	spillShader.FreeGLResources();
}

namespace
{
// A mesh vertex's output position, rounded so that the copies of a vertex shared by two
// triangles compare equal.
using VertexKey = std::pair< long long, long long >;

VertexKey KeyOf( const std::vector< float >& mesh, size_t vertex )
{
	return { std::llround( mesh[ vertex * 4 ] * 1e5 ), std::llround( mesh[ vertex * 4 + 1 ] * 1e5 ) };
}

// Area-weighted centre of a slice's mesh, in output coordinates: the pivot of its chase moves.
std::pair< float, float > CentreOf( const std::vector< float >& mesh )
{
	double area = 0.0, x = 0.0, y = 0.0;
	for( size_t v = 0; v + 11 < mesh.size(); v += 12 )
	{
		double ax = mesh[ v ], ay = mesh[ v + 1 ], bx = mesh[ v + 4 ], by = mesh[ v + 5 ], cx = mesh[ v + 8 ], cy = mesh[ v + 9 ];
		double a = std::abs( ( bx - ax ) * ( cy - ay ) - ( cx - ax ) * ( by - ay ) );
		area += a;
		x += a * ( ax + bx + cx ) / 3.0;
		y += a * ( ay + by + cy ) / 3.0;
	}
	return area > 0.0 ? std::make_pair( (float)( x / area ), (float)( y / area ) ) : std::make_pair( 0.5f, 0.5f );
}

}//End anonymous namespace

void Stage3D::UploadScreens()
{
	std::vector< float > vertices;
	std::vector< float > samples;
	auto vertexCount = [ & ]() { return (GLint)( vertices.size() / FLOATS_PER_VERTEX ); };
	// Mesh vertex i of a slice (4 floats: output x, y, input x, y) at a depth, with its centre
	// and its family's offset.
	std::pair< float, float > centre;// of the slice being added
	float familyOffset = 0.0f;       // same
	float familyHash   = 0.0f;       // same
	auto addVertex = [ & ]( const std::vector< float >& mesh, size_t i, float depth, float side ) {
		vertices.insert( vertices.end(), mesh.begin() + i * 4, mesh.begin() + i * 4 + 4 );
		vertices.insert( vertices.end(), { (float)sliceCount, depth, side, centre.first, centre.second, familyOffset, familyHash } );
	};
	// The edge from mesh vertex i to j, from depth di to dj, as a band of two triangles.
	std::vector< float > edges;
	auto addEdge = [ & ]( const std::vector< float >& mesh, size_t i, float di, size_t j, float dj ) {
		const float corners[ 6 ][ 2 ] = { { 0, -1 }, { 1, -1 }, { 1, 1 }, { 0, -1 }, { 1, 1 }, { 0, 1 } };
		for( const auto& corner : corners )
			edges.insert( edges.end(), { mesh[ i * 4 ], mesh[ i * 4 + 1 ], di, mesh[ j * 4 ], mesh[ j * 4 + 1 ], dj, (float)sliceCount,
			                             centre.first, centre.second, familyOffset, corner[ 0 ], corner[ 1 ] } );
	};
	auto edgeCount = [ & ]() { return (GLint)( edges.size() / FLOATS_PER_EDGE ); };

	for( size_t s = 0; s < preset.screens.size(); ++s )
	{
		const PresetScreen& screen = preset.screens[ s ];
		if( !IsSelected( screen ) )
			continue;
		ScreenDraw draw{ (int)s, vertexCount(), 0, 0 };
		// The backdrop: two triangles over the whole screen.
		const float corners[] = { 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 };
		for( int i = 0; i < 12; i += 2 )
			vertices.insert( vertices.end(), { corners[ i ], corners[ i + 1 ], 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f } );

		std::vector< const PresetSlice* > slices;
		for( const PresetSlice& slice : screen.slices )
			if( IsSelected( screen, slice ) )
				slices.push_back( &slice );
		int firstSlice = sliceCount;
		size_t firstSliceDraw = sliceDraws.size();
		for( const PresetSlice* slice : slices )
		{
			centre = CentreOf( slice->mesh );
			familyOffset = FamilyOffset( screen, *slice );
			familyHash   = NameHash( slice->family );
			SliceDraw sliceDraw{ (int)draws.size(), vertexCount() };
			for( size_t i = 0; i < slice->mesh.size() / 4; ++i )
				addVertex( slice->mesh, i, 1.0f, 0.0f );
			sliceDraw.frontEnd = vertexCount();
			sliceDraw.centre[ 0 ] = ( centre.first - 0.5f ) * screen.aspect;
			sliceDraw.centre[ 1 ] = centre.second - 0.5f;
			sliceDraw.centre[ 2 ] = familyOffset;
			sliceDraw.low[ 0 ] = sliceDraw.low[ 1 ] = 1e9f;
			sliceDraw.high[ 0 ] = sliceDraw.high[ 1 ] = -1e9f;
			for( size_t i = 0; i < slice->mesh.size(); i += 4 )
			{
				float x = ( slice->mesh[ i ] - 0.5f ) * screen.aspect, y = slice->mesh[ i + 1 ] - 0.5f;
				sliceDraw.low[ 0 ]  = std::min( sliceDraw.low[ 0 ], x );
				sliceDraw.low[ 1 ]  = std::min( sliceDraw.low[ 1 ], y );
				sliceDraw.high[ 0 ] = std::max( sliceDraw.high[ 0 ], x );
				sliceDraw.high[ 1 ] = std::max( sliceDraw.high[ 1 ], y );
			}
			sliceDraws.push_back( sliceDraw );
			samples.insert( samples.end(), slice->samples.begin(), slice->samples.end() );
			++sliceCount;
		}

		// Sides along the outline (the edges that belong to a single triangle), and back faces.
		// The outline is also edged on the front and at the back, and so are its sharp corners
		// from one to the other.
		draw.sidesFirst = vertexCount();
		draw.edgesFirst = edgeCount();
		sliceCount      = firstSlice;
		for( size_t k = 0; k < slices.size(); ++k )
		{
			const PresetSlice* slice = slices[ k ];
			SliceDraw& sliceDraw     = sliceDraws[ firstSliceDraw + k ];
			sliceDraw.sidesFirst     = vertexCount();
			centre = CentreOf( slice->mesh );
			familyOffset = FamilyOffset( screen, *slice );
			familyHash   = NameHash( slice->family );
			const std::vector< float >& mesh = slice->mesh;
			size_t count                     = mesh.size() / 4;
			std::map< std::pair< VertexKey, VertexKey >, int > edgeUses;
			for( size_t t = 0; t < count; t += 3 )
				for( size_t k = 0; k < 3; ++k )
				{
					VertexKey a = KeyOf( mesh, t + k ), b = KeyOf( mesh, t + ( k + 1 ) % 3 );
					++edgeUses[ { std::min( a, b ), std::max( a, b ) } ];
				}
			// Each outline vertex: one of its copies in the mesh, and the directions of its
			// outline edges, on screen, leaving it.
			std::map< VertexKey, std::pair< size_t, std::vector< std::pair< float, float > > > > outline;
			auto leave = [ & ]( size_t from, size_t to ) {
				float dx = ( mesh[ to * 4 ] - mesh[ from * 4 ] ) * screen.aspect, dy = mesh[ to * 4 + 1 ] - mesh[ from * 4 + 1 ];
				float length = std::sqrt( dx * dx + dy * dy );
				auto& vertex = outline[ KeyOf( mesh, from ) ];
				vertex.first = from;
				if( length > 0.0f )
					vertex.second.push_back( { dx / length, dy / length } );
			};
			for( size_t t = 0; t < count; t += 3 )
				for( size_t k = 0; k < 3; ++k )
				{
					size_t a = t + k, b = t + ( k + 1 ) % 3;
					VertexKey ka = KeyOf( mesh, a ), kb = KeyOf( mesh, b );
					if( edgeUses[ { std::min( ka, kb ), std::max( ka, kb ) } ] != 1 )
						continue;
					addVertex( mesh, a, 0.0f, 1.0f );
					addVertex( mesh, b, 0.0f, 1.0f );
					addVertex( mesh, b, 1.0f, 1.0f );
					addVertex( mesh, a, 0.0f, 1.0f );
					addVertex( mesh, b, 1.0f, 1.0f );
					addVertex( mesh, a, 1.0f, 1.0f );
					addEdge( mesh, a, 1.0f, b, 1.0f );
					addEdge( mesh, a, 0.0f, b, 0.0f );
					leave( a, b );
					leave( b, a );
				}
			// A corner is sharp where the outline turns by more than 30 degrees: a curve, made
			// of many short edges, gets none.
			for( const auto& vertex : outline )
			{
				const auto& ways = vertex.second.second;
				bool sharp       = ways.size() != 2 || ways[ 0 ].first * ways[ 1 ].first + ways[ 0 ].second * ways[ 1 ].second > -0.866f;
				if( sharp )
					addEdge( mesh, vertex.second.first, 0.0f, vertex.second.first, 1.0f );
			}
			sliceDraw.backFirst = vertexCount();
			for( size_t i = 0; i < count; ++i )
				addVertex( mesh, i, 0.0f, 1.0f );
			sliceDraw.backEnd = vertexCount();
			++sliceCount;
		}
		draw.end      = vertexCount();
		draw.edgesEnd = edgeCount();
		draws.push_back( draw );
	}
	if( draws.empty() )
		return;
	if( sliceCount > 0 )
		levels.Upload( samples, sliceCount );

	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
	ScopedVAOBinding vaoBinding( vao );
	ScopedVBOBinding vboBinding( vbo );
	glBufferData( GL_ARRAY_BUFFER, vertices.size() * sizeof( float ), vertices.data(), GL_STATIC_DRAW );
	const GLint sizes[] = { 2, 2, 1, 1, 1, 3, 1 };
	size_t offset       = 0;
	for( GLuint attribute = 0; attribute < 7; ++attribute )
	{
		glEnableVertexAttribArray( attribute );
		glVertexAttribPointer( attribute, sizes[ attribute ], GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof( float ), (void*)( offset * sizeof( float ) ) );
		offset += sizes[ attribute ];
	}

	if( edges.empty() )
		return;
	glGenVertexArrays( 1, &edgeVao );
	glGenBuffers( 1, &edgeVbo );
	ScopedVAOBinding edgeVaoBinding( edgeVao );
	ScopedVBOBinding edgeVboBinding( edgeVbo );
	glBufferData( GL_ARRAY_BUFFER, edges.size() * sizeof( float ), edges.data(), GL_STATIC_DRAW );
	const GLint edgeSizes[] = { 3, 3, 1, 3, 1, 1 };
	offset                  = 0;
	for( GLuint attribute = 0; attribute < 6; ++attribute )
	{
		glEnableVertexAttribArray( attribute );
		glVertexAttribPointer( attribute, edgeSizes[ attribute ], GL_FLOAT, GL_FALSE, FLOATS_PER_EDGE * sizeof( float ), (void*)( offset * sizeof( float ) ) );
		offset += edgeSizes[ attribute ];
	}
}

void Stage3D::ReleaseScreens()
{
	levels.Release();
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	glDeleteVertexArrays( 1, &edgeVao );
	glDeleteBuffers( 1, &edgeVbo );
	vao = vbo = edgeVao = edgeVbo = 0;
	sliceCount = 0;
	draws.clear();
	sliceDraws.clear();
}
