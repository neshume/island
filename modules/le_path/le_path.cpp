#include "le_path.h"
#include "pal_api_loader/ApiRegistry.hpp"
#include "glm.hpp"
#include <vector>

#include <cstring>
#include <cstdio>
#include <cstdlib>

using Vertex = le_path_api::Vertex;

struct PathCommand {

	enum Type : uint32_t {
		eUnknown = 0,
		eMoveTo,
		eLineTo,
		eCurveTo,
		eQuadBezierTo = eCurveTo,
		eCubicBezierTo,
		eClosePath,
	} type = eUnknown;

	Vertex p  = {}; // end point
	Vertex c1 = {}; // control point 1
	Vertex c2 = {}; // control point 2
};

struct Contour {
	std::vector<PathCommand> commands; // svg-style commands+parameters creating the path
};

struct Polyline {
	std::vector<Vertex> vertices;
	std::vector<Vertex> tangents;
	std::vector<float>  distances;
	float               total_distance = 0;
};

struct le_path_o {
	std::vector<Contour>  contours;  // an array of sub-paths, a contour must start with a moveto instruction
	std::vector<Polyline> polylines; // an array of polylines, each corresponding to a sub-path.
};

// ----------------------------------------------------------------------

static le_path_o *le_path_create() {
	auto self = new le_path_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_path_destroy( le_path_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_path_clear( le_path_o *self ) {
	self->contours.clear();
	self->polylines.clear();
}

// ----------------------------------------------------------------------

static void trace_move_to( Polyline &polyline, Vertex const &p ) {
	polyline.distances.emplace_back( 0 );
	polyline.vertices.emplace_back( p );
	// NOTE: we dont insert a tangent here, as we need at least two
	// points to calculate tangents. In an open path, there will be n-1
	// tangent vectors than vertices, closed paths have same number of
	// tangent vectors as vertices.
}

// ----------------------------------------------------------------------

static void trace_line_to( Polyline &polyline, Vertex const &p ) {

	// We must check if the current point is identical with previous point -
	// in which case we will not add this point.

	auto const &p0               = polyline.vertices.back();
	Vertex      relativeMovement = p - p0;

	// Instead of using glm::distance directly, we calculate squared distance
	// so that we can filter out any potential invalid distance calculations -
	// distance cannot be calculated with two points which are identical,
	// because this would mean a division by zero. We must therefore filter out
	// any zero distances.

	float dist2 = glm::dot( relativeMovement, relativeMovement );

	static constexpr float epsilon2 = std::numeric_limits<float>::epsilon() * std::numeric_limits<float>::epsilon();

	if ( dist2 <= epsilon2 ) {
		// Distance to previous point is too small
		// No need to add this point twice.
		return;
	}

	polyline.total_distance += sqrtf( dist2 );
	polyline.distances.emplace_back( polyline.total_distance );
	polyline.vertices.emplace_back( p );
	polyline.tangents.emplace_back( relativeMovement );
}

// ----------------------------------------------------------------------

static void trace_close_path( Polyline &polyline ) {
	// eClosePath is the same as a direct line to the very first vertex.
	trace_line_to( polyline, polyline.vertices.front() );
}

// ----------------------------------------------------------------------

// Trace a quadratic bezier curve from previous point p0 to target point p2 (p2_x,p2_y),
// controlled by control point p1 (p1_x, p1_y), in steps iterations.
static void trace_quad_bezier_to( Polyline &    polyline,
                                  Vertex const &p1,        // end point
                                  Vertex const &c1,        // control point
                                  size_t        resolution // number of segments
) {

	if ( resolution == 0 ) {
		// nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may draw a
		// direct line to target point and return.
		trace_line_to( polyline, p1 );
		return;
	}

	// --------| invariant: resolution > 1

	polyline.vertices.reserve( polyline.vertices.size() + resolution );
	polyline.distances.reserve( polyline.vertices.size() );

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	Vertex const &p0     = polyline.vertices.back(); // copy start point
	Vertex        p_prev = p0;

	float delta_t = 1.f / float( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( size_t i = 1; i <= resolution; i++ ) {
		float t              = i * delta_t;
		float t_sq           = t * t;
		float one_minus_t    = ( 1.f - t );
		float one_minus_t_sq = one_minus_t * one_minus_t;

		Vertex b = one_minus_t_sq * p0 + 2 * one_minus_t * t * c1 + t_sq * p1;

		polyline.total_distance += glm::distance( b, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );
		p_prev = b;
		polyline.vertices.emplace_back( b );

		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
		polyline.tangents.emplace_back( 2 * one_minus_t * ( c1 - p0 ) + 2 * t * ( p1 - c1 ) );
	}
}

// ----------------------------------------------------------------------
// Trace a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void trace_cubic_bezier_to( Polyline &    polyline,
                                   Vertex const &p1,        // end point
                                   Vertex const &c1,        // control point 1
                                   Vertex const &c2,        // control point 2
                                   size_t        resolution // number of segments
) {
	if ( resolution == 0 ) {
		// Nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly trace to the target point and return.
		trace_line_to( polyline, p1 );
		return;
	}

	// --------| invariant: resolution > 1

	polyline.vertices.reserve( polyline.vertices.size() + resolution );

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	Vertex const p0     = polyline.vertices.back(); // copy start point
	Vertex       p_prev = p0;

	float delta_t = 1.f / float( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( size_t i = 1; i <= resolution; i++ ) {
		float t               = i * delta_t;
		float t_sq            = t * t;
		float t_cub           = t_sq * t;
		float one_minus_t     = ( 1.f - t );
		float one_minus_t_sq  = one_minus_t * one_minus_t;
		float one_minus_t_cub = one_minus_t_sq * one_minus_t;

		Vertex b = one_minus_t_cub * p0 + 3 * one_minus_t_sq * t * c1 + 3 * one_minus_t * t_sq * c2 + t_cub * p1;

		polyline.total_distance += glm::distance( b, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );
		p_prev = b;

		polyline.vertices.emplace_back( b );

		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
		polyline.tangents.emplace_back( 3 * one_minus_t_sq * ( c1 - p0 ) + 6 * one_minus_t * t * ( c2 - c1 ) + 3 * t_sq * ( p1 - c2 ) );
	}
}

// ----------------------------------------------------------------------
// Traces the path with all its subpaths into a list of polylines.
// Each subpath will be translated into one polyline.
// A polyline is a list of vertices which may be thought of being
// connected by lines.
//
static void le_path_trace_path( le_path_o *self, size_t resolution ) {

	self->polylines.clear();
	self->polylines.reserve( self->contours.size() );

	for ( auto const &s : self->contours ) {

		Polyline polyline;

		for ( auto const &command : s.commands ) {

			switch ( command.type ) {
			case PathCommand::eMoveTo:
				trace_move_to( polyline, command.p );
				break;
			case PathCommand::eLineTo:
				trace_line_to( polyline, command.p );
				break;
			case PathCommand::eQuadBezierTo:
				trace_quad_bezier_to( polyline,
				                      command.p,
				                      command.c1,
				                      resolution );
				break;
			case PathCommand::eCubicBezierTo:
				trace_cubic_bezier_to( polyline,
				                       command.p,
				                       command.c1,
				                       command.c2,
				                       resolution );
				break;
			case PathCommand::eClosePath:
				trace_close_path( polyline );
				break;
			case PathCommand::eUnknown:
				assert( false );
				break;
			}
		}

		assert( polyline.vertices.size() == polyline.distances.size() );

		self->polylines.emplace_back( polyline );
	}
}

// Subdivides given cubic bezier curve `b` at position `t`
// into two cubic bezier curves, `s_0`, and `s_1`
// curves are expected in the format `{p0,c0,c1,p1}`
static void bezier_subdivide( Vertex const b[ 4 ], float t, Vertex s_0[ 4 ], Vertex s_1[ 4 ] ) {

	auto const b2_   = b[ 2 ] + t * ( b[ 3 ] - b[ 2 ] );
	auto const b1_   = b[ 1 ] + t * ( b[ 2 ] - b[ 1 ] );
	auto const b0_   = b[ 0 ] + t * ( b[ 1 ] - b[ 0 ] );
	auto const b0__  = b0_ + t * ( b1_ - b0_ );
	auto const b1__  = b1_ + t * ( b2_ - b1_ );
	auto const b0___ = b0__ + t * ( b1__ - b0__ );

	if ( s_0 ) {
		s_0[ 0 ] = b[ 0 ];
		s_0[ 1 ] = b0_;
		s_0[ 2 ] = b0__;
		s_0[ 3 ] = b0___;
	}
	if ( s_1 ) {
		s_1[ 0 ] = b0___;
		s_1[ 1 ] = b1__;
		s_1[ 2 ] = b2_;
		s_1[ 3 ] = b[ 3 ];
	}
}

// ----------------------------------------------------------------------
// Flatten a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void flatten_cubic_bezier_to( Polyline &    polyline,
                                     Vertex const &p1,       // end point
                                     Vertex const &c1,       // control point 1
                                     Vertex const &c2,       // control point 2
                                     float         tolerance // max distance for arc segment
) {

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	Vertex const p0     = polyline.vertices.back(); // copy start point
	Vertex       p_prev = p0;

	float toi = tolerance;
	toi       = 0.04f;

	glm::vec2 b[ 4 ]{
	    p0,
	    c1,
	    c2,
	    p1,
	};

	float t = 0;
	for ( ;; ) {

		// create a coordinate basis based on the first point, and the first control point
		glm::vec2 r = glm::normalize( b[ 1 ] - b[ 0 ] );
		glm::vec2 s = {r.y, -r.x};

		glm::mat2 const  basis     = {r, s};
		glm::mat2 const &inv_basis = basis; // experiment (wolfram alpha) shows: inverse is same as original matrix: (because matrix is orthogonal?)

		b[ 1 ] = {basis * ( b[ 1 ] - b[ 0 ] )};
		b[ 2 ] = {basis * ( b[ 2 ] - b[ 0 ] )};
		b[ 3 ] = {basis * ( b[ 3 ] - b[ 0 ] )};
		b[ 0 ] = {};

		// first we define a coordinate basis built on the first two points, b0, and b1

		float t_dash = sqrtf( toi / ( 3 * fabsf( b[ 2 ].y ) ) );
		t            = std::min<float>( 1.f, t_dash * 2.f );

		float t_sq  = t * t;
		float t_cub = t_sq * t;

		glm::vec2 pt = b[ 0 ] +
		               3.f * ( b[ 1 ] - b[ 0 ] ) * t +
		               3.f * ( b[ 2 ] - 2.f * b[ 1 ] + b[ 0 ] ) * t_sq +
		               ( b[ 3 ] - 3.f * b[ 2 ] + 3.f * b[ 1 ] - b[ 0 ] ) * t_cub;

		// translate back into original coordinate system
		pt = p_prev + inv_basis * pt;

		polyline.vertices.emplace_back( pt );
		polyline.total_distance += glm::distance( pt, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );

		polyline.tangents.emplace_back();

		if ( t >= 1.0f )
			break;

		// Now apply subdivision: See p658 T.F. Hain et al.
		bezier_subdivide( b, t, nullptr, b );

		// transform bezier control points back into canonical coordinate system
		b[ 0 ] = p_prev + inv_basis * b[ 0 ];
		b[ 1 ] = p_prev + inv_basis * b[ 1 ];
		b[ 2 ] = p_prev + inv_basis * b[ 2 ];
		b[ 3 ] = p_prev + inv_basis * b[ 3 ];

		p_prev = pt;
	}

	//	float delta_t = 1.f / float( resolution );

	//	// Note that we begin the following loop at 1,
	//	// because element 0 (the starting point) is
	//	// already part of the contour.
	//	//
	//	// Loop goes over the set: ]0,resolution]
	//	//
	//	for ( size_t i = 1; i <= resolution; i++ ) {
	//		float t               = i * delta_t;
	//		float t_sq            = t * t;
	//		float t_cub           = t_sq * t;
	//		float one_minus_t     = ( 1.f - t );
	//		float one_minus_t_sq  = one_minus_t * one_minus_t;
	//		float one_minus_t_cub = one_minus_t_sq * one_minus_t;

	//		Vertex b = one_minus_t_cub * p0 + 3 * one_minus_t_sq * t * c1 + 3 * one_minus_t * t_sq * c2 + t_cub * p1;

	//		polyline.total_distance += glm::distance( b, p_prev );
	//		polyline.distances.emplace_back( polyline.total_distance );
	//		p_prev = b;

	//		polyline.vertices.emplace_back( b );

	//		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
	//		polyline.tangents.emplace_back( 3 * one_minus_t_sq * ( c1 - p0 ) + 6 * one_minus_t * t * ( c2 - c1 ) + 3 * t_sq * ( p1 - c2 ) );
	//	}
}

// ----------------------------------------------------------------------

static void le_path_flatten_path( le_path_o *self, float tolerance ) {

	self->polylines.clear();
	self->polylines.reserve( self->contours.size() );

	for ( auto const &s : self->contours ) {

		Polyline polyline;

		for ( auto const &command : s.commands ) {

			switch ( command.type ) {
			case PathCommand::eMoveTo:
				trace_move_to( polyline, command.p );
				break;
			case PathCommand::eLineTo:
				trace_line_to( polyline, command.p );
				break;
			case PathCommand::eQuadBezierTo:
				flatten_cubic_bezier_to( polyline,
				                         command.p,
				                         command.c1,
				                         command.c1,
				                         tolerance );
				break;
			case PathCommand::eCubicBezierTo:
				flatten_cubic_bezier_to( polyline,
				                         command.p,
				                         command.c1,
				                         command.c2,
				                         tolerance );
				break;
			case PathCommand::eClosePath:
				trace_close_path( polyline );
				break;
			case PathCommand::eUnknown:
				assert( false );
				break;
			}
		}

		assert( polyline.vertices.size() == polyline.distances.size() );

		self->polylines.emplace_back( polyline );
	}
}

// ----------------------------------------------------------------------

static void le_path_iterate_vertices_for_contour( le_path_o *self, size_t const &contour_index, le_path_api::contour_vertex_cb callback, void *user_data ) {

	assert( self->contours.size() > contour_index );

	auto const &s = self->contours[ contour_index ];

	for ( auto const &command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:        // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eLineTo:        // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eQuadBezierTo:  // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eCubicBezierTo: // fall-through, as we're allways just issueing the vertex, ignoring control points
			callback( user_data, command.p );
			break;
		case PathCommand::eClosePath:
			callback( user_data, s.commands[ 0 ].p ); // re-issue first vertex
			break;
		case PathCommand::eUnknown:
			assert( false );
			break;
		}
	}
}

// ----------------------------------------------------------------------

static void le_path_iterate_quad_beziers_for_contour( le_path_o *self, size_t const &contour_index, le_path_api::contour_quad_bezier_cb callback, void *user_data ) {

	assert( self->contours.size() > contour_index );

	auto const &s = self->contours[ contour_index ];

	Vertex p0 = {};

	for ( auto const &command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:
			p0 = command.p;
			break;
		case PathCommand::eLineTo:
			p0 = command.p;
			break;
		case PathCommand::eQuadBezierTo:
			callback( user_data, p0, command.p, command.c1 );
			p0 = command.p;
			break;
		case PathCommand::eCubicBezierTo:
			p0 = command.p;
			break;
		case PathCommand::eClosePath:
			break;
		case PathCommand::eUnknown:
			assert( false );
			break;
		}
	}
}

