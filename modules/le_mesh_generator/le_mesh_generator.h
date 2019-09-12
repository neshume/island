#ifndef GUARD_le_mesh_generator_H
#define GUARD_le_mesh_generator_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_mesh_o;

void register_le_mesh_generator_api( void *api );

// clang-format off
struct le_mesh_generator_api {
	static constexpr auto id      = "le_mesh_generator";
	static constexpr auto pRegFun = register_le_mesh_generator_api;

	struct le_mesh_generator_interface_t {

		void ( *generate_sphere )(
		    le_mesh_o * mesh,
		    float                 radius,         //
		    uint32_t              widthSegments,  //
		    uint32_t              heightSegments, //
		    float                 phiStart,       // 0..2pi (default: 0)
		    float                 phiLength,      // 0..2pi (default: 2pi)
		    float                 thetaStart,     // 0..pi  (default: 0)
		    float                 thetaLength     // 0..pi  (default: pi)
		);

		void ( *generate_plane )(le_mesh_o* mesh, float width, float height, uint32_t widthSegments, uint32_t heightSegments);

	};

	le_mesh_generator_interface_t le_mesh_generator_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_mesh_generator {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_mesh_generator_api>( true );
#	else
const auto api = Registry::addApiStatic<le_mesh_generator_api>();
#	endif

static const auto &le_mesh_generator_i = api -> le_mesh_generator_i;

} // namespace le_mesh_generator

class LeMeshGenerator : NoCopy, NoMove {
#	ifndef this_i
#		define this_i le_mesh_generator::le_mesh_generator_i

	static constexpr float PI = 3.14159265358979323846f;

  public:
	static void generateSphere( le_mesh_o *mesh,
	                            float      radius         = 1.f,
	                            uint32_t   widthSegments  = 3,
	                            uint32_t   heightSegments = 2,
	                            float      phiStart       = 0.f,
	                            float      phiLength      = 2 * PI,
	                            float      thetaStart     = 0.f,
	                            float      thetaLength    = PI ) {

		this_i.generate_sphere( mesh, radius, widthSegments, heightSegments, phiStart, phiLength, thetaStart, thetaLength );
	}

	static void generatePlane( le_mesh_o *mesh, float width, float height, uint32_t widthSegments = 2, uint32_t heightSegments = 2 ) {
		this_i.generate_plane( mesh, width, height, widthSegments, heightSegments );
	}
#		undef this_i
#	endif
};

#endif // __cplusplus

#endif
