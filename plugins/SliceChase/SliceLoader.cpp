#include "SliceLoader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <clipper2/clipper.h>
#include <mapbox/earcut.hpp>
#include <pugixml.hpp>

using Point    = std::array< double, 2 >;
using Triangle = std::array< Point, 3 >;

namespace
{
// Bezier segments, if any, are approximated by the polyline through their points.
Clipper2Lib::PathD ReadPoints( pugi::xml_node points )
{
	Clipper2Lib::PathD path;
	for( pugi::xml_node v : points.children( "v" ) )
		path.push_back( { v.attribute( "x" ).as_double(), v.attribute( "y" ).as_double() } );
	return path;
}

pugi::xml_attribute ParamValue( pugi::xml_node owner, const char* group, const char* name )
{
	return owner.find_child_by_attribute( "Params", "name", group )
	    .find_child_by_attribute( "Param", "name", name )
	    .attribute( "value" );
}

// The visible input area of a Slice or Polygon, in composition pixels (origin top-left).
// Polygons have their real outline in InputContour; Slices only have the 4 corners of InputRect.
// Enabled masks are applied: an inverted mask (Resolume's default) keeps what is inside it,
// a non-inverted one cuts it out.
Clipper2Lib::PathsD ReadVisibleArea( pugi::xml_node layer )
{
	using namespace Clipper2Lib;
	pugi::xml_node outline = layer.child( "InputContour" ).child( "points" );
	PathsD area{ ReadPoints( outline ? outline : layer.child( "InputRect" ) ) };

	PathsD keep, cut;
	for( pugi::xml_node mask : layer.children( "SliceMask" ) )
	{
		if( ParamValue( mask, "Input Mask", "Enabled" ).as_int( 1 ) == 0 )
			continue;
		bool inverted = ParamValue( mask, "Input Mask", "Invert" ).as_int( 1 ) != 0;
		for( pugi::xml_node contour : mask.child( "ShapeObject" ).child( "Shape" ).children( "Contour" ) )
			( inverted ? keep : cut ).push_back( ReadPoints( contour.child( "points" ) ) );
	}
	if( !keep.empty() )
		area = Intersect( area, keep, FillRule::NonZero );
	if( !cut.empty() )
		area = Difference( area, cut, FillRule::NonZero );
	return area;
}

// earcut needs each outer ring together with its holes: walk Clipper's tree of outers,
// holes, islands inside holes, and so on. Points are normalised on the way.
void Triangulate( const Clipper2Lib::PolyPathD& outer, double width, double height, std::vector< Triangle >& triangles )
{
	auto normalised = []( const Clipper2Lib::PathD& path, double width, double height ) {
		std::vector< Point > ring;
		for( const Clipper2Lib::PointD& p : path )
			ring.push_back( { p.x / width, 1.0 - p.y / height } );
		return ring;
	};

	std::vector< std::vector< Point > > rings{ normalised( outer.Polygon(), width, height ) };
	for( const auto& hole : outer )
		rings.push_back( normalised( hole->Polygon(), width, height ) );

	// earcut indexes into the rings as if they were concatenated.
	std::vector< Point > points;
	for( const auto& ring : rings )
		points.insert( points.end(), ring.begin(), ring.end() );
	std::vector< uint32_t > indices = mapbox::earcut< uint32_t >( rings );
	for( size_t t = 0; t + 2 < indices.size(); t += 3 )
		triangles.push_back( { points[ indices[ t ] ], points[ indices[ t + 1 ] ], points[ indices[ t + 2 ] ] } );

	for( const auto& hole : outer )
		for( const auto& island : *hole )
			Triangulate( *island, width, height, triangles );
}

// Stores the triangles and spreads the samples over them. Returns false for a shape without area.
bool BuildSlice( const std::vector< Triangle >& triangles, unsigned int seed, PresetSlice& slice )
{
	std::vector< double > cumulativeArea;
	double totalArea = 0.0;
	for( const Triangle& t : triangles )
	{
		const Point &a = t[ 0 ], &b = t[ 1 ], &c = t[ 2 ];
		totalArea += 0.5 * std::abs( ( b[ 0 ] - a[ 0 ] ) * ( c[ 1 ] - a[ 1 ] ) - ( c[ 0 ] - a[ 0 ] ) * ( b[ 1 ] - a[ 1 ] ) );
		cumulativeArea.push_back( totalArea );
	}
	if( totalArea <= 0.0 )
		return false;

	for( const Triangle& t : triangles )
	{
		for( const Point& p : t )
		{
			slice.triangles.push_back( (float)p[ 0 ] );
			slice.triangles.push_back( (float)p[ 1 ] );
		}
	}

	// Stratified area-weighted samples: sample k falls in the triangle covering the
	// k-th 1/N of the total area, at a random point inside it. Seeded so a preset
	// always gives the same samples.
	const int N = SAMPLES_PER_SLICE;
	std::mt19937 rng( seed );
	std::uniform_real_distribution< double > random( 0.0, 1.0 );
	for( int k = 0; k < N; ++k )
	{
		double target = ( k + random( rng ) ) / N * totalArea;
		size_t tri    = std::lower_bound( cumulativeArea.begin(), cumulativeArea.end(), target ) - cumulativeArea.begin();
		tri           = std::min( tri, cumulativeArea.size() - 1 );
		const Point &a = triangles[ tri ][ 0 ], &b = triangles[ tri ][ 1 ], &c = triangles[ tri ][ 2 ];
		double r1 = random( rng ), r2 = random( rng );
		if( r1 + r2 > 1.0 )
		{
			r1 = 1.0 - r1;
			r2 = 1.0 - r2;
		}
		slice.samples.push_back( (float)( a[ 0 ] + r1 * ( b[ 0 ] - a[ 0 ] ) + r2 * ( c[ 0 ] - a[ 0 ] ) ) );
		slice.samples.push_back( (float)( a[ 1 ] + r1 * ( b[ 1 ] - a[ 1 ] ) + r2 * ( c[ 1 ] - a[ 1 ] ) ) );
	}
	return true;
}

}//End anonymous namespace