// ----------------------------------------------------------------------

static float clamp( float val, float range_min, float range_max ) {
	return val < range_min ? range_min : val > range_max ? range_max : val;
}

static float map( float val_, float range_min_, float range_max_, float min_, float max_ ) {
	return clamp( min_ + ( max_ - min_ ) * ( ( clamp( val_, range_min_, range_max_ ) - range_min_ ) / ( range_max_ - range_min_ ) ), min_, max_ );
}

// ----------------------------------------------------------------------
// Updates `result` to the vertex position on polyline
// at normalized position `t`
static void le_polyline_get_at( Polyline const &polyline, float t, Vertex &result ) {

	// -- Calculate unnormalised distance
	float d = t * float( polyline.total_distance );

	// find the first element in polyline which has a position larger than pos

	size_t       a = 0, b = 1;
	size_t const n = polyline.distances.size();

	assert( n >= 2 ); // we must have at least two elements for this to work.

	for ( ; b < n - 1; ++a, ++b ) {
		if ( polyline.distances[ b ] > d ) {
			// find the second distance which is larger than our test distance
			break;
		}
	}

	assert( b < n ); // b must not overshoot.

	float dist_start = polyline.distances[ a ];
	float dist_end   = polyline.distances[ b ];

	float scalar = map( d, dist_start, dist_end, 0.f, 1.f );

	Vertex const &start_vertex = polyline.vertices[ a ];
	Vertex const &end_vertex   = polyline.vertices[ b ];

	result = start_vertex + scalar * ( end_vertex - start_vertex );
}

