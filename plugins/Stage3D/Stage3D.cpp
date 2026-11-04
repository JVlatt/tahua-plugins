#include "Stage3D.h"
#include <algorithm>
#include <cmath>
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
// (move, turn, scale, in the screen's axes, around the slice's centre) as its level goes to 1.
const char screenVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec2 outputPosition;
layout( location = 1 ) in vec2 inputPosition;
layout( location = 2 ) in float slice;
layout( location = 3 ) in float depth;
layout( location = 4 ) in float side;
layout( location = 5 ) in vec3 centre;
uniform mat4 mvp;
uniform mat4 modelView;
uniform float aspect;
uniform float extrude;
uniform vec2 maxUV;
uniform int mode;
uniform sampler2D levels;
uniform vec3 move;
uniform vec3 turn;
uniform float scale;
out vec2 uv;
out vec2 spillUV;// in the spill texture, which has no padding
out vec3 viewPosition;
flat out int vSlice;
flat out int vSide;

// Same order as the screens: around z, then x, then y.
mat3 Rotation( vec3 degrees )
{
	vec3 c = cos( radians( degrees ) ), s = sin( radians( degrees ) );
	mat3 x = mat3( 1.0, 0.0, 0.0, 0.0, c.x, s.x, 0.0, -s.x, c.x );
	mat3 y = mat3( c.y, 0.0, -s.y, 0.0, 1.0, 0.0, s.y, 0.0, c.y );
	mat3 z = mat3( c.z, s.z, 0.0, -s.z, c.z, 0.0, 0.0, 0.0, 1.0 );
	return y * x * z;
}

