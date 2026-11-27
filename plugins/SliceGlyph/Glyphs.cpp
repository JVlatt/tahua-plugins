#include "Glyphs.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>

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

// Collects strokes through a placement: mirror, scale, rotation, then offset; then the base
// placement, which puts a whole symbol somewhere: scale, then offset.
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
	void SetBase( float x = 0.0f, float y = 0.0f, float scale_ = 1.0f )
	{
		baseOffset = { x, y };
		baseScale  = scale_;
	}
	void Path( const std::vector< Vec >& points )
	{
		for( size_t i = 0; i + 1 < points.size(); ++i )
			Line( points[ i ].x, points[ i ].y, points[ i + 1 ].x, points[ i + 1 ].y );
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
		prims.push_back( { c.x, c.y, MapAngle( from ), MapAngle( to ), r * scale * baseScale, ARC } );
	}
	void Ring( float x, float y, float r )
	{
		Arc( x, y, r, 0.0f, TAU );
	}
	void Dot( float x, float y, float r )
	{
		Vec c = Map( x, y );
		prims.push_back( { c.x, c.y, 0.0f, 0.0f, r * scale * baseScale, DOT } );
	}

private:
	Vec Map( float x, float y ) const
	{
		if( mirror )
			x = -x;
		x *= scale;
		y *= scale;
		float c = std::cos( angle ), s = std::sin( angle );
		return { ( c * x - s * y + offset.x ) * baseScale + baseOffset.x, ( s * x + c * y + offset.y ) * baseScale + baseOffset.y };
	}
	float MapAngle( float a ) const
	{
		return ( mirror ? PI - a : a ) + angle;
	}

	float scale  = 1.0f;
	float angle  = 0.0f;
	Vec offset   = { 0.0f, 0.0f };
	bool mirror  = false;
	float baseScale = 1.0f;
	Vec baseOffset  = { 0.0f, 0.0f };
};

// ---- Shape tools ----

struct Circle
{
	Vec centre;
	float radius;
};

Vec FieldPoint( const Field& f, int i, int j )
{
	return { f.x0 + (float)i * f.step, f.y0 + (float)j * f.step };
}

// Largest circle inside the shape that keeps clear of the taken ones; radius 0 if none.
Circle Largest( const Field& f, const std::vector< Circle >& taken )
{
	Circle best = { { 0.0f, 0.0f }, 0.0f };
	for( int j = 0; j < f.rows; ++j )
		for( int i = 0; i < f.cols; ++i )
		{
			float r = f.values[ (size_t)j * f.cols + i ];
			if( r <= best.radius )
				continue;
			Vec p = FieldPoint( f, i, j );
			for( const Circle& c : taken )
				r = std::min( r, std::hypot( p.x - c.centre.x, p.y - c.centre.y ) - c.radius );
			if( r > best.radius )
				best = { p, r };
		}
	return best;
}

// Typical half thickness of the shape: three times its mean depth (a disc's radius), at most its deepest point.
float Thickness( const Field& f )
{
	double sum = 0.0;
	int inside = 0;
	float deepest = 0.0f;
	for( float v : f.values )
		if( v > 0.0f )
		{
			sum += v;
			++inside;
			deepest = std::max( deepest, v );
		}
	return inside == 0 ? 0.0f : std::min( deepest, 3.0f * (float)( sum / inside ) );
}

float Area( const Field& f )
{
	int inside = 0;
	for( float v : f.values )
		inside += v > 0.0f;
	return (float)inside * f.step * f.step;
}

// Unit vector pointing out of the shape at p.
Vec Outward( const Field& f, Vec p )
{
	float e  = f.step * 0.5f;
	float gx = f.At( p.x + e, p.y ) - f.At( p.x - e, p.y );
	float gy = f.At( p.x, p.y + e ) - f.At( p.x, p.y - e );
	float n  = std::max( std::hypot( gx, gy ), 1e-9f );
	return { -gx / n, -gy / n };
}