// ----------------------------------------------------------------------
// return calculated position on polyline
static void le_path_get_polyline_at_pos_interpolated( le_path_o *self, size_t const &polyline_index, float t, Vertex &result ) {
	assert( polyline_index < self->polylines.size() );
	le_polyline_get_at( self->polylines[ polyline_index ], t, result );
}

// ----------------------------------------------------------------------

static void le_polyline_resample( Polyline &polyline, float interval ) {
	Polyline poly_resampled;

	// -- How many times can we fit interval into length of polyline?

	// Find next integer multiple
	size_t n_segments = size_t( std::round( polyline.total_distance / interval ) );
	n_segments        = std::max( size_t( 1 ), n_segments );

	float delta = 1.f / float( n_segments );

	if ( n_segments == 1 ) {
		// we cannot resample polylines which have only one segment.
		return;
	}

	// reserve n vertices

	poly_resampled.vertices.reserve( n_segments + 1 );
	poly_resampled.distances.reserve( n_segments + 1 );
	poly_resampled.tangents.reserve( n_segments + 1 );

	// Find first point
	Vertex vertex;
	le_polyline_get_at( polyline, 0.f, vertex );
	trace_move_to( poly_resampled, vertex );

	// Note that we must add an extra vertex at the end so that we
	// capture the correct number of segments.
	for ( size_t i = 1; i <= n_segments; ++i ) {
		le_polyline_get_at( polyline, i * delta, vertex );
		// We use trace_line_to, because this will get us more accurate distance
		// calculations - trace_line_to updates the distances as a side-effect,
		// effectively redrawing the polyline as if it was a series of `line_to`s.
		trace_line_to( poly_resampled, vertex );
	}

	std::swap( polyline, poly_resampled );
}

