#include "Glyphs.h"
#include <algorithm>
#include <cmath>

namespace glyphs
{
const char* const STYLE_NAMES[ STYLE_COUNT + 1 ] = { "Futhark", "Bindrune", "Rune Circle", "Stave", "Ogham", "Circuit",
	                                                 "Chip", "Sigil", "Constellation", "Maze", "HUD", "Mix" };

namespace
{
const float PI       = 3.14159265f;
const float TAU      = 6.28318531f;
const float NO_STAVE = 99.0f;

// PCG-style generator: the same seed always gives the same sequence.
struct Rng
{
	uint32_t state;
	explicit Rng( uint32_t seed ) : state( seed * 747796405u + 2891336453u )
	{
		Next();
	}
	uint32_t Next()
	{
		state      = state * 747796405u + 2891336453u;
		uint32_t w = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
		return ( w >> 22u ) ^ w;
	}
	float Uniform()
	{
		return (float)( Next() >> 8 ) / 16777216.0f;
	}
	float Range( float low, float high )
	{
		return low + ( high - low ) * Uniform();
	}
	int Int( int count )
	{
		return (int)( Next() % (uint32_t)count );
	}
	bool Chance( float p )
	{
		return Uniform() < p;
	}
};

struct Vec
{
	float x, y;
};

// Collects strokes through a placement: mirror, scale, rotation, then offset.
class Builder
{
public:
	std::vector< Prim > prims;

	void Place( float scale_ = 1.0f, float angle_ = 0.0f, float x = 0.0f, float y = 0.0f, bool mirror_ = false )
	{
		scale  = scale_;
		angle  = angle_;
		offset = { x, y };
		mirror = mirror_;
	}
	void Line( float x0, float y0, float x1, float y1 )
	{
		Vec a = Map( x0, y0 ), b = Map( x1, y1 );
		prims.push_back( { a.x, a.y, b.x, b.y, 0.0f, LINE } );
	}
	// x, y pairs, shifted by dx.
	void Polyline( const std::vector< float >& points, float dx = 0.0f, bool closed = false )
	{
		size_t n = points.size() / 2;
		for( size_t i = 0; i + 1 < n; ++i )
			Line( points[ 2 * i ] + dx, points[ 2 * i + 1 ], points[ 2 * i + 2 ] + dx, points[ 2 * i + 3 ] );
		if( closed && n > 2 )
			Line( points[ 2 * n - 2 ] + dx, points[ 2 * n - 1 ], points[ 0 ] + dx, points[ 1 ] );
	}
	void Arc( float x, float y, float r, float from, float to )
	{
		Vec c = Map( x, y );
		prims.push_back( { c.x, c.y, MapAngle( from ), MapAngle( to ), r * scale, ARC } );
	}
	void Ring( float x, float y, float r )
	{
		Arc( x, y, r, 0.0f, TAU );
	}
	void Dot( float x, float y, float r )
	{
		Vec c = Map( x, y );
		prims.push_back( { c.x, c.y, 0.0f, 0.0f, r * scale, DOT } );
	}

private:
	Vec Map( float x, float y ) const
	{
		if( mirror )
			x = -x;
		x *= scale;
		y *= scale;
		float c = std::cos( angle ), s = std::sin( angle );
		return { c * x - s * y + offset.x, s * x + c * y + offset.y };
	}
	float MapAngle( float a ) const
	{
		return ( mirror ? PI - a : a ) + angle;
	}

