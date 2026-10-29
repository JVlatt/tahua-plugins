#pragma once
#include <string>
#include <vector>

// Coordinates are normalised to the composition: (0,0) bottom-left, (1,1) top-right.
static const int SAMPLES_PER_SLICE = 64;

struct PresetSlice
{
	std::string family;          // name without its trailing number, see SliceFamily()
	std::vector< float > triangles;// x, y per vertex, 3 vertices per triangle
	std::vector< float > samples;  // SAMPLES_PER_SLICE points inside the shape, x, y each
	// The same shape placed on its screen: output x, y (normalised to the screen's output size,
	// (0,0) bottom-left) then input x, y per vertex, 3 vertices per triangle.
	std::vector< float > mesh;
};

struct PresetScreen
{
	std::string name;
	float aspect = 16.0f / 9.0f;// output width / height
	std::vector< PresetSlice > slices;
};

// The enabled Slices and Polygons of a Resolume Advanced Output preset (.xml), per screen.
struct Preset
{
	std::vector< PresetScreen > screens;
};

// The selected slices, ready to upload to the GPU.
struct SliceGeometry
{
	int sliceCount = 0;
	std::vector< float > vertices;// x, y, slice index per vertex
	std::vector< float > samples; // SAMPLES_PER_SLICE x sliceCount points, x, y each
};

// Reads every enabled Slice and Polygon of every screen: its input shape, and where that shape
// lands on the screen's output. Slices are mapped by the homography from their input rect to
// their output rect (the Bezier warper is ignored); Polygons piecewise, triangle by triangle of
// their input contour.
// Returns false and fills error if the file cannot be read or is not an Advanced Output preset.
bool LoadPreset( const std::string& utf8Path, Preset& preset, std::string& error );

// Groups numbered slices: "METAL_12", "Wing 3" and "EYE-02" give "METAL", "Wing" and "EYE".
// A name without a trailing number is its own family.
std::string SliceFamily( const std::string& name );

void AppendSlice( const PresetSlice& slice, SliceGeometry& geometry );