// ----------------------------------------------------------------------

static void le_path_resample( le_path_o *self, float interval ) {

	if ( self->contours.empty() ) {
		// nothing to do.
		return;
	}

	// --------| invariant: subpaths exist

	if ( self->polylines.empty() ) {
		le_path_trace_path( self, 100 ); // We must trace path - we will do it at a fairy high resolution.
	}

	// Resample each polyline, turn by turn

	for ( auto &p : self->polylines ) {
		le_polyline_resample( p, interval );
		// -- Enforce invariant that says for closed paths:
		// First and last vertex must be identical.
	}
}

// ----------------------------------------------------------------------

static void le_path_move_to( le_path_o *self, Vertex const *p ) {
	// move_to means a new subpath, unless the last command was a
	self->contours.emplace_back(); // add empty subpath
	self->contours.back().commands.push_back( {PathCommand::eMoveTo, *p} );
}

// ----------------------------------------------------------------------

static void le_path_line_to( le_path_o *self, Vertex const *p ) {
	if ( self->contours.empty() ) {
		constexpr static auto v0 = Vertex{};
		le_path_move_to( self, &v0 );
	}
	assert( !self->contours.empty() ); //subpath must exist
	self->contours.back().commands.push_back( {PathCommand::eLineTo, *p} );
}