// Drops the points within epsilon of the line through their neighbours (Ramer-Douglas-Peucker).
void Simplify( const std::vector< Vec >& points, size_t first, size_t last, float epsilon, std::vector< Vec >& out )
{
	float worst = 0.0f;
	size_t index = first;
	Vec a = points[ first ], b = points[ last ];
	float abx = b.x - a.x, aby = b.y - a.y;
	float ab2 = std::max( abx * abx + aby * aby, 1e-12f );
	for( size_t i = first + 1; i < last; ++i )
	{
		float t = std::min( std::max( ( ( points[ i ].x - a.x ) * abx + ( points[ i ].y - a.y ) * aby ) / ab2, 0.0f ), 1.0f );
		float d = std::hypot( points[ i ].x - a.x - abx * t, points[ i ].y - a.y - aby * t );
		if( d > worst )
		{
			worst = d;
			index = i;
		}
	}
	if( worst > epsilon )
	{
		Simplify( points, first, index, epsilon, out );
		Simplify( points, index, last, epsilon, out );
	}
	else
		out.push_back( b );
}

// Lines where the distance to the outline is level, as polylines (a closed one ends on its start).
std::vector< std::vector< Vec > > Contours( const Field& f, float level )
{
	auto V = [ & ]( int i, int j ) { return f.values[ (size_t)j * f.cols + i ] - level; };
	// Edge ids: 2 per grid point, its edge to the right then its edge upwards.
	auto Crossing = [ & ]( int id ) -> Vec {
		int cell = id / 2, i = cell % f.cols, j = cell / f.cols;
		bool up  = id % 2 == 1;
		float a  = V( i, j ), b = up ? V( i, j + 1 ) : V( i + 1, j );
		float t  = a / ( a - b );
		return up ? Vec{ f.x0 + (float)i * f.step, f.y0 + ( (float)j + t ) * f.step } : Vec{ f.x0 + ( (float)i + t ) * f.step, f.y0 + (float)j * f.step };
	};
	std::vector< std::pair< int, int > > segments;
	for( int j = 0; j + 1 < f.rows; ++j )
		for( int i = 0; i + 1 < f.cols; ++i )
		{
			float v0 = V( i, j ), v1 = V( i + 1, j ), v2 = V( i + 1, j + 1 ), v3 = V( i, j + 1 );
			int code = ( v0 > 0.0f ) | ( v1 > 0.0f ) << 1 | ( v2 > 0.0f ) << 2 | ( v3 > 0.0f ) << 3;
			int e0 = ( j * f.cols + i ) * 2;           // bottom
			int e1 = ( j * f.cols + i + 1 ) * 2 + 1;   // right
			int e2 = ( ( j + 1 ) * f.cols + i ) * 2;   // top
			int e3 = ( j * f.cols + i ) * 2 + 1;       // left
			bool centre = v0 + v1 + v2 + v3 > 0.0f;
			switch( code )
			{
			case 1: case 14: segments.push_back( { e3, e0 } ); break;
			case 2: case 13: segments.push_back( { e0, e1 } ); break;
			case 3: case 12: segments.push_back( { e3, e1 } ); break;
			case 4: case 11: segments.push_back( { e1, e2 } ); break;
			case 6: case 9: segments.push_back( { e0, e2 } ); break;
			case 7: case 8: segments.push_back( { e2, e3 } ); break;
			case 5:
				if( centre )
					segments.insert( segments.end(), { { e0, e1 }, { e2, e3 } } );
				else
					segments.insert( segments.end(), { { e3, e0 }, { e1, e2 } } );
				break;
			case 10:
				if( centre )
					segments.insert( segments.end(), { { e3, e0 }, { e1, e2 } } );
				else
					segments.insert( segments.end(), { { e0, e1 }, { e2, e3 } } );
				break;
			default: break;
			}
		}

	// Chain the segments through their shared edges.
	std::unordered_map< int, std::vector< int > > byEdge;
	for( int s = 0; s < (int)segments.size(); ++s )
	{
		byEdge[ segments[ s ].first ].push_back( s );
		byEdge[ segments[ s ].second ].push_back( s );
	}
	std::vector< char > used( segments.size(), 0 );
	std::vector< std::vector< Vec > > lines;
	for( size_t s0 = 0; s0 < segments.size(); ++s0 )
	{
		if( used[ s0 ] )
			continue;
		used[ s0 ] = 1;
		std::deque< int > ids = { segments[ s0 ].first, segments[ s0 ].second };
		for( int side = 0; side < 2; ++side )
		{
			bool front = side == 1;
			while( true )
			{
				int end  = front ? ids.front() : ids.back();
				int next = -1;
				for( int s : byEdge[ end ] )
					if( !used[ s ] )
					{
						next = s;
						break;
					}
				if( next < 0 )
					break;
				used[ next ] = 1;
				int other = segments[ next ].first == end ? segments[ next ].second : segments[ next ].first;
				if( front )
					ids.push_front( other );
				else
					ids.push_back( other );
			}
		}
		std::vector< Vec > points;
		for( int id : ids )
			points.push_back( Crossing( id ) );
		std::vector< Vec > line = { points.front() };
		Simplify( points, 0, points.size() - 1, f.step * 0.25f, line );
		if( line.size() >= 2 )
			lines.push_back( std::move( line ) );
	}
	return lines;
}

