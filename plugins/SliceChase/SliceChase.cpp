#include "SliceChase.h"
#include <sstream>
#include <ffglex/FFGLScopedFBOBinding.h>
using namespace ffglqs;
using namespace ffglex;

static PluginInstance pluginInstance = Effect::CreatePlugin< SliceChase >( tahua::MakePluginInfo() );

namespace
{
// The quickstart has text parameters but no file parameter: same storage, other type.
class ParamFile : public ParamText
{
public:
	ParamFile( std::string name ) :
		ParamText( std::move( name ) )
	{
		type = FF_TYPE_FILE;
	}
};

// Unused: the quickstart compiles it, but drawing is done by the two shaders below.
const char unusedFragmentShader[] = R"(
void main()
{
	fragColor = vec4( 0.0 );
}
)";

// Pass 1, drawn into a 1 x sliceCount target: row i averages the mask over slice i's samples,
// compares it to the threshold and moves the slice's level (0 = off, 1 = on) towards the result,
// at the speed set by the fade durations. Textures are premultiplied, so the luminance of rgb
// already includes alpha.
const char stateVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec4 vPosition;

void main()
{
	gl_Position = vPosition;
}
)";

const char stateFragmentShader[] = R"(
#version 410 core
uniform sampler2D inputTexture;
uniform sampler2D samples;
uniform sampler2D previousLevels;
uniform vec2 maxUV;
uniform float threshold;
uniform float fadeIn;
uniform float fadeOut;
uniform float deltaTime;
out vec4 fragColor;

void main()
{
	int slice = int( gl_FragCoord.y );
	int count = textureSize( samples, 0 ).x;
	float sum = 0.0;
	for( int i = 0; i < count; ++i )
	{
		vec2 uv = texelFetch( samples, ivec2( i, slice ), 0 ).xy;
		sum += dot( texture( inputTexture, uv * maxUV ).rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
	}
	float target   = sum / float( count ) > threshold ? 1.0 : 0.0;
	float previous = texelFetch( previousLevels, ivec2( 0, slice ), 0 ).r;
	float duration = target > previous ? fadeIn : fadeOut;
	float maxStep  = duration > 0.0 ? deltaTime / duration : 1.0;
	fragColor      = vec4( previous + clamp( target - previous, -maxStep, maxStep ) );
}
)";

// Pass 2: every slice's triangles, at the opacity of its level.
const char sliceVertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec2 position;
layout( location = 1 ) in float slice;
flat out int vSlice;

void main()
{
	gl_Position = vec4( position * 2.0 - 1.0, 0.0, 1.0 );
	vSlice      = int( slice );
}
)";

const char sliceFragmentShader[] = R"(
#version 410 core
uniform sampler2D levels;
uniform vec4 color;
flat in int vSlice;
out vec4 fragColor;

void main()
{
	float level = texelFetch( levels, ivec2( 0, vSlice ), 0 ).r;
	if( level <= 0.0 )
		discard;
	fragColor = vec4( color.rgb, 1.0 ) * color.a * level;
}
)";

}//End anonymous namespace

