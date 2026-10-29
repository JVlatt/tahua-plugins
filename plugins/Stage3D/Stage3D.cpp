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
// extrusion. In Chase mode, each slice moves from where it is towards its target transform
// (move, turn, scale, in the screen's axes, around the slice's centre) as its level goes to 1.
const char screenVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec2 outputPosition;
layout( location = 1 ) in vec2 inputPosition;
layout( location = 2 ) in float slice;
layout( location = 3 ) in float depth;
layout( location = 4 ) in float side;
layout( location = 5 ) in vec2 centre;
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
	vec3 position = vec3( ( outputPosition - 0.5 ) * screen, slice < 0.0 ? -extrude - 0.002 : ( depth - 1.0 ) * extrude );
	if( slice >= 0.0 && mode == 2 )
	{
		float level = texelFetch( levels, ivec2( 0, int( slice ) ), 0 ).r;
		vec3 pivot  = vec3( ( centre - 0.5 ) * screen, 0.0 );
		position    = pivot + move * level + Rotation( turn * level ) * ( ( position - pivot ) * mix( 1.0, scale, level ) );
	}
	gl_Position  = mvp * vec4( position, 1.0 );
	viewPosition = ( modelView * vec4( position, 1.0 ) ).xyz;
	uv           = inputPosition * maxUV;
	vSlice       = int( slice );
	vSide        = int( side );
}
)";

// Textures are premultiplied, and so is what is drawn: blended with ONE, ONE_MINUS_SRC_ALPHA.
// Sides and back faces are lit by a light at the camera, so the volume reads; front faces keep
// their colours untouched.
const char screenFragmentShader[] = R"(
#version 410 core
uniform sampler2D inputTexture;
uniform int mode;
uniform vec4 color;
uniform float backdrop;
uniform vec3 lightPosition;// in view space
uniform float lightIntensity;
uniform int globalIllumination;
in vec2 uv;
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
	// Texture mode shows the layer; in Color and Chase modes (where the layer is the mask) the
	// slices are filled with the colour.
	vec4 surface = mode == 0 ? texture( inputTexture, uv ) : vec4( color.rgb, 1.0 ) * color.a;

	// The face's normal, turned towards the camera: the side that is seen is the side lit.
	vec3 normal = normalize( cross( dFdx( viewPosition ), dFdy( viewPosition ) ) );
	if( dot( normal, viewPosition ) > 0.0 )
		normal = -normal;
	// Base shading, unless turned off so that only the point light shows the slices.
	float light = globalIllumination == 0 ? 0.0 : vSide != 0 ? 0.3 + 0.5 * abs( dot( normal, normalize( viewPosition ) ) ) : 1.0;

	// Point light, added on top: diffuse, fading with the square of the distance.
	vec3 toLight = lightPosition - viewPosition;
	float lightDistance = length( toLight );
	light += lightIntensity * max( dot( normal, toLight / lightDistance ), 0.0 ) / ( 1.0 + lightDistance * lightDistance );

	fragColor = vec4( surface.rgb * light, surface.a );
}
)";