	float scale  = 1.0f;
	float angle  = 0.0f;
	Vec offset   = { 0.0f, 0.0f };
	bool mirror  = false;
};

// The 24 runes of the Elder Futhark, as polylines in x -0.5 to 0.5, y -1 to 1.
struct Rune
{
	float staveX;// x of its vertical stave, NO_STAVE without one
	std::vector< std::vector< float > > strokes;
};

const std::vector< Rune >& Runes()
{
	static const std::vector< Rune > runes = {
		{ -0.3f, { { -0.3f, -1, -0.3f, 1 }, { -0.3f, 0.4f, 0.3f, 0.9f }, { -0.3f, -0.1f, 0.3f, 0.4f } } },// fehu
		{ -0.3f, { { -0.3f, -1, -0.3f, 1, 0.3f, 0.4f, 0.3f, -1 } } },                                  // uruz
		{ -0.2f, { { -0.2f, -1, -0.2f, 1 }, { -0.2f, 0.5f, 0.3f, 0, -0.2f, -0.5f } } },               // thurisaz
		{ -0.3f, { { -0.3f, -1, -0.3f, 1, 0.3f, 0.5f }, { -0.3f, 0.5f, 0.3f, 0 } } },                 // ansuz
		{ -0.3f, { { -0.3f, -1, -0.3f, 1, 0.3f, 0.5f, -0.3f, 0, 0.3f, -1 } } },                       // raidho
		{ NO_STAVE, { { 0.3f, 0.6f, -0.3f, 0, 0.3f, -0.6f } } },                                       // kaunan
		{ NO_STAVE, { { -0.5f, -1, 0.5f, 1 }, { -0.5f, 1, 0.5f, -1 } } },                              // gebo
		{ -0.3f, { { -0.3f, -1, -0.3f, 1, 0.3f, 0.6f, -0.3f, 0.2f } } },                               // wunjo
		{ -0.35f, { { -0.35f, -1, -0.35f, 1 }, { 0.35f, -1, 0.35f, 1 }, { -0.35f, 0.3f, 0.35f, -0.3f } } },// hagalaz
		{ 0.0f, { { 0, -1, 0, 1 }, { -0.35f, 0.3f, 0.35f, -0.3f } } },                                  // naudiz
		{ 0.0f, { { 0, -1, 0, 1 } } },                                                                   // isaz
		{ NO_STAVE, { { -0.1f, 0.8f, -0.45f, 0.35f, -0.1f, -0.1f }, { 0.1f, 0.1f, 0.45f, -0.35f, 0.1f, -0.8f } } },// jera
		{ 0.0f, { { -0.35f, -0.7f, 0, -1, 0, 1, 0.35f, 0.7f } } },                                      // eihwaz
		{ -0.3f, { { 0.4f, 0.9f, 0.1f, 0.6f, -0.3f, 1, -0.3f, -1, 0.1f, -0.6f, 0.4f, -0.9f } } },       // perthro
		{ 0.0f, { { 0, -1, 0, 1 }, { -0.4f, 0.8f, 0, 0.2f, 0.4f, 0.8f } } },                            // algiz
		{ NO_STAVE, { { 0.3f, 1, -0.3f, 0.35f, 0.3f, -0.35f, -0.3f, -1 } } },                           // sowilo
		{ 0.0f, { { 0, -1, 0, 1 }, { -0.4f, 0.5f, 0, 1, 0.4f, 0.5f } } },                               // tiwaz
		{ -0.3f, { { -0.3f, -1, -0.3f, 1, 0.3f, 0.5f, -0.3f, 0, 0.3f, -0.5f, -0.3f, -1 } } },           // berkanan
		{ -0.35f, { { -0.35f, -1, -0.35f, 1, 0, 0.4f, 0.35f, 1, 0.35f, -1 } } },                        // ehwaz
		{ -0.35f, { { -0.35f, -1, -0.35f, 1, 0.35f, 0.2f }, { 0.35f, -1, 0.35f, 1, -0.35f, 0.2f } } },  // mannaz
		{ -0.2f, { { -0.2f, -1, -0.2f, 1, 0.3f, 0.5f } } },                                             // laguz
		{ NO_STAVE, { { 0, 0.6f, 0.4f, 0, 0, -0.6f, -0.4f, 0, 0, 0.6f } } },                            // ingwaz
		{ NO_STAVE, { { -0.45f, 1, -0.45f, -1, 0.45f, 1, 0.45f, -1, -0.45f, 1 } } },                    // dagaz
		{ NO_STAVE, { { -0.4f, -0.8f, 0.4f, 0.4f, 0, 1, -0.4f, 0.4f, 0.4f, -0.8f } } },                 // othala
	};
	return runes;
}

const Rune& RandomRune( Rng& rng )
{
	return Runes()[ rng.Int( (int)Runes().size() ) ];
}

// Shifted by dx; skipStave leaves out a stroke that is the bare stave.
void DrawRune( Builder& b, const Rune& rune, float dx, bool skipStave )
{
	for( const std::vector< float >& stroke : rune.strokes )
	{
		if( skipStave && stroke.size() == 4 && stroke[ 0 ] == rune.staveX && stroke[ 2 ] == rune.staveX )
			continue;
		b.Polyline( stroke, dx );
	}
}

void Futhark( Builder& b, Rng& rng, float c )
{
	b.Place( 0.85f );
	DrawRune( b, RandomRune( rng ), 0.0f, false );
	b.Place();
	if( c > 0.4f )
		b.Ring( 0, 0, 1.0f );
	if( c > 0.75f )
	{
		b.Dot( -0.7f, 0, 0.05f );
		b.Dot( 0.7f, 0, 0.05f );
	}
}

// Several runes sharing one stave, some mirrored.
void Bindrune( Builder& b, Rng& rng, float c )
{
	std::vector< const Rune* > staved;
	for( const Rune& rune : Runes() )
		if( rune.staveX != NO_STAVE )
			staved.push_back( &rune );
	b.Place( 0.9f );
	b.Line( 0, -1, 0, 1 );
	int count = 2 + (int)( c * 2.99f );
	for( int i = 0; i < count; ++i )
	{
		const Rune& rune = *staved[ rng.Int( (int)staved.size() ) ];
		b.Place( 0.9f, 0.0f, 0.0f, 0.0f, rng.Chance( 0.5f ) );
		DrawRune( b, rune, -rune.staveX, true );
	}
}

// A band of small runes between two rings, one large rune in the middle.
void RuneCircle( Builder& b, Rng& rng, float c )
{
	b.Place();
	b.Ring( 0, 0, 0.97f );
	if( c > 0.3f )
		b.Ring( 0, 0, 0.68f );
	int count   = 6 + (int)( c * 8.99f );
	float start = rng.Range( 0.0f, TAU );
	for( int i = 0; i < count; ++i )
	{
		float a = start + TAU * (float)i / (float)count;
		b.Place( 0.1f, a - PI / 2.0f, 0.83f * std::cos( a ), 0.83f * std::sin( a ) );
		DrawRune( b, RandomRune( rng ), 0.0f, false );
		if( c > 0.6f )
		{
			float m = a + PI / (float)count;
			b.Place();
			b.Dot( 0.83f * std::cos( m ), 0.83f * std::sin( m ), 0.018f );
		}
	}
	b.Place( 0.42f );
	DrawRune( b, RandomRune( rng ), 0.0f, false );
}

// Arms from the centre, each ending in an ornament, in the manner of Icelandic magical staves.
void Stave( Builder& b, Rng& rng, float c )
{
	int arms      = 3 + (int)( c * 5.99f );
	float turn    = rng.Chance( 0.6f ) ? PI / 2.0f : rng.Range( 0.0f, TAU );
	int period    = arms % 2 == 0 && rng.Chance( 0.5f ) ? 2 : 1;// alternate two ornaments
	int ornament[ 2 ] = { rng.Int( 8 ), rng.Int( 8 ) };
	bool bar[ 2 ]     = { c > 0.5f && rng.Chance( 0.5f ), c > 0.5f && rng.Chance( 0.5f ) };
	float inner       = rng.Chance( 0.5f ) ? 0.14f : 0.0f;
	const float L     = 0.75f;

	b.Place();
	if( inner > 0.0f )
		b.Ring( 0, 0, inner );
	else if( rng.Chance( 0.5f ) )
		b.Dot( 0, 0, 0.05f );
	for( int i = 0; i < arms; ++i )
	{
		int p = i % period;
		// Each arm along +x of its own frame.
		b.Place( 1.0f, turn + TAU * (float)i / (float)arms );
		b.Line( inner, 0, L, 0 );
		if( bar[ p ] )
			b.Line( 0.45f, -0.09f, 0.45f, 0.09f );
		switch( ornament[ p ] )
		{
		case 0:// arrow
			b.Line( L, 0, L - 0.15f, 0.13f );
			b.Line( L, 0, L - 0.15f, -0.13f );
			break;
		case 1:// fork
			b.Line( L, 0, L + 0.15f, 0.14f );
			b.Line( L, 0, L + 0.15f, -0.14f );
			break;
		case 2:// circle
			b.Ring( L + 0.1f, 0, 0.1f );
			break;
		case 3:// one or two crossbars
			b.Line( L, -0.15f, L, 0.15f );
			if( c > 0.3f )
				b.Line( L - 0.12f, -0.1f, L - 0.12f, 0.1f );
			break;
		case 4:// trident
			b.Line( L, -0.13f, L, 0.13f );
			b.Line( L, 0.13f, L + 0.15f, 0.13f );
			b.Line( L, 0, L + 0.17f, 0 );
			b.Line( L, -0.13f, L + 0.15f, -0.13f );
			break;
		case 5:// hook
			b.Arc( L, 0.1f, 0.1f, -PI / 2.0f, PI / 2.0f );
			break;
		case 6:// dot
			b.Dot( L + 0.06f, 0, 0.06f );
			break;
		default:// branches halfway, crossbar at the end
			b.Line( L * 0.55f, 0, L * 0.55f + 0.15f, 0.15f );
			b.Line( L * 0.55f, 0, L * 0.55f + 0.15f, -0.15f );
			b.Line( L, -0.12f, L, 0.12f );
			break;
		}
	}
}

// A stem with groups of notches.
void Ogham( Builder& b, Rng& rng, float c )
{
	b.Place();
	b.Line( 0, -0.95f, 0, 0.95f );
	int groups       = 3 + (int)( c * 4.99f );
	const float step = 0.075f;
	float y          = -0.85f;
	for( int g = 0; g < groups; ++g )
	{
		int kind  = rng.Int( 4 );
		int count = 1 + rng.Int( 5 );
		if( y + count * step > 0.9f )
			break;
		for( int j = 0; j < count; ++j )
		{
			float yy = y + ( (float)j + 0.5f ) * step;
			switch( kind )
			{
			case 0: b.Line( 0, yy, -0.32f, yy ); break;
			case 1: b.Line( 0, yy, 0.32f, yy ); break;
			case 2: b.Line( -0.32f, yy - 0.07f, 0.32f, yy + 0.07f ); break;
			default: b.Line( -0.1f, yy, 0.1f, yy ); break;
			}
		}
		y += count * step + 0.12f;
	}
}

// Printed circuit traces on a grid, with 45 degree bends, from a pad to a via.
void Circuit( Builder& b, Rng& rng, float c, float bx, float by )
{
	b.Place();
	float cell = 2.0f / ( 5.0f + c * 7.0f );
	int cols   = std::max( 2, (int)( 2.0f * bx / cell ) );
	int rows   = std::max( 2, (int)( 2.0f * by / cell ) );
	float sx   = 2.0f * bx / (float)cols;
	float sy   = 2.0f * by / (float)rows;
	float pad  = 0.3f * std::min( sx, sy );
	auto X     = [ & ]( int i ) { return -bx + ( (float)i + 0.5f ) * sx; };
	auto Y     = [ & ]( int j ) { return -by + ( (float)j + 0.5f ) * sy; };
	std::vector< char > used( (size_t)( cols * rows ), 0 );
	const int DX[ 8 ] = { 1, 1, 0, -1, -1, -1, 0, 1 };
	const int DY[ 8 ] = { 0, 1, 1, 1, 0, -1, -1, -1 };

	int traces = 3 + (int)( c * 12.0f );
	for( int t = 0; t < traces; ++t )
	{
		int x = -1, y = -1;
		for( int attempt = 0; attempt < 20; ++attempt )
		{
			int i = rng.Int( cols ), j = rng.Int( rows );
			if( !used[ j * cols + i ] )
			{
				x = i;
				y = j;
				break;
			}
		}
		if( x < 0 )
			break;
		used[ y * cols + x ] = 1;
		std::vector< int > corners = { x, y };
		int dir  = rng.Int( 4 ) * 2;// starts straight
		int legs = 1 + rng.Int( 3 );
		for( int leg = 0; leg < legs; ++leg )
		{
			int length = 1 + rng.Int( 4 );
			bool moved = false;
			for( int s = 0; s < length; ++s )
			{
				int nx = x + DX[ dir ], ny = y + DY[ dir ];
				if( nx < 0 || ny < 0 || nx >= cols || ny >= rows || used[ ny * cols + nx ] )
					break;
				x = nx;
				y = ny;
				used[ y * cols + x ] = 1;
				moved = true;
			}
			if( moved )
				corners.insert( corners.end(), { x, y } );
			dir = ( dir + ( rng.Chance( 0.5f ) ? 1 : 7 ) ) % 8;
		}
		if( corners.size() < 4 )
		{
			b.Dot( X( x ), Y( y ), pad * 0.6f );
			continue;
		}
		b.Dot( X( corners[ 0 ] ), Y( corners[ 1 ] ), pad * 0.55f );
		for( size_t k = 2; k + 1 < corners.size(); k += 2 )
			b.Line( X( corners[ k - 2 ] ), Y( corners[ k - 1 ] ), X( corners[ k ] ), Y( corners[ k + 1 ] ) );
		b.Ring( X( x ), Y( y ), pad );
	}
}

// An integrated circuit: its body, and traces fanning out from its pins.
void Chip( Builder& b, Rng& rng, float c, float bx, float by )
{
	b.Place();
	float hw = 0.38f * bx, hh = 0.38f * by;
	b.Polyline( { -hw, -hh, hw, -hh, hw, hh, -hw, hh }, 0.0f, true );
	b.Arc( 0, hh, 0.07f, PI, TAU );
	b.Dot( -hw + 0.09f, hh - 0.09f, 0.03f );

	int pins = 2 + (int)( c * 3.99f );
	for( int side = 0; side < 4; ++side )
	{
		// Outward normal n and tangent t of this side.
		float nx = side == 0 ? 1.0f : side == 2 ? -1.0f : 0.0f;
		float ny = side == 1 ? 1.0f : side == 3 ? -1.0f : 0.0f;
		float tx = -ny, ty = nx;
		float halfN  = nx != 0.0f ? hw : hh;
		float halfT  = nx != 0.0f ? hh : hw;
		float boxN   = nx != 0.0f ? bx : by;
		float boxT   = nx != 0.0f ? by : bx;
		float run    = rng.Range( 0.02f, 0.1f );
		float fan    = rng.Range( 0.3f, 1.0f );
		auto Line = [ & ]( float u0, float v0, float u1, float v1 ) {
			b.Line( nx * u0 + tx * v0, ny * u0 + ty * v0, nx * u1 + tx * v1, ny * u1 + ty * v1 );
		};
		for( int p = 0; p < pins; ++p )
		{
			float v  = halfT * ( -0.8f + 1.6f * ( (float)p + 0.5f ) / (float)pins );
			float u1 = halfN + 0.07f;
			Line( halfN, v, u1, v );
			if( rng.Chance( 0.2f ) )
			{
				b.Dot( nx * u1 + tx * v, ny * u1 + ty * v, 0.025f );
				continue;
			}
			// Pins further from the middle bend further out: the traces never cross.
			float dv   = v / halfT * fan * ( boxT - halfT ) * 0.8f;
			float u2   = u1 + run;
			float room = std::max( 0.0f, boxN * 0.9f - u2 );
			if( std::abs( dv ) > room )
				dv = dv > 0.0f ? room : -room;
			float u3   = u2 + std::abs( dv );
			float uEnd = std::max( u3 + 0.05f, boxN * rng.Range( 0.75f, 0.95f ) );
			Line( u1, v, u2, v );
			Line( u2, v, u3, v + dv );
			Line( u3, v + dv, uEnd, v + dv );
			b.Ring( nx * uEnd + tx * ( v + dv ), ny * uEnd + ty * ( v + dv ), 0.035f );
		}
	}
}

// A star polygon in a circle, with occult-diagram ornaments.
void Sigil( Builder& b, Rng& rng, float c )
{
	b.Place();
	const float R = 0.92f;
	b.Ring( 0, 0, R );
	int n = 5 + rng.Int( 5 );
	int k = 1 + rng.Int( std::max( 1, ( n - 1 ) / 2 ) );
	auto Point = [ & ]( int i ) {
		float a = PI / 2.0f + TAU * (float)i / (float)n;
		return Vec{ R * std::cos( a ), R * std::sin( a ) };
	};
	for( int i = 0; i < n; ++i )
	{
		Vec p = Point( i ), q = Point( i + k );
		b.Line( p.x, p.y, q.x, q.y );
	}

	int order[ 6 ] = { 0, 1, 2, 3, 4, 5 };
	for( int i = 5; i > 0; --i )
		std::swap( order[ i ], order[ rng.Int( i + 1 ) ] );
	int extras = 1 + (int)( c * 3.99f );
	for( int e = 0; e < extras; ++e )
	{
		switch( order[ e ] )
		{
		case 0:// circle inside the star
			b.Ring( 0, 0, R * std::cos( PI * (float)k / (float)n ) );
			break;
		case 1:// small circles on the vertices
			for( int i = 0; i < n; ++i )
				b.Ring( Point( i ).x, Point( i ).y, 0.07f );
			break;
		case 2:// spokes
			for( int i = 0; i < n; ++i )
				b.Line( 0, 0, Point( i ).x, Point( i ).y );
			break;
		case 3:// crescent
			b.Arc( 0, 0, 0.28f, PI * 0.3f, PI * 1.7f );
			b.Arc( 0.1f, 0, 0.22f, PI * 0.35f, PI * 1.65f );
			break;
		case 4:// triangle
			b.Polyline( { 0, 0.35f, 0.3f, -0.18f, -0.3f, -0.18f }, 0.0f, true );
			break;
		default:// second outer ring
			b.Ring( 0, 0, R + 0.06f );
			break;
		}
	}
	if( rng.Chance( 0.5f ) )
		b.Dot( 0, 0, 0.05f );
}

// Stars joined by their minimum spanning tree.
void Constellation( Builder& b, Rng& rng, float c, float bx, float by )
{
	b.Place();
	size_t count = (size_t)( 5 + (int)( c * 10.0f ) );
	std::vector< Vec > stars;
	for( int attempt = 0; attempt < 400 && stars.size() < count; ++attempt )
	{
		Vec p     = { rng.Range( -bx, bx ) * 0.9f, rng.Range( -by, by ) * 0.9f };
		bool free = true;
		for( const Vec& s : stars )
			free = free && std::hypot( s.x - p.x, s.y - p.y ) > 0.22f;
		if( free )
			stars.push_back( p );
	}
	size_t n = stars.size();
	std::vector< bool > linked( n, false );
	std::vector< float > nearest( n, 1e30f );
	std::vector< size_t > parent( n, 0 );
	b.Dot( stars[ 0 ].x, stars[ 0 ].y, rng.Range( 0.02f, 0.055f ) );
	linked[ 0 ] = true;
	for( size_t i = 1; i < n; ++i )
		nearest[ i ] = std::hypot( stars[ i ].x - stars[ 0 ].x, stars[ i ].y - stars[ 0 ].y );
	for( size_t added = 1; added < n; ++added )
	{
		size_t j = 0;
		float best = 1e30f;
		for( size_t i = 0; i < n; ++i )
			if( !linked[ i ] && nearest[ i ] < best )
			{
				best = nearest[ i ];
				j    = i;
			}
		linked[ j ] = true;
		b.Line( stars[ parent[ j ] ].x, stars[ parent[ j ] ].y, stars[ j ].x, stars[ j ].y );
		b.Dot( stars[ j ].x, stars[ j ].y, rng.Range( 0.02f, 0.055f ) );
		for( size_t i = 0; i < n; ++i )
		{
			float d = std::hypot( stars[ i ].x - stars[ j ].x, stars[ i ].y - stars[ j ].y );
			if( !linked[ i ] && d < nearest[ i ] )
			{
				nearest[ i ] = d;
				parent[ i ]  = j;
			}
		}
	}
}

// The passages of a random maze, drawn in the order it is dug.
void Maze( Builder& b, Rng& rng, float c, float bx, float by )
{
	b.Place();
	float density = 3.0f + c * 5.0f;
	int cols      = std::max( 2, (int)std::round( density * bx ) );
	int rows      = std::max( 2, (int)std::round( density * by ) );
	while( cols * rows > 60 )
		( cols >= rows ? cols : rows )--;
	float sx = 2.0f * bx / (float)cols;
	float sy = 2.0f * by / (float)rows;
	auto X   = [ & ]( int cell ) { return -bx + ( (float)( cell % cols ) + 0.5f ) * sx; };
	auto Y   = [ & ]( int cell ) { return -by + ( (float)( cell / cols ) + 0.5f ) * sy; };
	const int DX[ 4 ] = { 1, 0, -1, 0 };
	const int DY[ 4 ] = { 0, 1, 0, -1 };

	std::vector< char > visited( (size_t)( cols * rows ), 0 );
	int start = rng.Int( cols * rows );
	std::vector< int > stack = { start };
	visited[ start ] = 1;
	b.Ring( X( start ), Y( start ), 0.25f * std::min( sx, sy ) );
	int lastDir   = -1;
	bool advanced = false;
	while( !stack.empty() )
	{
		int cell = stack.back();
		int x = cell % cols, y = cell / cols;
		int options[ 4 ], count = 0;
		for( int d = 0; d < 4; ++d )
		{
			int nx = x + DX[ d ], ny = y + DY[ d ];
			if( nx >= 0 && ny >= 0 && nx < cols && ny < rows && !visited[ ny * cols + nx ] )
				options[ count++ ] = d;
		}
		if( count == 0 )
		{
			if( advanced )// dead end
				b.Dot( X( cell ), Y( cell ), 0.12f * std::min( sx, sy ) );
			stack.pop_back();
			advanced = false;
			lastDir  = -1;
			continue;
		}
		int d    = options[ rng.Int( count ) ];
		int next = ( y + DY[ d ] ) * cols + x + DX[ d ];
		visited[ next ] = 1;
		if( d == lastDir )// straight on: lengthen the last line
		{
			b.prims.back().bx = X( next );
			b.prims.back().by = Y( next );
		}
		else
			b.Line( X( cell ), Y( cell ), X( next ), Y( next ) );
		lastDir  = d;
		advanced = true;
		stack.push_back( next );
	}
}

// Concentric rings of a head-up display: arcs, ticks, dashes, and a crosshair.
void Hud( Builder& b, Rng& rng, float c )
{
	b.Place();
	int rings = 2 + (int)( c * 3.0f );
	float r   = 0.95f;
	for( int i = 0; i < rings && r > 0.2f; ++i )
	{
		switch( rng.Int( 5 ) )
		{
		case 0:
			b.Ring( 0, 0, r );
			break;
		case 1:
		{
			int m   = 1 + rng.Int( 3 );
			float a = rng.Range( 0.0f, TAU );
			for( int j = 0; j < m; ++j, a += TAU / (float)m )
				b.Arc( 0, 0, r, a, a + rng.Range( 0.4f, TAU / (float)m - 0.3f ) );
			break;
		}
		case 2:
		{
			int m = 12 + 4 * rng.Int( 4 );
			for( int j = 0; j < m; ++j )
			{
				float a     = TAU * (float)j / (float)m;
				float inner = j % 4 == 0 ? r - 0.1f : r - 0.05f;
				b.Line( inner * std::cos( a ), inner * std::sin( a ), r * std::cos( a ), r * std::sin( a ) );
			}
			break;
		}
		case 3:
		{
			int m      = 8 + rng.Int( 9 );
			float step = TAU / (float)m;
			for( int j = 0; j < m; ++j )
				b.Arc( 0, 0, r, step * (float)j, step * ( (float)j + 0.5f ) );
			break;
		}
		default:
		{
			b.Ring( 0, 0, r );
			int m = 3 + rng.Int( 2 );
			for( int j = 0; j < m; ++j )
			{
				float a = rng.Range( 0.0f, TAU );
				b.Dot( r * std::cos( a ), r * std::sin( a ), 0.03f );
			}
			break;
		}
		}
		r -= rng.Range( 0.14f, 0.22f );
	}
	b.Line( -0.12f, 0, -0.04f, 0 );
	b.Line( 0.04f, 0, 0.12f, 0 );
	b.Line( 0, -0.12f, 0, -0.04f );
	b.Line( 0, 0.04f, 0, 0.12f );
	b.Dot( 0, 0, 0.025f );
}

}//End anonymous namespace

std::vector< Prim > Generate( Style style, uint32_t seed, float complexity, float boxX, float boxY )
{
	Rng rng( seed );
	Builder b;
	if( style >= STYLE_COUNT )
		style = (Style)rng.Int( STYLE_COUNT );
	float c = std::min( std::max( complexity, 0.0f ), 1.0f );
	switch( style )
	{
	case FUTHARK: Futhark( b, rng, c ); break;
	case BINDRUNE: Bindrune( b, rng, c ); break;
	case RUNE_CIRCLE: RuneCircle( b, rng, c ); break;
	case STAVE: Stave( b, rng, c ); break;
	case OGHAM: Ogham( b, rng, c ); break;
	case CIRCUIT: Circuit( b, rng, c, boxX, boxY ); break;
	case CHIP: Chip( b, rng, c, boxX, boxY ); break;
	case SIGIL: Sigil( b, rng, c ); break;
	case CONSTELLATION: Constellation( b, rng, c, boxX, boxY ); break;
	case MAZE: Maze( b, rng, c, boxX, boxY ); break;
	default: Hud( b, rng, c ); break;
	}

	// Along the glyph in drawing order, by length; a dot counts as a short stroke.
	std::vector< Prim >& prims = b.prims;
	std::vector< float > lengths;
	float total = 0.0f;
	for( const Prim& p : prims )
	{
		float length = p.type == LINE ? std::hypot( p.bx - p.ax, p.by - p.ay )
		             : p.type == ARC  ? p.radius * std::abs( p.by - p.bx )
		                              : 0.1f;
		lengths.push_back( length );
		total += length;
	}
	float along = 0.0f;
	for( size_t i = 0; i < prims.size(); ++i )
	{
		prims[ i ].alongStart = along / total;
		along += lengths[ i ];
		prims[ i ].alongEnd = along / total;
	}
	return prims;
}

}//End namespace glyphs
