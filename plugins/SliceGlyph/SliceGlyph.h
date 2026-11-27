#pragma once
#include <cstdint>
#include <SliceEffect.h>
#include "Glyphs.h"

class SliceGlyph : public SliceEffect
{
public:
	SliceGlyph();

	FFResult Init() override;
	void Update() override;
	FFResult Render( ProcessOpenGLStruct* inputTextures ) override;
	void Clean() override;

private:
	// Where a selected slice, a family of slices, or all of them, show their glyph.
	struct Slot
	{
		std::vector< float > triangles;  // normalised x, y per vertex, of all its slices
		float center[ 2 ] = {};          // in pixels of the layer
		float scale       = 1.0f;        // pixels per glyph unit
		float box[ 2 ]    = { 1.0f, 1.0f };// half size the area styles fill, in glyph units
		bool shaped       = false;       // Fit Shape: the glyph follows field
		glyphs::Field field;             // distance to the outline of its slices
		uint32_t seed     = 0;           // fixed per slot
		float offset      = 0.0f;        // 0 to 1, fixed per slot: its shift in the cycle with Spread
		long long cycle   = -1;          // the cycle its glyph was made for
		std::vector< glyphs::Prim > glyph;
		bool stale = true;// glyph must be generated again

		// Its glyph as texels: strokes, then grid cells, then their index lists.
		std::vector< float > block;
		float gridHalf[ 2 ] = {};// half size of its grid of cells, in glyph units
		int grid[ 2 ]       = {};// cells across, up
		int cellOffset      = 0; // texels from the block start
		int indexOffset     = 0;
		int blockStart      = 0; // texel in the data texture
		int capacity        = 0; // texels it may grow to in place
		int firstVertex     = 0;
	};

	void PlaceSlots();
	// Generates the glyph again if stale, then its block.
	void BuildBlock( Slot& slot );
	void WriteVertices( const Slot& slot, std::vector< float >& out ) const;
	// Places every block in a new texture and vertex buffer.
	void Layout();
	// Writes one block and its vertices in place; false if it no longer fits.
	bool Rewrite( Slot& slot );
	void Upload( const std::vector< float >& vertices, const std::vector< float >& data );
	void Release();
	// Beats of Resolume's tempo since the plugin started.
	double CountBeats();

	ffglex::FFGLShader glyphShader;
	GLuint vao         = 0;
	GLuint vbo         = 0;
	GLuint dataTexture  = 0;// the blocks of every glyph
	GLsizei vertexCount = 0;

	std::vector< Slot > slots;
	bool glyphsDirty = true;// every block must be built again and laid out
	// What the slots were placed and the glyphs built with.
	unsigned int placedWidth = 0, placedHeight = 0;
	float placedGroup = -1.0f, placedFit = -1.0f, placedSize = -1.0f;
	float builtStyle = -1.0f, builtComplexity = -1.0f, builtSeed = -1.0f, builtThickness = -1.0f, builtGlow = -1.0f;

	std::shared_ptr< ffglqs::ParamOption > style;
	std::shared_ptr< ffglqs::ParamOption > group;
	std::shared_ptr< ffglqs::ParamOption > fit;
	std::shared_ptr< ffglqs::ParamOption > animation;
	std::shared_ptr< ffglqs::ParamOption > cycleBeats;
	std::shared_ptr< ffglqs::ParamBool > change;
	double bars        = 0.0;// Bars seen since the plugin started, counted on each barPhase wrap.
	float lastBarPhase = 0.0f;
};