float Length( const std::vector< Vec >& line )
{
	float length = 0.0f;
	for( size_t i = 0; i + 1 < line.size(); ++i )
		length += std::hypot( line[ i + 1 ].x - line[ i ].x, line[ i + 1 ].y - line[ i ].y );
	return length;
}

// The point at distance s along the line, and its direction there.
Vec PointAlong( const std::vector< Vec >& line, float s, Vec* direction = nullptr )
{
	for( size_t i = 0; i + 1 < line.size(); ++i )
	{
		float dx = line[ i + 1 ].x - line[ i ].x, dy = line[ i + 1 ].y - line[ i ].y;
		float l  = std::hypot( dx, dy );
		if( s <= l || i + 2 == line.size() )
		{
			float t = l > 0.0f ? std::min( s / l, 1.0f ) : 0.0f;
			if( direction )
				*direction = l > 0.0f ? Vec{ dx / l, dy / l } : Vec{ 1.0f, 0.0f };
			return { line[ i ].x + dx * t, line[ i ].y + dy * t };
		}
		s -= l;
	}
	if( direction )
		*direction = { 1.0f, 0.0f };
	return line.front();
}

// The part of the line from s0 to s1 along it.
std::vector< Vec > Piece( const std::vector< Vec >& line, float s0, float s1 )
{
	std::vector< Vec > piece = { PointAlong( line, s0 ) };
	float along = 0.0f;
	for( size_t i = 0; i + 1 < line.size(); ++i )
	{
		along += std::hypot( line[ i + 1 ].x - line[ i ].x, line[ i + 1 ].y - line[ i ].y );
		if( along > s0 && along < s1 )
			piece.push_back( line[ i + 1 ] );
	}
	piece.push_back( PointAlong( line, s1 ) );
	return piece;
}

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
// With a shape, only the cells well inside it are used, and the grid follows its thickness.
void Circuit( Builder& b, Rng& rng, float c, float bx, float by, const Field* f )
{
	b.Place();
	float cell = 2.0f / ( 5.0f + c * 7.0f ) * ( f ? Thickness( *f ) : 1.0f );
	if( cell <= 0.0f )
		return;
	cell       = std::max( cell, std::sqrt( 4.0f * bx * by / 6000.0f ) );
	int cols   = std::max( 2, (int)( 2.0f * bx / cell ) );
	int rows   = std::max( 2, (int)( 2.0f * by / cell ) );
	float sx   = 2.0f * bx / (float)cols;
	float sy   = 2.0f * by / (float)rows;
	float pad  = 0.3f * std::min( sx, sy );
	auto X     = [ & ]( int i ) { return -bx + ( (float)i + 0.5f ) * sx; };
	auto Y     = [ & ]( int j ) { return -by + ( (float)j + 0.5f ) * sy; };
	std::vector< char > used( (size_t)( cols * rows ), 0 );
	int freeCells = cols * rows;
	if( f )
		for( int j = 0; j < rows; ++j )
			for( int i = 0; i < cols; ++i )
				if( f->At( X( i ), Y( j ) ) < 0.45f * std::min( sx, sy ) )
				{
					used[ j * cols + i ] = 1;
					--freeCells;
				}
	const int DX[ 8 ] = { 1, 1, 0, -1, -1, -1, 0, 1 };
	const int DY[ 8 ] = { 0, 1, 1, 1, 0, -1, -1, -1 };

	int traces = f ? std::max( 1, (int)( (float)freeCells * ( 0.06f + 0.12f * c ) ) ) : 3 + (int)( c * 12.0f );
	for( int t = 0; t < traces; ++t )
	{
		// The n-th free cell, n at random.
		if( freeCells <= 0 )
			break;
		int n = rng.Int( freeCells ), x = -1, y = -1;
		for( int k = 0; k < cols * rows && x < 0; ++k )
			if( !used[ k ] && n-- == 0 )
			{
				x = k % cols;
				y = k / cols;
			}
		used[ y * cols + x ] = 1;
		--freeCells;
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
				--freeCells;
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

// An integrated circuit: its body, and traces fanning out from its pins. With a shape, the chip is
// drawn at centre, scaled by size, and its traces stop where the shape ends.
void Chip( Builder& b, Rng& rng, float c, float bx, float by, const Field* f = nullptr, Vec centre = { 0.0f, 0.0f }, float size = 1.0f )
{
	b.Place();
	b.SetBase( centre.x, centre.y, size );
	// Depth of a local point inside the shape, in local units.
	auto Depth = [ & ]( float x, float y ) { return f ? f->At( centre.x + x * size, centre.y + y * size ) / size : 1.0f; };
	float margin = f ? std::max( f->step * 0.5f / size, 0.04f ) : 0.0f;
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
			auto Inside = [ & ]( float u, float w ) { return Depth( nx * u + tx * w, ny * u + ty * w ) > margin; };
			if( f )
			{
				if( !Inside( u2, v ) || !Inside( u3, v + dv ) )
				{
					b.Dot( nx * u1 + tx * v, ny * u1 + ty * v, 0.025f );
					continue;
				}
				// As far out as the shape goes, up to four times the room of the box.
				float stepU = std::max( f->step * 0.5f / size, 0.01f );
				uEnd        = u3;
				while( uEnd < 4.0f * boxN && Inside( uEnd + stepU, v + dv ) )
					uEnd += stepU;
				uEnd = u3 + ( uEnd - u3 ) * rng.Range( 0.6f, 1.0f );
			}
			Line( u1, v, u2, v );
			Line( u2, v, u3, v + dv );
			Line( u3, v + dv, uEnd, v + dv );
			b.Ring( nx * uEnd + tx * ( v + dv ), ny * uEnd + ty * ( v + dv ), 0.035f );
		}
	}
	b.SetBase();
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
// With a shape, stars only inside it, as many as its area holds.
void Constellation( Builder& b, Rng& rng, float c, float bx, float by, const Field* f )
{
	b.Place();
	float unit   = f ? Thickness( *f ) : 1.0f;// sizes and spacing follow the shape's thickness
	float fill   = f && unit > 0.0f ? std::min( std::max( Area( *f ) / ( PI * unit * unit ), 1.0f ), 25.0f ) : 1.0f;
	size_t count = (size_t)( ( 5.0f + c * 10.0f ) * fill );
	std::vector< Vec > stars;
	for( size_t attempt = 0; attempt < 80 * count && stars.size() < count; ++attempt )
	{
		Vec p     = { rng.Range( -bx, bx ) * 0.9f, rng.Range( -by, by ) * 0.9f };
		bool free = !f || f->At( p.x, p.y ) > 0.08f * unit;
		for( size_t s = 0; free && s < stars.size(); ++s )
			free = std::hypot( stars[ s ].x - p.x, stars[ s ].y - p.y ) > 0.22f * unit;
		if( free )
			stars.push_back( p );
	}
	size_t n = stars.size();
	if( n == 0 )
		return;
	auto Star = [ & ]( const Vec& p ) { b.Dot( p.x, p.y, rng.Range( 0.02f, 0.055f ) * unit ); };
	std::vector< bool > linked( n, false );
	std::vector< float > nearest( n, 1e30f );
	std::vector< size_t > parent( n, 0 );
	Star( stars[ 0 ] );
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
		Star( stars[ j ] );
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
// With a shape, only its cells, one maze per separate part of it.
void Maze( Builder& b, Rng& rng, float c, float bx, float by, const Field* f )
{
	b.Place();
	float density = ( 3.0f + c * 5.0f ) / ( f ? std::max( Thickness( *f ), 1e-3f ) : 1.0f );
	int cols      = std::max( 2, (int)std::round( density * bx ) );
	int rows      = std::max( 2, (int)std::round( density * by ) );
	int most      = f ? 1500 : 60;
	while( cols * rows > most )
		( cols >= rows ? cols : rows )--;
	float sx = 2.0f * bx / (float)cols;
	float sy = 2.0f * by / (float)rows;
	auto X   = [ & ]( int cell ) { return -bx + ( (float)( cell % cols ) + 0.5f ) * sx; };
	auto Y   = [ & ]( int cell ) { return -by + ( (float)( cell / cols ) + 0.5f ) * sy; };
	const int DX[ 4 ] = { 1, 0, -1, 0 };
	const int DY[ 4 ] = { 0, 1, 0, -1 };

	// Cells outside the shape count as visited from the start.
	std::vector< char > visited( (size_t)( cols * rows ), 0 );
	std::vector< int > starts;
	for( int k = 0; k < cols * rows; ++k )
	{
		if( f && f->At( X( k ), Y( k ) ) < 0.3f * std::min( sx, sy ) )
			visited[ k ] = 1;
		else
			starts.push_back( k );
	}
	if( starts.empty() )
		return;
	// From a random cell first, then from the first cell of each part not reached yet.
	std::swap( starts[ 0 ], starts[ rng.Int( (int)starts.size() ) ] );
	for( int start : starts )
	{
		if( visited[ start ] )
			continue;
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

// ---- Styles that follow a shape ----

// Symbols in the largest free circles of the shape, biggest first, until they get too small.
void Pack( Builder& b, Rng& rng, float c, const Field& f, void ( *symbol )( Builder&, Rng&, float ) )
{
	std::vector< Circle > taken;
	int count   = 1 + (int)( c * 15.0f );
	float first = 0.0f;
	for( int i = 0; i < count; ++i )
	{
		Circle circle = Largest( f, taken );
		if( i == 0 )
			first = circle.radius;
		if( circle.radius <= 0.0f || circle.radius < first * 0.2f || circle.radius < 1.5f * f.step )
			break;
		b.SetBase( circle.centre.x, circle.centre.y, circle.radius * f.size );
		symbol( b, rng, c );
		taken.push_back( circle );
	}
	b.SetBase();
}

void PackChips( Builder& b, Rng& rng, float c, const Field& f )
{
	std::vector< Circle > taken;
	int count   = 1 + (int)( c * 4.0f );
	float first = 0.0f;
	for( int i = 0; i < count; ++i )
	{
		Circle circle = Largest( f, taken );
		if( i == 0 )
			first = circle.radius;
		if( circle.radius <= 0.0f || circle.radius < first * 0.35f || circle.radius < 3.0f * f.step )
			break;
		// A body about half the circle; its traces run on to the edges of the shape.
		Chip( b, rng, c, 1.2f, 1.2f, &f, circle.centre, circle.radius * f.size );
		taken.push_back( circle );
	}
}

// Edge of the shape, then a band of runes along it, then a rune in what is left in the middle.
void RuneBand( Builder& b, Rng& rng, float c, const Field& f )
{
	Circle deepest = Largest( f, {} );
	if( deepest.radius <= 0.0f )
		return;
	float edge  = std::max( 0.05f * deepest.radius, f.step );
	float band  = 0.32f * deepest.radius * f.size;
	float rune  = band * 0.36f;// half height of a rune
	b.Place();
	for( const std::vector< Vec >& line : Contours( f, edge ) )
		b.Path( line );
	if( c > 0.3f )
		for( const std::vector< Vec >& line : Contours( f, edge + band ) )
			b.Path( line );
	// Spread wider when the whole band would hold too many runes.
	std::vector< std::vector< Vec > > middles = Contours( f, edge + band * 0.5f );
	float total = 0.0f;
	for( const std::vector< Vec >& line : middles )
		total += Length( line );
	float spacing = std::max( rune * 1.5f, total / ( 60.0f + c * 200.0f ) );
	for( const std::vector< Vec >& line : middles )
	{
		float length = Length( line );
		int count    = (int)( length / spacing );
		for( int k = 0; k < count; ++k )
		{
			Vec direction;
			Vec p   = PointAlong( line, ( (float)k + 0.5f ) * length / (float)count, &direction );
			Vec out = Outward( f, p );
			// Upright towards the outside of the shape.
			b.Place( rune, std::atan2( out.y, out.x ) - PI / 2.0f, p.x, p.y );
			DrawRune( b, RandomRune( rng ), 0.0f, false );
			if( c > 0.6f )
			{
				Vec m = PointAlong( line, ( (float)k + 1.0f ) * length / (float)count );
				b.Place();
				b.Dot( m.x, m.y, rune * 0.15f );
			}
		}
	}
	float inner = deepest.radius - edge - band;
	if( inner > 2.0f * f.step )
	{
		b.SetBase( deepest.centre.x, deepest.centre.y, inner * 0.85f );
		Bindrune( b, rng, c * 0.5f );
		b.SetBase();
	}
	b.Place();
}

// Points spread along the inset outline, joined as a star, with ornaments.
void SigilShape( Builder& b, Rng& rng, float c, const Field& f )
{
	Circle deepest = Largest( f, {} );
	if( deepest.radius <= 0.0f )
		return;
	float R     = deepest.radius;
	float level = std::max( 0.08f * R, f.step );
	b.Place();
	std::vector< std::vector< Vec > > lines = Contours( f, level );
	if( lines.empty() )
		return;
	float total = 0.0f;
	for( const std::vector< Vec >& line : lines )
	{
		b.Path( line );
		total += Length( line );
	}
	// Points along each outline, in proportion to its length; each outline makes its own star,
	// whose chords are kept only where they stay inside the shape.
	int perimeterPoints = 5 + rng.Int( 5 ) + (int)( c * 6.0f );
	int k               = 1 + rng.Int( 3 );
	std::vector< Vec > points;
	for( const std::vector< Vec >& line : lines )
	{
		float length = Length( line );
		int n        = std::max( 3, (int)std::round( (float)perimeterPoints * length / ( total / std::sqrt( (float)lines.size() ) ) ) );
		n            = std::min( n, 60 );
		float start  = rng.Range( 0.0f, length );
		size_t first = points.size();
		for( int i = 0; i < n; ++i )
			points.push_back( PointAlong( line, std::fmod( start + length * (float)i / (float)n, length ) ) );
		int step = std::min( k, std::max( 1, ( n - 1 ) / 2 ) );
		for( int i = 0; i < n; ++i )
		{
			Vec p = points[ first + i ], q = points[ first + ( i + step ) % n ];
			int samples = (int)std::ceil( std::hypot( q.x - p.x, q.y - p.y ) / ( f.step * 0.5f ) );
			bool inside = true;
			for( int s = 1; s < samples && inside; ++s )
			{
				float t = (float)s / (float)samples;
				inside  = f.At( p.x + ( q.x - p.x ) * t, p.y + ( q.y - p.y ) * t ) > 0.0f;
			}
			if( inside )
				b.Line( p.x, p.y, q.x, q.y );
		}
	}

	int order[ 5 ] = { 0, 1, 2, 3, 4 };
	for( int i = 4; i > 0; --i )
		std::swap( order[ i ], order[ rng.Int( i + 1 ) ] );
	int extras = 1 + (int)( c * 2.99f );
	for( int e = 0; e < extras; ++e )
	{
		switch( order[ e ] )
		{
		case 0:// inner outline
			for( const std::vector< Vec >& line : Contours( f, 0.5f * R ) )
				b.Path( line );
			break;
		case 1:// small circles on the points
			for( const Vec& p : points )
				b.Ring( p.x, p.y, 0.06f * R );
			break;
		case 2:// spokes from the deepest point
			for( const Vec& p : points )
				b.Line( deepest.centre.x, deepest.centre.y, p.x, p.y );
			break;
		case 3:// crescent in the middle
			b.Arc( deepest.centre.x, deepest.centre.y, 0.28f * R, PI * 0.3f, PI * 1.7f );
			b.Arc( deepest.centre.x + 0.1f * R, deepest.centre.y, 0.22f * R, PI * 0.35f, PI * 1.65f );
			break;
		default:// second outline just inside the edge
			for( const std::vector< Vec >& line : Contours( f, level * 0.4f ) )
				b.Path( line );
			break;
		}
	}
}

// Outlines of the shape inset step by step: plain, dashed, graduated or dotted; a crosshair in the middle.
void HudShape( Builder& b, Rng& rng, float c, const Field& f )
{
	Circle deepest = Largest( f, {} );
	if( deepest.radius <= 0.0f )
		return;
	float R     = deepest.radius;
	float level = std::max( 0.05f * R, f.step );
	int rings   = 2 + (int)( c * 3.0f );
	b.Place();
	for( int i = 0; i < rings && level < 0.85f * R; ++i )
	{
		int kind = rng.Int( 4 );
		for( const std::vector< Vec >& line : Contours( f, level ) )
		{
			float length = Length( line );
			switch( kind )
			{
			case 0:
				b.Path( line );
				break;
			case 1:
			{
				float dash = rng.Range( 0.08f, 0.2f ) * R;
				for( float s = 0.0f; s + dash * 0.5f < length; s += dash * 2.0f )
					b.Path( Piece( line, s, std::min( s + dash, length ) ) );
				break;
			}
			case 2:
			{
				b.Path( line );
				float spacing = 0.08f * R;
				int ticks     = std::min( (int)( length / spacing ), 200 );
				for( int t = 0; t < ticks; ++t )
				{
					Vec p   = PointAlong( line, (float)t * spacing );
					Vec out = Outward( f, p );
					float l = ( t % 4 == 0 ? 0.08f : 0.04f ) * R;
					b.Line( p.x, p.y, p.x - out.x * l, p.y - out.y * l );
				}
				break;
			}
			default:
			{
				b.Path( line );
				int dots = 2 + rng.Int( 3 );
				for( int t = 0; t < dots; ++t )
				{
					Vec p = PointAlong( line, rng.Range( 0.0f, length ) );
					b.Dot( p.x, p.y, 0.025f * R );
				}
				break;
			}
			}
		}
		level += rng.Range( 0.12f, 0.2f ) * R;
	}
	b.SetBase( deepest.centre.x, deepest.centre.y, R );
	b.Line( -0.12f, 0, -0.04f, 0 );
	b.Line( 0.04f, 0, 0.12f, 0 );
	b.Line( 0, -0.12f, 0, -0.04f );
	b.Line( 0, 0.04f, 0, 0.12f );
	b.Dot( 0, 0, 0.025f );
	b.SetBase();
}

}//End anonymous namespace

float Field::At( float x, float y ) const
{
	float u = ( x - x0 ) / step, v = ( y - y0 ) / step;
	if( cols < 2 || rows < 2 || u < 0.0f || v < 0.0f || u > (float)( cols - 1 ) || v > (float)( rows - 1 ) )
		return -step;
	int i = std::min( (int)u, cols - 2 ), j = std::min( (int)v, rows - 2 );
	float fu = u - (float)i, fv = v - (float)j;
	const float* row0 = &values[ (size_t)j * cols + i ];
	const float* row1 = row0 + cols;
	return ( row0[ 0 ] * ( 1.0f - fu ) + row0[ 1 ] * fu ) * ( 1.0f - fv ) + ( row1[ 0 ] * ( 1.0f - fu ) + row1[ 1 ] * fu ) * fv;
}

std::vector< Prim > Generate( Style style, uint32_t seed, float complexity, float boxX, float boxY, const Field* shape )
{
	Rng rng( seed );
	Builder b;
	if( style >= STYLE_COUNT )
		style = (Style)rng.Int( STYLE_COUNT );
	float c = std::min( std::max( complexity, 0.0f ), 1.0f );
	if( shape )
	{
		switch( style )
		{
		case FUTHARK: Pack( b, rng, c, *shape, Futhark ); break;
		case BINDRUNE: Pack( b, rng, c, *shape, Bindrune ); break;
		case RUNE_CIRCLE: RuneBand( b, rng, c, *shape ); break;
		case STAVE: Pack( b, rng, c, *shape, Stave ); break;
		case OGHAM: Pack( b, rng, c, *shape, Ogham ); break;
		case CIRCUIT: Circuit( b, rng, c, boxX, boxY, shape ); break;
		case CHIP: PackChips( b, rng, c, *shape ); break;
		case SIGIL: SigilShape( b, rng, c, *shape ); break;
		case CONSTELLATION: Constellation( b, rng, c, boxX, boxY, shape ); break;
		case MAZE: Maze( b, rng, c, boxX, boxY, shape ); break;
		default: HudShape( b, rng, c, *shape ); break;
		}
	}
	else
		switch( style )
		{
		case FUTHARK: Futhark( b, rng, c ); break;
		case BINDRUNE: Bindrune( b, rng, c ); break;
		case RUNE_CIRCLE: RuneCircle( b, rng, c ); break;
		case STAVE: Stave( b, rng, c ); break;
		case OGHAM: Ogham( b, rng, c ); break;
		case CIRCUIT: Circuit( b, rng, c, boxX, boxY, nullptr ); break;
		case CHIP: Chip( b, rng, c, boxX, boxY ); break;
		case SIGIL: Sigil( b, rng, c ); break;
		case CONSTELLATION: Constellation( b, rng, c, boxX, boxY, nullptr ); break;
		case MAZE: Maze( b, rng, c, boxX, boxY, nullptr ); break;
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