bool LoadPreset( const std::string& utf8Path, Preset& preset, std::string& error )
{
	preset = Preset();

	// Read through std::filesystem so non-ASCII paths work on Windows too.
	std::ifstream file( std::filesystem::u8path( utf8Path ), std::ios::binary );
	if( !file )
	{
		error = "cannot open " + utf8Path;
		return false;
	}
	std::stringstream content;
	content << file.rdbuf();
	std::string text = content.str();

	pugi::xml_document doc;
	pugi::xml_parse_result parsed = doc.load_buffer( text.data(), text.size() );
	if( !parsed )
	{
		error = std::string( "invalid XML: " ) + parsed.description();
		return false;
	}

	pugi::xml_node setup = doc.child( "XmlState" ).child( "ScreenSetup" );
	pugi::xml_node size  = setup.child( "CurrentCompositionTextureSize" );
	double width         = size.attribute( "width" ).as_double();
	double height        = size.attribute( "height" ).as_double();
	if( width <= 0.0 || height <= 0.0 )
	{
		error = "not an Advanced Output preset (no composition size)";
		return false;
	}

	unsigned int seed = 0;
	for( pugi::xml_node screenNode : setup.child( "screens" ).children( "Screen" ) )
	{
		PresetScreen screen;
		screen.name = screenNode.attribute( "name" ).as_string();
		for( pugi::xml_node layer : screenNode.child( "layers" ).children() )
		{
			std::string type = layer.name();
			if( ( type != "Slice" && type != "Polygon" ) || ParamValue( layer, "Common", "Enabled" ).as_int( 1 ) == 0 )
				continue;

			// A union with nothing turns the area into a tree of outers and holes.
			Clipper2Lib::PolyTreeD tree;
			Clipper2Lib::BooleanOp( Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, ReadVisibleArea( layer ), {}, tree );
			std::vector< Triangle > triangles;
			for( const auto& outer : tree )
				Triangulate( *outer, width, height, triangles );

			PresetSlice slice;
			slice.family = SliceFamily( ParamValue( layer, "Common", "Name" ).as_string() );
			if( BuildSlice( triangles, seed++, slice ) )
				screen.slices.push_back( std::move( slice ) );
		}
		preset.screens.push_back( std::move( screen ) );
	}
	return true;
}

std::string SliceFamily( const std::string& name )
{
	size_t lastNonDigit = name.find_last_not_of( "0123456789" );
	if( lastNonDigit == std::string::npos || lastNonDigit + 1 == name.size() )
		return name;// only digits, or no trailing number
	size_t end = name.find_last_not_of( " _-.", lastNonDigit );
	return end == std::string::npos ? name : name.substr( 0, end + 1 );
}

void AppendSlice( const PresetSlice& slice, SliceGeometry& geometry )
{
	float index = (float)geometry.sliceCount++;
	for( size_t i = 0; i < slice.triangles.size(); i += 2 )
	{
		geometry.vertices.push_back( slice.triangles[ i ] );
		geometry.vertices.push_back( slice.triangles[ i + 1 ] );
		geometry.vertices.push_back( index );
	}
	geometry.samples.insert( geometry.samples.end(), slice.samples.begin(), slice.samples.end() );
}
