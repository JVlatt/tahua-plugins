#include "SliceChase.h"
using namespace ffglqs;
using namespace ffglex;

static PluginInstance pluginInstance = Effect::CreatePlugin< SliceChase >( tahua::MakePluginInfo() );

namespace
{
// Unused: the quickstart compiles it, but drawing is done by ChaseLevels and the shaders below.
const char unusedFragmentShader[] = R"(
void main()
{
	fragColor = vec4( 0.0 );
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

	AddSlicesFileParam();
	AddParam( ParamRange::Create( "Threshold", 0.5f, { 0.0f, 1.0f } ) );
	// Seconds to go from off to on and back. Names become shader uniforms, so the
	// spaced versions are display names only.
	AddParam( ParamRange::Create( "FadeIn", 0.0f, { 0.0f, 10.0f } ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Fade In", false );
	AddParam( ParamRange::Create( "FadeOut", 0.0f, { 0.0f, 10.0f } ) );
	SetParamDisplayName( (unsigned int)params.size() - 1, "Fade Out", false );
	AddHueColorParam( "Color" );
	AddSelectionParams();
}

FFResult SliceChase::Init()
{
	if( !levels.Init() )
		return FF_FAIL;
	if( !sliceShader.Compile( sliceVertexShader, sliceFragmentShader ) )
		return FF_FAIL;
	return FF_SUCCESS;
}

void SliceChase::Update()
{
	if( !UpdateSelection().changed )
		return;

	SliceGeometry geometry;
	for( const PresetScreen& screen : preset.screens )
	{
		if( !IsSelected( screen ) )
			continue;
		for( const PresetSlice& slice : screen.slices )
			if( IsSelected( slice ) )
				AppendSlice( slice, geometry );
	}
	ReleaseSlices();
	if( geometry.sliceCount > 0 )
		UploadSlices( geometry );
}

FFResult SliceChase::Render( ProcessOpenGLStruct* inputTextures )
{
	if( inputTextures->numInputTextures < 1 || inputTextures->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( sliceCount == 0 )
		return FF_SUCCESS;

	// Pass 1: new level of each slice.
	levels.Update( *inputTextures->inputTextures[ 0 ],
	               { GetParam( "Threshold" )->GetValue(), GetParam( "FadeIn" )->GetValue(), GetParam( "FadeOut" )->GetValue(), deltaTime },
	               quad );
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
	Scoped2DTextureBinding levelsBinding( levels.Texture() );
	sliceShader.Set( "levels", 0 );
	sliceShader.Set( "color", rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], GetParam( "Color_alpha" )->GetValue() );

	ScopedVAOBinding vaoBinding( vao );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	return FF_SUCCESS;
}

void SliceChase::Clean()
{
	ReleaseSlices();
	ClearPreset();
	levels.FreeGLResources();
	sliceShader.FreeGLResources();
}

void SliceChase::UploadSlices( const SliceGeometry& geometry )
{
	sliceCount  = geometry.sliceCount;
	vertexCount = (GLsizei)( geometry.vertices.size() / 3 );
	levels.Upload( geometry.samples, sliceCount );

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
	levels.Release();
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
	vao = vbo = 0;
	vertexCount = sliceCount = 0;
}
