#pragma once
#include <cstdint>
#include <SliceEffect.h>

class SliceGlyph : public SliceEffect
{
public:
	SliceGlyph();

	FFResult Init() override;
	void Update() override;
	FFResult Render( ProcessOpenGLStruct* inputTextures ) override;
	void Clean() override;

private:
	// Where a selected slice shows its glyph.
	struct Slot
	{
		std::vector< float > triangles;  // normalised x, y per vertex
		float center[ 2 ] = {};          // in pixels of the layer
		float scale       = 1.0f;        // pixels per glyph unit
		float box[ 2 ]    = { 1.0f, 1.0f };// half size the area styles fill, in glyph units
		uint32_t seed     = 0;           // fixed per slice
		float offset      = 0.0f;        // 0 to 1, fixed per slice: its shift in the cycle with Spread
		long long cycle   = -1;          // the cycle its glyph was made for
	};

	void PlaceSlots();
	void BuildGlyphs();
	void Upload( const std::vector< float >& vertices, const std::vector< float >& prims );
	void Release();
	// Beats of Resolume's tempo since the plugin started.
	double CountBeats();

	ffglex::FFGLShader glyphShader;
	GLuint vao          = 0;
	GLuint vbo          = 0;
	GLuint primsTexture = 0;// two texels per stroke
	GLsizei vertexCount = 0;

	std::vector< Slot > slots;
	bool glyphsDirty = true;
	// What the slots were placed and the glyphs built with.
	unsigned int placedWidth = 0, placedHeight = 0;
	float placedFit = -1.0f, placedSize = -1.0f;
	float builtStyle = -1.0f, builtComplexity = -1.0f, builtSeed = -1.0f;

	std::shared_ptr< ffglqs::ParamOption > style;
	std::shared_ptr< ffglqs::ParamOption > fit;
	std::shared_ptr< ffglqs::ParamOption > animation;
	std::shared_ptr< ffglqs::ParamOption > cycleBeats;
	std::shared_ptr< ffglqs::ParamBool > change;
	double bars        = 0.0;// Bars seen since the plugin started, counted on each barPhase wrap.
	float lastBarPhase = 0.0f;
};