// ----------------------------------------------------------------------

// Fetch the current pen point by grabbing the previous target point
// from the command stream.
static Vertex const *le_path_get_previous_p( le_path_o *self ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	Vertex const *p = nullptr;

	auto const &c = self->contours.back().commands.back(); // fetch last command

	switch ( c.type ) {
	case PathCommand::eMoveTo:        // fall-through
	case PathCommand::eLineTo:        // fall-through
	case PathCommand::eQuadBezierTo:  // fall-through
	case PathCommand::eCubicBezierTo: // fall-through
		p = &c.p;
		break;
	default:
		// Error. Previous command must be one of above
		fprintf( stderr, "Warning: Relative path instruction requires absolute position to be known. In %s:%i\n", __FILE__, __LINE__ );
		break;
	}

	return p;
}

// ----------------------------------------------------------------------

static void le_path_line_horiz_to( le_path_o *self, float px ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		Vertex p2 = *p;
		p2.x      = px;
		le_path_line_to( self, &p2 );
	}
}

// ----------------------------------------------------------------------

static void le_path_line_vert_to( le_path_o *self, float py ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		Vertex p2 = *p;
		p2.y      = py;
		le_path_line_to( self, &p2 );
	}
}

// ----------------------------------------------------------------------

static void le_path_quad_bezier_to( le_path_o *self, Vertex const *p, Vertex const *c1 ) {
	assert( !self->contours.empty() ); //contour must exist
	self->contours.back().commands.push_back( {PathCommand::eQuadBezierTo, *p, *c1} );
}