void main()
{
	vec2 screen   = vec2( aspect, 1.0 );
	vec3 position = vec3( ( outputPosition - 0.5 ) * screen, slice < 0.0 ? -extrude - 0.002 : ( depth - 1.0 ) * extrude + centre.z );
	if( slice >= 0.0 && mode == 2 )
	{
		float level = texelFetch( levels, ivec2( 0, int( slice ) ), 0 ).r;
		vec3 pivot  = vec3( ( centre.xy - 0.5 ) * screen, centre.z );
		position    = pivot + move * level + Rotation( turn * level ) * ( ( position - pivot ) * mix( 1.0, scale, level ) );
	}
	gl_Position  = mvp * vec4( position, 1.0 );
	viewPosition = ( modelView * vec4( position, 1.0 ) ).xyz;
	uv           = inputPosition * maxUV;
	spillUV      = inputPosition;
	vSlice      = int( slice );
	vSide        = int( side );
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
uniform int globalIllumination;
uniform vec4 baseColor;
uniform vec3 emissive;// the layer's glow, per channel
uniform float bounce;// 0 when the glow lights nothing
uniform float spread;// blur radius, in layer heights
uniform float specular;
uniform float glossiness;
uniform float fresnel;
in vec2 uv;
in vec2 spillUV;
in vec3 viewPosition;
flat in int vSlice;
flat in int vSide;
out vec4 fragColor;

void main()
{
	if( vSlice < 0 )
	{
		if( backdrop <= 0.0 )
			discard;
		fragColor = vec4( vec3( 0.25 ), 1.0 ) * backdrop;
		return;
	}
	// The face's normal, turned towards the camera: the side that is seen is the side lit.
	vec3 normal = normalize( cross( dFdx( viewPosition ), dFdy( viewPosition ) ) );
	if( dot( normal, viewPosition ) > 0.0 )
		normal = -normal;
	// Base shading, unless turned off so that only the point light shows the slices.
	vec3 light = globalColor * ( globalIllumination == 0 ? 0.0 : vSide != 0 ? 0.3 + 0.5 * abs( dot( normal, normalize( viewPosition ) ) ) : 1.0 );

	// Point light, added on top in its colour: diffuse, fading with the square of the distance.
	vec3 toLight = lightPosition - viewPosition;
	float lightDistance = length( toLight );
	light += lightColor * lightIntensity * max( dot( normal, toLight / lightDistance ), 0.0 ) / ( 1.0 + lightDistance * lightDistance );

	// In Color and Chase modes (where the layer is the mask) the slices are filled with the colour.
	if( mode != 0 )
	{
		vec4 surface = vec4( color.rgb, 1.0 ) * color.a;
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
	vec4 layer   = texture( inputTexture, uv );
	vec4 surface = vec4( baseColor.rgb * baseColor.a * received + layer.rgb * emissive, baseColor.a + layer.a * ( 1.0 - baseColor.a ) );

	// Glass. Seen edge-on, the face turns into a mirror of the light it receives, opaque.
	vec3 toEye = -normalize( viewPosition );
	surface    = mix( surface, vec4( received, 1.0 ), fresnel * pow( 1.0 - max( dot( normal, toEye ), 0.0 ), 3.0 ) );
	// The point light's highlight, as opaque as it is bright.
	vec3 halfway    = normalize( toLight / lightDistance + toEye );
	vec3 highlight = lightColor * specular * lightIntensity * pow( max( dot( normal, halfway ), 0.0 ), exp2( 1.0 + 10.0 * glossiness ) ) / ( 1.0 + lightDistance * lightDistance );
	fragColor      = vec4( surface.rgb + highlight, min( surface.a + max( highlight.r, max( highlight.g, highlight.b ) ), 1.0 ) );
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

const int FLOATS_PER_VERTEX = 10;// output x, y, input x, y, slice, depth, side, centre x, y, z
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

	// Texture mode: the slices are made of the base colour, lit, and the layer glows on top of
	// it (Emissive, tinted by its colour), spilling onto what is around it (Bounce, Spread). Base Color's
	// alpha is the opacity of the slices; it starts dark grey, so that the glow shows.
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
	for( unsigned int i = materialIndex; i < params.size(); ++i )
		SetParamGroup( i, "Material" );

	// A point light in the scene, added to the base shading; off at intensity 0.
	unsigned int firstLight = (unsigned int)params.size();
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
	for( unsigned int i = firstLight; i < params.size(); ++i )
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
	if( !screenShader.Compile( screenVertexShader, screenFragmentShader ) )
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
	for( unsigned int i = materialIndex; i < materialIndex + 14; ++i )
		SetParamVisibility( i, mode == MODE_TEXTURE, true );
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
		levels.Update( input, { GetParam( "Threshold" )->GetValue(), smooth, smooth, deltaTime }, quad );
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
	float lightColor[ 3 ], globalColor[ 3 ];
	hueColor( "LightColor", lightColor );
	hueColor( "GlobalColor", globalColor );
	FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );

	// Texture mode: the layer copied without its padding, and mipmapped, for its glow to spill.
	float bounce = GetParam( "Bounce" )->GetValue();
	bool spilled = mode == MODE_TEXTURE && bounce > 0.0f;
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
		Vec3 light = view.Transform( { GetParam( "LightX" )->GetValue(), GetParam( "LightY" )->GetValue(), GetParam( "LightZ" )->GetValue() } );
		screenShader.Set( "lightPosition", light.x, light.y, light.z );
		screenShader.Set( "lightIntensity", GetParam( "LightIntensity" )->GetValue() * GetParam( "LightColor_alpha" )->GetValue() );
		screenShader.Set( "lightColor", lightColor[ 0 ], lightColor[ 1 ], lightColor[ 2 ] );
		float global = GetParam( "GlobalColor_alpha" )->GetValue();
		screenShader.Set( "globalColor", globalColor[ 0 ] * global, globalColor[ 1 ] * global, globalColor[ 2 ] * global );
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
		screenShader.Set( "extrude", extrude );
		screenShader.Set( "maxUV", maxCoords.s, maxCoords.t );
		screenShader.Set( "move", GetParam( "MoveX" )->GetValue(), GetParam( "MoveY" )->GetValue(), GetParam( "MoveZ" )->GetValue() );
		screenShader.Set( "turn", GetParam( "TurnX" )->GetValue(), GetParam( "TurnY" )->GetValue(), GetParam( "TurnZ" )->GetValue() );
		screenShader.Set( "scale", GetParam( "Scale" )->GetValue() );

		ScopedVAOBinding vaoBinding( vao );
		GLint mvp = screenShader.FindUniform( "mvp" ), modelView = screenShader.FindUniform( "modelView" );
		auto setScreen = [ & ]( size_t i ) {
			Mat4 matrix = projection * modelViews[ i ];
			glUniformMatrix4fv( mvp, 1, GL_FALSE, matrix.m );
			glUniformMatrix4fv( modelView, 1, GL_FALSE, modelViews[ i ].m );
			screenShader.Set( "aspect", preset.screens[ draws[ i ].screen ].aspect );
		};
		float opacity = GetParam( mode == MODE_TEXTURE ? "BaseColor_alpha" : "Color_alpha" )->GetValue();
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
	auto addVertex = [ & ]( const std::vector< float >& mesh, size_t i, float depth, float side ) {
		vertices.insert( vertices.end(), mesh.begin() + i * 4, mesh.begin() + i * 4 + 4 );
		vertices.insert( vertices.end(), { (float)sliceCount, depth, side, centre.first, centre.second, familyOffset } );
	};

	for( size_t s = 0; s < preset.screens.size(); ++s )
	{
		const PresetScreen& screen = preset.screens[ s ];
		if( !IsSelected( screen ) )
			continue;
		ScreenDraw draw{ (int)s, vertexCount(), 0, 0 };
		// The backdrop: two triangles over the whole screen.
		const float corners[] = { 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 };
		for( int i = 0; i < 12; i += 2 )
			vertices.insert( vertices.end(), { corners[ i ], corners[ i + 1 ], 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f } );

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
		draw.sidesFirst = vertexCount();
		sliceCount      = firstSlice;
		for( size_t k = 0; k < slices.size(); ++k )
		{
			const PresetSlice* slice = slices[ k ];
			SliceDraw& sliceDraw     = sliceDraws[ firstSliceDraw + k ];
			sliceDraw.sidesFirst     = vertexCount();
			centre = CentreOf( slice->mesh );
			familyOffset = FamilyOffset( screen, *slice );
			const std::vector< float >& mesh = slice->mesh;
			size_t count                     = mesh.size() / 4;
			std::map< std::pair< VertexKey, VertexKey >, int > edgeUses;
			for( size_t t = 0; t < count; t += 3 )
				for( size_t k = 0; k < 3; ++k )
				{
					VertexKey a = KeyOf( mesh, t + k ), b = KeyOf( mesh, t + ( k + 1 ) % 3 );
					++edgeUses[ { std::min( a, b ), std::max( a, b ) } ];
				}
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
				}
			sliceDraw.backFirst = vertexCount();
			for( size_t i = 0; i < count; ++i )
				addVertex( mesh, i, 0.0f, 1.0f );
			sliceDraw.backEnd = vertexCount();
			++sliceCount;
		}
		draw.end = vertexCount();
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
	const GLint sizes[] = { 2, 2, 1, 1, 1, 3 };
	size_t offset       = 0;
	for( GLuint attribute = 0; attribute < 6; ++attribute )
	{
		glEnableVertexAttribArray( attribute );
		glVertexAttribPointer( attribute, sizes[ attribute ], GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof( float ), (void*)( offset * sizeof( float ) ) );
		offset += sizes[ attribute ];
	}
}

void Stage3D::ReleaseScreens()
{
	levels.Release();
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	vao = vbo  = 0;
	sliceCount = 0;
	draws.clear();
	sliceDraws.clear();
}