SliceChase::SliceChase()
{
	SetFragmentShader( unusedFragmentShader );

	slicesFile = std::make_shared< ParamFile >( "Slices" );
	SetFileParamInfo( (unsigned int)params.size(), "Slices", { "xml" }, "" );
	params.push_back( slicesFile );

	AddParam( ParamRange::Create( "Threshold", 0.5f, { 0.0f, 1.0f } ) );
	// Seconds to go from off to on and back. Names become shader uniforms, so the
	// spaced versions are display names only.
	AddParam( ParamRange::Create( "FadeIn", 0.0f, { 0.0f, 10.0f } ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Fade In", false );
	AddParam( ParamRange::Create( "FadeOut", 0.0f, { 0.0f, 10.0f } ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Fade Out", false );
	AddHueColorParam( "Color" );

	// What is unchecked, saved by name so it survives a reordered or edited preset.
	// Hidden: the checkboxes below are the UI. Names become shader uniforms, hence no spaces.
	excludedScreensIndex = (unsigned int)params.size();
	excludedScreens      = ParamText::create( "ExcludedScreens" );
	AddParam( excludedScreens );
	SetParamVisibility( excludedScreensIndex, false, false );
	excludedFamiliesIndex = (unsigned int)params.size();
	excludedFamilies      = ParamText::create( "ExcludedFamilies" );
	AddParam( excludedFamilies );
	SetParamVisibility( excludedFamiliesIndex, false, false );

	// FFGL parameters cannot be added after construction: a pool of checkboxes is declared up
	// front, then renamed and shown for the screens and slice families of the loaded preset.
	firstScreenBox = (unsigned int)params.size();
	for( int i = 0; i < MAX_SCREENS; ++i )
		AddCheckbox( "Screen" + std::to_string( i + 1 ), "Screens" );
	firstFamilyBox = (unsigned int)params.size();
	for( int i = 0; i < MAX_FAMILIES; ++i )
		AddCheckbox( "Family" + std::to_string( i + 1 ), "Slices" );
}

void SliceChase::AddCheckbox( const std::string& name, const std::string& group )
{
	unsigned int index = (unsigned int)params.size();
	AddParam( ParamBool::Create( name, true ) );
	SetParamGroup( index, group );
	SetParamVisibility( index, false, false );
}

FFResult SliceChase::Init()
{
	if( !stateShader.Compile( stateVertexShader, stateFragmentShader ) )
		return FF_FAIL;
	if( !sliceShader.Compile( sliceVertexShader, sliceFragmentShader ) )
		return FF_FAIL;
	return FF_SUCCESS;
}

namespace
{
const char SEPARATOR = '|';

std::set< std::string > Split( const std::string& text )
{
	std::set< std::string > names;
	std::stringstream stream( text );
	std::string name;
	while( std::getline( stream, name, SEPARATOR ) )
	{
		if( !name.empty() )
			names.insert( name );
	}
	return names;
}

std::string Join( const std::set< std::string >& names )
{
	std::string text;
	for( const std::string& name : names )
		text += ( text.empty() ? "" : std::string( 1, SEPARATOR ) ) + name;
	return text;
}

}//End anonymous namespace

void SliceChase::Update()
{
	// The host changes the file or restores the saved exclusions (composition load, undo):
	// the checkboxes follow. Otherwise a checkbox that differs from the exclusions was clicked.
	bool reload   = slicesFile->text != loadedPath;
	bool restored = excludedScreens->text != Join( screenExclusions ) || excludedFamilies->text != Join( familyExclusions );
	if( reload )
	{
		loadedPath = slicesFile->text;
		preset     = Preset();
		std::string error;
		if( !loadedPath.empty() && !LoadPreset( loadedPath, preset, error ) )
			FFGLLog::LogToHost( ( "Tahua SliceChase: " + error ).c_str() );
		if( preset.screens.size() > MAX_SCREENS )
			FFGLLog::LogToHost( "Tahua SliceChase: more screens than checkboxes, the last ones are always on" );
	}
	if( restored )
	{
		screenExclusions = Split( excludedScreens->text );
		familyExclusions = Split( excludedFamilies->text );
	}
	bool toggled = false;
	if( !reload && !restored )
	{
		toggled |= ReadCheckboxes( firstScreenBox, shownScreens, screenExclusions );
		toggled |= ReadCheckboxes( firstFamilyBox, shownFamilies, familyExclusions );
	}
	if( !reload && !restored && !toggled )
		return;

	shownScreens.clear();
	std::set< std::string > families;
	for( const PresetScreen& screen : preset.screens )
	{
		if( shownScreens.size() < MAX_SCREENS )
			shownScreens.push_back( screen.name );
		if( screenExclusions.count( screen.name ) == 0 )
			for( const PresetSlice& slice : screen.slices )
				families.insert( slice.family );
	}
	if( reload && families.size() > MAX_FAMILIES )
		FFGLLog::LogToHost( "Tahua SliceChase: more slice families than checkboxes, the last ones are always on" );
	shownFamilies.assign( families.begin(), families.end() );
	shownFamilies.resize( std::min( shownFamilies.size(), (size_t)MAX_FAMILIES ) );

	ShowCheckboxes( firstScreenBox, MAX_SCREENS, shownScreens, screenExclusions );
	ShowCheckboxes( firstFamilyBox, MAX_FAMILIES, shownFamilies, familyExclusions );
	WriteExclusions( excludedScreensIndex, *excludedScreens, screenExclusions );
	WriteExclusions( excludedFamiliesIndex, *excludedFamilies, familyExclusions );

	SliceGeometry geometry;
	for( const PresetScreen& screen : preset.screens )
	{
		if( screenExclusions.count( screen.name ) != 0 )
			continue;
		for( const PresetSlice& slice : screen.slices )
			if( familyExclusions.count( slice.family ) == 0 )
				AppendSlice( slice, geometry );
	}
	ReleaseSlices();
	if( geometry.sliceCount > 0 )
		UploadSlices( geometry );
}

bool SliceChase::ReadCheckboxes( unsigned int first, const std::vector< std::string >& names, std::set< std::string >& exclusions )
{
	bool changed = false;
	for( size_t i = 0; i < names.size(); ++i )
	{
		bool checked = params[ first + i ]->GetValue() > 0.5f;
		if( checked == ( exclusions.count( names[ i ] ) == 0 ) )
			continue;
		if( checked )
			exclusions.erase( names[ i ] );
		else
			exclusions.insert( names[ i ] );
		changed = true;
	}
	return changed;
}

void SliceChase::ShowCheckboxes( unsigned int first, int poolSize, const std::vector< std::string >& names, const std::set< std::string >& exclusions )
{
	for( int i = 0; i < poolSize; ++i )
	{
		unsigned int index = first + i;
		bool shown         = i < (int)names.size();
		SetParamVisibility( index, shown, true );
		if( !shown )
			continue;
		SetParamDisplayName( index, names[ i ], true );
		float value = exclusions.count( names[ i ] ) == 0 ? 1.0f : 0.0f;
		if( params[ index ]->GetValue() != value )
		{
			params[ index ]->SetValue( value );
			RaiseParamEvent( index, FF_EVENT_FLAG_VALUE );
		}
	}
}

void SliceChase::WriteExclusions( unsigned int index, ParamText& param, const std::set< std::string >& exclusions )
{
	std::string text = Join( exclusions );
	if( param.text == text )
		return;
	param.text = text;
	RaiseParamEvent( index, FF_EVENT_FLAG_VALUE );
}

FFResult SliceChase::Render( ProcessOpenGLStruct* inputTextures )
{
	if( inputTextures->numInputTextures < 1 || inputTextures->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( sliceCount == 0 )
		return FF_SUCCESS;

	// Pass 1: new level of each slice, from the previous frame's levels. The two level
	// targets swap roles every frame.
	FFGLFBO& previousLevels = levels[ currentLevels ];
	currentLevels           = 1 - currentLevels;
	{
		ScopedFBOBinding fboBinding( levels[ currentLevels ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, 1, sliceCount );

		ScopedShaderBinding shaderBinding( stateShader.GetGLID() );
		// ScopedSamplerActivation returns to unit 0, not to the previous unit: with three units,
		// scoped bindings would unbind the wrong one and leave a texture bound for the host.
		const GLuint textures[] = { inputTextures->inputTextures[ 0 ]->Handle, samplesTexture, previousLevels.GetTextureInfo().Handle };
		for( GLuint unit = 0; unit < 3; ++unit )
		{
			glActiveTexture( GL_TEXTURE0 + unit );
			glBindTexture( GL_TEXTURE_2D, textures[ unit ] );
		}

		FFGLTexCoords maxCoords = GetMaxGLTexCoords( *inputTextures->inputTextures[ 0 ] );
		stateShader.Set( "inputTexture", 0 );
		stateShader.Set( "samples", 1 );
		stateShader.Set( "previousLevels", 2 );
		stateShader.Set( "maxUV", maxCoords.s, maxCoords.t );
		stateShader.Set( "threshold", GetParam( "Threshold" )->GetValue() );
		stateShader.Set( "fadeIn", GetParam( "FadeIn" )->GetValue() );
		stateShader.Set( "fadeOut", GetParam( "FadeOut" )->GetValue() );
		stateShader.Set( "deltaTime", deltaTime );
		quad.Draw();

		for( GLuint unit = 3; unit-- > 0; )
		{
			glActiveTexture( GL_TEXTURE0 + unit );
			glBindTexture( GL_TEXTURE_2D, 0 );
		}
	}
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );

	// Pass 2: draw the slices that are not off.
	float hue = GetParam( "Color" )->GetValue();
	float rgb[ 3 ];
	HSVtoRGB( hue == 1.0f ? 0.0f : hue,
	          GetParam( "Color_saturation" )->GetValue(),
	          GetParam( "Color_brightness" )->GetValue(),
	          rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );

	ScopedShaderBinding shaderBinding( sliceShader.GetGLID() );
	ScopedSamplerActivation activateSampler0( 0 );
	Scoped2DTextureBinding levelsBinding( levels[ currentLevels ].GetTextureInfo().Handle );
	sliceShader.Set( "levels", 0 );
	sliceShader.Set( "color", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], GetParam( "Color_alpha" )->GetValue() );

	ScopedVAOBinding vaoBinding( vao );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	return FF_SUCCESS;
}

void SliceChase::Clean()
{
	ReleaseSlices();
	loadedPath.clear();
	preset = Preset();
	stateShader.FreeGLResources();
	sliceShader.FreeGLResources();
}

void SliceChase::UploadSlices( const SliceGeometry& geometry )
{
	sliceCount  = geometry.sliceCount;
	vertexCount = (GLsizei)( geometry.vertices.size() / 3 );

	// 32-bit levels: a long fade moves them by tiny steps each frame. They start off.
	for( FFGLFBO& fbo : levels )
	{
		fbo.Initialise( 1, sliceCount, GL_R32F );
		ScopedFBOBinding fboBinding( fbo.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}

	glGenTextures( 1, &samplesTexture );
	{
		Scoped2DTextureBinding textureBinding( samplesTexture );
		// texelFetch only, but the texture must still be complete: no mipmaps.
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RG32F, SAMPLES_PER_SLICE, sliceCount, 0, GL_RG, GL_FLOAT, geometry.samples.data() );
	}

	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
	ScopedVAOBinding vaoBinding( vao );
	ScopedVBOBinding vboBinding( vbo );
	glBufferData( GL_ARRAY_BUFFER, geometry.vertices.size() * sizeof( float ), geometry.vertices.data(), GL_STATIC_DRAW );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (void*)0 );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (void*)( 2 * sizeof( float ) ) );
}

void SliceChase::ReleaseSlices()
{
	for( FFGLFBO& fbo : levels )
		fbo.Release();
	glDeleteTextures( 1, &samplesTexture );
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	samplesTexture = vao = vbo = 0;
	vertexCount = sliceCount = 0;
}