// ----------------------------------------------------------------------

static void le_path_cubic_bezier_to( le_path_o *self, Vertex const *p, Vertex const *c1, Vertex const *c2 ) {
	assert( !self->contours.empty() ); //subpath must exist
	self->contours.back().commands.push_back( {PathCommand::eCubicBezierTo, *p, *c1, *c2} );
}

// ----------------------------------------------------------------------

static void le_path_close_path( le_path_o *self ) {
	self->contours.back().commands.push_back( {PathCommand::eClosePath} );
}

// ----------------------------------------------------------------------

static size_t le_path_get_num_polylines( le_path_o *self ) {
	return self->polylines.size();
}

static size_t le_path_get_num_contours( le_path_o *self ) {
	return self->contours.size();
}

// ----------------------------------------------------------------------

static void le_path_get_vertices_for_polyline( le_path_o *self, size_t const &polyline_index, Vertex const **vertices, size_t *numVertices ) {
	assert( polyline_index < self->polylines.size() );

	auto const &polyline = self->polylines[ polyline_index ];

	*vertices    = polyline.vertices.data();
	*numVertices = polyline.vertices.size();
}

// ----------------------------------------------------------------------

static void le_path_get_tangents_for_polyline( le_path_o *self, size_t const &polyline_index, Vertex const **tangents, size_t *numTangents ) {
	assert( polyline_index < self->polylines.size() );

	auto const &polyline = self->polylines[ polyline_index ];

	*tangents    = polyline.tangents.data();
	*numTangents = polyline.tangents.size();
}