const int FLOATS_PER_VERTEX = 9;// output x, y, input x, y, slice, depth, side, centre x, y
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

	// A point light in the scene, added to the base shading; off at intensity 0.
	unsigned int firstLight = (unsigned int)params.size();
	AddParam( ParamRange::Create( "LightX", 0.0f, { -2.0f, 2.0f } ) );
	AddParam( ParamRange::Create( "LightY", 0.5f, { -2.0f, 2.0f } ) );
	AddParam( ParamRange::Create( "LightZ", 1.0f, { -2.0f, 2.0f } ) );
	AddParam( ParamRange::Create( "LightIntensity", 0.0f, { 0.0f, 5.0f } ) );
	AddParam( ParamBool::Create( "GlobalIllumination", true ) );
	const char* lightNames[] = { "X", "Y", "Z", "Intensity", "Global Illumination" };
	for( unsigned int i = 0; i < 5; ++i )
	{
		SetParamDisplayName( firstLight + i, lightNames[ i ], false );
		SetParamGroup( firstLight + i, "Light" );
	}

	// Orbit around the scene's origin, in scene units: the view does not follow the screens.
	unsigned int firstCamera = (unsigned int)params.size();
	AddParam( ParamRange::Create( "Yaw", 0.0f, { -180.0f, 180.0f } ) );
	AddParam( ParamRange::Create( "Pitch", 0.0f, { -89.0f, 89.0f } ) );
	AddParam( ParamRange::Create( "Distance", 2.5f, { 0.0f, 10.0f } ) );
	// 0 is flat (orthographic), 1 a 90 degree field of view. Distance keeps the framing.
	AddParam( ParamRange::Create( "Perspective", 0.5f, { 0.0f, 1.0f } ) );
	for( unsigned int i = firstCamera; i < params.size(); ++i )
		SetParamGroup( i, "Camera" );

	// Moves, turns and scales all the screens together, around the origin.
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

	// The placement parameters edit the screen picked in Edit Screen. Its elements become the
	// screen names of the loaded preset; the option keeps MAX_SCREENS entries so that the
	// quickstart accepts any index the host sends.
	editScreenIndex = (unsigned int)params.size();
	std::vector< ParamOption::Option > screens;
	for( int i = 0; i < MAX_SCREENS; ++i )
		screens.push_back( { "Screen" + std::to_string( i + 1 ), (float)i } );
	AddParam( ParamOption::Create( "EditScreen", screens ) );
	SetParamDisplayName( editScreenIndex, "Edit Screen", false );
	SetParamElements( editScreenIndex, { "-" }, { 0.0f }, false );
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

	AddSelectionParams();
}

FFResult Stage3D::Init()
{
	if( !levels.Init() )
		return FF_FAIL;
	if( !screenShader.Compile( screenVertexShader, screenFragmentShader ) )
		return FF_FAIL;
	return FF_SUCCESS;
}

void Stage3D::Update()
{
	SelectionUpdate selection = UpdateSelection();
	if( selection.presetLoaded )
		UpdateScreenList();
	UpdatePlacement( selection.presetLoaded );
	UpdateModeVisibility();
	if( !selection.changed )
		return;
	ReleaseScreens();
	UploadScreens();
}