// ----------------------------------------------------------------------

// Accumulates `*offset_local` into `*offset_total`.
// Always returns true.
static bool add_offsets( int *offset_local, int *offset_total ) {
	( *offset_total ) += ( *offset_local );
	return true;
}

// Returns true if string c may be interpreted as
// a number,
// If true,
// + increases *offset by the count of characters forming the number.
// + sets *f to value of parsed number.
//
static bool is_float_number( char const *c, int *offset, float *f ) {
	if ( *c == 0 )
		return false;

	char *num_end;

	*f = strtof( c, &num_end ); // num_end will point to one after last number character
	*offset += ( num_end - c ); // add number of number characters to offset

	return num_end != c; // if at least one number character was extracted, we were successful
}

// Returns true if needle matches c.
// Increases *offset by 1 if true.
static bool is_character_match( char const needle, char const *c, int *offset ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	if ( *c == needle ) {
		++( *offset );
		return true;
	} else {
		return false;
	}
}

// Returns true if what c points to may be interpreted as
// whitespace, and sets offset to the count of whitespace
// characters.
static bool is_whitespace( char const *c, int *offset ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	bool found_whitespace = false;

	// while c is one of possible whitespace characters
	while ( *c == 0x20 || *c == 0x9 || *c == 0xD || *c == 0xA ) {
		c++;
		++( *offset );
		found_whitespace = true;
	}

	return found_whitespace;
}