void Stage3D::UpdateScreenList()
{
	std::vector< std::string > names;
	std::vector< float > values;
	for( size_t i = 0; i < preset.screens.size() && i < MAX_SCREENS; ++i )
	{
		names.push_back( preset.screens[ i ].name );
		values.push_back( (float)i );
	}
	if( names.empty() )
	{
		names.push_back( "-" );
		values.push_back( 0.0f );
	}
	SetParamElements( editScreenIndex, names, values, true );
	if( params[ editScreenIndex ]->GetValue() >= (float)names.size() )
	{
		params[ editScreenIndex ]->SetValue( 0.0f );
		RaiseParamEvent( editScreenIndex, FF_EVENT_FLAG_VALUE );
	}
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

	auto total = [ this ]( const char* name ) { return GetParam( name )->GetValue(); };
	float totalSize = total( "TotalSize" );
	Mat4 world      = view * Translate( total( "TotalX" ), total( "TotalY" ), total( "TotalZ" ) ) * Rotate( 1, total( "TotalRotY" ) ) *
	             Rotate( 0, total( "TotalRotX" ) ) * Rotate( 2, total( "TotalRotZ" ) ) * Scale( totalSize, totalSize, totalSize );

	std::vector< Mat4 > modelViews;
	for( const ScreenDraw& draw : draws )
	{
		Placement p = PlacementOf( draw.screen );
		modelViews.push_back( world * Translate( p.x, p.y, p.z ) * Rotate( 1, p.rotY ) * Rotate( 0, p.rotX ) * Rotate( 2, p.rotZ ) * Scale( p.size, p.size, p.size ) );
	}
	// The see-through backdrops go last, from the farthest to the nearest.
	std::vector< size_t > order( draws.size() );
	std::vector< float > depth( draws.size() );
	for( size_t i = 0; i < draws.size(); ++i )
	{
		order[ i ] = i;
		depth[ i ] = modelViews[ i ].Transform( { 0.0f, 0.0f, 0.0f } ).z;
	}
	std::sort( order.begin(), order.end(), [ & ]( size_t a, size_t b ) { return depth[ a ] < depth[ b ]; } );

	float hue = GetParam( "Color" )->GetValue();
	float rgb[ 3 ];
	HSVtoRGB( hue == 1.0f ? 0.0f : hue,
	          GetParam( "Color_saturation" )->GetValue(),
	          GetParam( "Color_brightness" )->GetValue(),
	          rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );
	FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
	float extrude           = GetParam( "Extrude" )->GetValue();

	{
		ScopedFBOBinding fboBinding( scene.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, width, height );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		glEnable( GL_DEPTH_TEST );
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

		ScopedShaderBinding shaderBinding( screenShader.GetGLID() );
		// Bound by hand: ScopedSamplerActivation returns to unit 0, not to the previous unit.
		const GLuint textures[] = { input.Handle, mode == MODE_CHASE && sliceCount > 0 ? levels.Texture() : 0 };
		for( GLuint unit = 0; unit < 2; ++unit )
		{
			glActiveTexture( GL_TEXTURE0 + unit );
			glBindTexture( GL_TEXTURE_2D, textures[ unit ] );
		}
		screenShader.Set( "inputTexture", 0 );
		screenShader.Set( "levels", 1 );
		screenShader.Set( "mode", mode );
		screenShader.Set( "color", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], GetParam( "Color_alpha" )->GetValue() );
		screenShader.Set( "backdrop", GetParam( "Backdrop" )->GetValue() );
		Vec3 light = view.Transform( { GetParam( "LightX" )->GetValue(), GetParam( "LightY" )->GetValue(), GetParam( "LightZ" )->GetValue() } );
		screenShader.Set( "lightPosition", light.x, light.y, light.z );
		screenShader.Set( "lightIntensity", GetParam( "LightIntensity" )->GetValue() );
		screenShader.Set( "globalIllumination", GetParam( "GlobalIllumination" )->GetValue() > 0.5f ? 1 : 0 );
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
		glDepthMask( GL_TRUE );

		for( GLuint unit = 2; unit-- > 0; )
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
	// Mesh vertex i of a slice (4 floats: output x, y, input x, y) at a depth, with its centre.
	std::pair< float, float > centre;// of the slice being added
	auto addVertex = [ & ]( const std::vector< float >& mesh, size_t i, float depth, float side ) {
		vertices.insert( vertices.end(), mesh.begin() + i * 4, mesh.begin() + i * 4 + 4 );
		vertices.insert( vertices.end(), { (float)sliceCount, depth, side, centre.first, centre.second } );
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
			vertices.insert( vertices.end(), { corners[ i ], corners[ i + 1 ], 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.5f, 0.5f } );

		std::vector< const PresetSlice* > slices;
		for( const PresetSlice& slice : screen.slices )
			if( IsSelected( slice ) )
				slices.push_back( &slice );
		int firstSlice = sliceCount;
		for( const PresetSlice* slice : slices )
		{
			centre = CentreOf( slice->mesh );
			for( size_t i = 0; i < slice->mesh.size() / 4; ++i )
				addVertex( slice->mesh, i, 1.0f, 0.0f );
			samples.insert( samples.end(), slice->samples.begin(), slice->samples.end() );
			++sliceCount;
		}

		// Sides along the outline (the edges that belong to a single triangle), and back faces.
		draw.sidesFirst = vertexCount();
		sliceCount      = firstSlice;
		for( const PresetSlice* slice : slices )
		{
			centre = CentreOf( slice->mesh );
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
			for( size_t i = 0; i < count; ++i )
				addVertex( mesh, i, 0.0f, 1.0f );
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
	const GLint sizes[] = { 2, 2, 1, 1, 1, 2 };
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
}