// Returns true if c points to a coordinate pair.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*coord` will hold the vertex defined by the coordinate pair
static bool is_coordinate_pair( char const *c, int *offset, Vertex *v ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	// we want the pattern:

	int local_offset = 0;

	return is_float_number( c, &local_offset, &v->x ) &&                 // note how offset is re-used
	       is_character_match( ',', c + local_offset, &local_offset ) && // in subsequent tests, so that
	       is_float_number( c + local_offset, &local_offset, &v->y ) &&  // each test begins at the previous offset
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'm' instruction.
// An 'm' instruction is a move_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the target point
static bool is_m_instruction( char const *c, int *offset, Vertex *p0 ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	int local_offset = 0;

	return is_character_match( 'M', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'l' instruction.
// An 'l' instruction is a line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the target point
static bool is_l_instruction( char const *c, int *offset, Vertex *p0 ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'L', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'h' instruction.
// A 'h' instruction is a horizontal line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*px` will hold the value of the target point's x coordinate
static bool is_h_instruction( char const *c, int *offset, float *px ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'H', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_float_number( c + local_offset, &local_offset, px ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'l' instruction.
// A 'v' instruction is a vertical line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*px` will hold the value of the target point's x coordinate
static bool is_v_instruction( char const *c, int *offset, float *py ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'V', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_float_number( c + local_offset, &local_offset, py ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as a 'c' instruction.
// A 'c' instruction is a cubic bezier instruction.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of control point 0
// + `*p1` will hold the value of control point 1
// + `*p2` will hold the value of the target point
static bool is_c_instruction( char const *c, int *offset, Vertex *p0, Vertex *p1, Vertex *p2 ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'C', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p1 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p2 ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as a 'q' instruction.
// A 'q' instruction is a quadratic bezier instruction.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the control point
// + `*p1` will hold the value of the target point
static bool is_q_instruction( char const *c, int *offset, Vertex *p0, Vertex *p1 ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'Q', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p1 ) &&
	       add_offsets( &local_offset, offset );
}

// ----------------------------------------------------------------------
// Parse string `svg` for simplified SVG instructions and adds paths
// based on instructions found.
//
// Rules for similified SVG:
//
// - All coordinates must be absolute
// - Commands must be repeated
// - Allowed instruction tokens are:
//	 - 'M', with params {  p        } (moveto),
//	 - 'L', with params {  p        } (lineto),
//	 - 'C', with params { c0, c1, p } (cubic bezier to),
//	 - 'Q', with params { c0,  p,   } (quad bezier to),
//	 - 'Z', with params {           } (close path)
//
// You may set up Inkscape to output simplified SVG via:
// `Edit -> Preferences -> SVG Output ->
// (tick) Force Repeat Commands, Path string format (select: Absolute)`
//
static void le_path_add_from_simplified_svg( le_path_o *self, char const *svg ) {

	char const *c = svg;

	Vertex p0 = {};
	Vertex p1 = {};
	Vertex p2 = {};

	for ( ; *c != 0; ) // We test for the \0 character, end of c-string
	{

		int offset = 0;

		if ( is_m_instruction( c, &offset, &p0 ) ) {
			// moveto event
			le_path_move_to( self, &p0 );
			c += offset;
			continue;
		}
		if ( is_l_instruction( c, &offset, &p0 ) ) {
			// lineto event
			le_path_line_to( self, &p0 );
			c += offset;
			continue;
		}
		if ( is_h_instruction( c, &offset, &p0.x ) ) {
			// lineto event
			le_path_line_horiz_to( self, p0.x );
			c += offset;
			continue;
		}
		if ( is_v_instruction( c, &offset, &p0.y ) ) {
			// lineto event
			le_path_line_vert_to( self, p0.y );
			c += offset;
			continue;
		}
		if ( is_c_instruction( c, &offset, &p0, &p1, &p2 ) ) {
			// cubic bezier event
			le_path_cubic_bezier_to( self, &p2, &p0, &p1 ); // Note that end vertex is p2 from SVG,
			                                                // as SVG has target vertex as last vertex
			c += offset;
			continue;
		}
		if ( is_q_instruction( c, &offset, &p0, &p1 ) ) {
			// quadratic bezier event
			le_path_quad_bezier_to( self, &p1, &p0 ); // Note that target vertex is p1 from SVG,
			                                          // as SVG has target vertex as last vertex
			c += offset;
			continue;
		}
		if ( is_character_match( 'Z', c, &offset ) ) {
			// close path event.
			le_path_close_path( self );
			c += offset;
			continue;
		}

		// ----------| Invariant: None of the cases above did match

		// If none of the above cases match, the current character is
		// invalid, or does not contribute. Most likely it is a white-
		// space character.

		++c; // Ignore current character.
	}
};

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_path_api( void *api ) {
	auto &le_path_i = static_cast<le_path_api *>( api )->le_path_i;

	le_path_i.create                  = le_path_create;
	le_path_i.destroy                 = le_path_destroy;
	le_path_i.move_to                 = le_path_move_to;
	le_path_i.line_to                 = le_path_line_to;
	le_path_i.quad_bezier_to          = le_path_quad_bezier_to;
	le_path_i.cubic_bezier_to         = le_path_cubic_bezier_to;
	le_path_i.close                   = le_path_close_path;
	le_path_i.add_from_simplified_svg = le_path_add_from_simplified_svg;

	le_path_i.get_num_contours                 = le_path_get_num_contours;
	le_path_i.get_num_polylines                = le_path_get_num_polylines;
	le_path_i.get_vertices_for_polyline        = le_path_get_vertices_for_polyline;
	le_path_i.get_tangents_for_polyline        = le_path_get_tangents_for_polyline;
	le_path_i.get_polyline_at_pos_interpolated = le_path_get_polyline_at_pos_interpolated;

	le_path_i.iterate_vertices_for_contour     = le_path_iterate_vertices_for_contour;
	le_path_i.iterate_quad_beziers_for_contour = le_path_iterate_quad_beziers_for_contour;

	le_path_i.trace    = le_path_trace_path;
	le_path_i.flatten  = le_path_flatten_path;
	le_path_i.resample = le_path_resample;
	le_path_i.clear    = le_path_clear;
}
