#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include "le_core/le_core.h"
#include "private/le_renderer_types.h"

extern const uint64_t LE_RENDERPASS_MARKER_EXTERNAL; // set in le_renderer.cpp

struct le_renderer_o;
struct le_render_module_o;
struct le_renderpass_o;
struct le_rendergraph_o;
struct le_command_buffer_encoder_o;
struct le_backend_o;
struct le_shader_module_o; ///< shader module, 1:1 relationship with a shader source file
struct le_pipeline_manager_o;

struct le_allocator_o;         // from backend
struct le_staging_allocator_o; // from backend

struct le_shader_binding_table_o;

// clang-format off
struct le_renderer_api {

	struct renderer_interface_t {
		le_renderer_o *                ( *create                                )( );
		void                           ( *destroy                               )( le_renderer_o *obj );
		void                           ( *setup                                 )( le_renderer_o *obj, le_renderer_settings_t const & settings );
		void                           ( *update                                )( le_renderer_o *obj, le_render_module_o *module );
        le_shader_module_o*            ( *create_shader_module                  )( le_renderer_o *self, char const *path, const LeShaderStageEnum& mtype, char const * macro_definitions );

		/// returns the resource handle for the current swapchain image
		le_resource_handle_t           ( *get_swapchain_resource                )( le_renderer_o* self );
		void                           ( *get_swapchain_extent                  )( le_renderer_o* self, uint32_t* p_width, uint32_t* p_height);
		le_backend_o*                  ( *get_backend                           )( le_renderer_o* self );

		le_pipeline_manager_o*         ( *get_pipeline_manager                  )( le_renderer_o* self );

        struct le_texture_handle_store_t * le_texture_handle_store = nullptr;
        le_texture_handle              ( *produce_texture_handle                )(char const * maybe_name );
        char const *                   ( *texture_handle_get_name               )(le_texture_handle handle);

		le_rtx_blas_info_handle        ( *create_rtx_blas_info ) (le_renderer_o* self, le_rtx_geometry_t* geometries, uint32_t geometries_count, LeBuildAccelerationStructureFlags const * flags);
		le_rtx_tlas_info_handle        ( *create_rtx_tlas_info ) (le_renderer_o* self, uint32_t instances_count, LeBuildAccelerationStructureFlags const * flags);
	};



	struct helpers_interface_t {
		le_resource_info_t (*get_default_resource_info_for_image)();
		le_resource_info_t (*get_default_resource_info_for_buffer)();
	};

	typedef bool ( *pfn_renderpass_setup_t )( le_renderpass_o *obj, void* user_data );
	typedef void ( *pfn_renderpass_execute_t )( le_command_buffer_encoder_o *encoder, void *user_data );

	struct renderpass_interface_t {
		le_renderpass_o *               ( *create               )( const char *renderpass_name, const LeRenderPassType &type_ );
		void                            ( *destroy              )( le_renderpass_o *obj );
		le_renderpass_o *               ( *clone                )( const le_renderpass_o *obj );
        void                            ( *set_setup_callback   )( le_renderpass_o *obj, void *user_data, pfn_renderpass_setup_t setup_fun );
		bool                            ( *has_setup_callback   )( const le_renderpass_o* obj);
		void                            ( *add_color_attachment )( le_renderpass_o *obj, le_resource_handle_t resource_id, le_image_attachment_info_t const *info );
		void                            ( *add_depth_stencil_attachment )( le_renderpass_o *obj, le_resource_handle_t resource_id, le_image_attachment_info_t const *info );
		uint32_t                        ( *get_width            )( le_renderpass_o const * obj);
		uint32_t                        ( *get_height           )( le_renderpass_o const * obj);
		void                            ( *set_width            )( le_renderpass_o* obj, uint32_t width);
		void                            ( *set_height           )( le_renderpass_o* obj, uint32_t height);
		void                            ( *set_sample_count     ) (le_renderpass_o* obj, le::SampleCountFlagBits const & sampleCount);
		le::SampleCountFlagBits const & ( *get_sample_count     ) (const le_renderpass_o* obj ); 
        void                            ( *set_execute_callback )( le_renderpass_o *obj, void *user_data, pfn_renderpass_execute_t render_fun );
		bool                            ( *has_execute_callback )( const le_renderpass_o* obj);
		void                            ( *use_resource         )( le_renderpass_o *obj, const le_resource_handle_t& resource_id, const LeResourceUsageFlags &usage_flags);
		void                            ( *set_is_root          )( le_renderpass_o *obj, bool is_root );
		bool                            ( *get_is_root          )( const le_renderpass_o *obj);
		void                            ( *set_sort_key         )( le_renderpass_o *obj, uint64_t sort_key);
		uint64_t                        ( *get_sort_key         )( const le_renderpass_o *obj);
		void                            ( *get_used_resources   )( const le_renderpass_o *obj, le_resource_handle_t const **pResourceIds, LeResourceUsageFlags const **pResourcesUsage, size_t *count );
		const char*                     ( *get_debug_name       )( const le_renderpass_o* obj );
		uint64_t                        ( *get_id               )( const le_renderpass_o* obj );
		LeRenderPassType                ( *get_type             )( const le_renderpass_o* obj );
		le_command_buffer_encoder_o*    ( *steal_encoder        )( le_renderpass_o* obj );
		void                            ( *get_image_attachments)(const le_renderpass_o* obj, const le_image_attachment_info_t** pAttachments, const le_resource_handle_t** pResourceIds, size_t* numAttachments);

		// Reference counting
		void (*ref_inc)(le_renderpass_o* self);
		void (*ref_dec)(le_renderpass_o* self);

		// TODO: not too sure about the nomenclature of this
		// Note that this method implicitly marks the image resource referenced in LeTextureInfo for read access.
		void                         ( *sample_texture        )(le_renderpass_o* obj, le_texture_handle texture, const le_image_sampler_info_t* info);

		void                         ( *get_texture_ids       )(le_renderpass_o* obj, le_texture_handle const ** pIds, uint64_t* count);
		void                         ( *get_texture_infos     )(le_renderpass_o* obj, le_image_sampler_info_t const ** pInfos, uint64_t* count);
	};

	struct rendermodule_interface_t {
		le_render_module_o * ( *create           )();
		void                 ( *destroy          )( le_render_module_o *self );
		void                 ( *add_renderpass   )( le_render_module_o *self, le_renderpass_o *rp );
		void                 ( *setup_passes     )( le_render_module_o *self, le_rendergraph_o *gb );
		void                 ( *declare_resource )( le_render_module_o *self, le_resource_handle_t const & resource_id, le_resource_info_t const & info);
	};

	// Graph builder builds a graph for a module
	struct rendergraph_interface_t {
		le_rendergraph_o *   ( *create                 ) ();
		void                 ( *destroy                ) ( le_rendergraph_o *self );
		void                 ( *reset                  ) ( le_rendergraph_o *self );

		void                 ( *build                  ) ( le_rendergraph_o *self, size_t frameNumber );
		void                 ( *execute                ) ( le_rendergraph_o *self, size_t frameIndex, le_backend_o *backend );

		void                 ( *get_passes             ) ( le_rendergraph_o *self, le_renderpass_o ***pPasses, size_t *pNumPasses );
		void                 ( *get_declared_resources ) ( le_rendergraph_o *self, le_resource_handle_t const **p_resource_handles, le_resource_info_t const **p_resource_infos, size_t *p_resource_count );
	};

	struct command_buffer_encoder_interface_t {
		le_command_buffer_encoder_o *( *create                 )( le_allocator_o **allocator, le_pipeline_manager_o* pipeline_cache, le_staging_allocator_o* stagingAllocator, le::Extent2D const& extent );
		void                         ( *destroy                )( le_command_buffer_encoder_o *obj );

		void                         ( *draw                   )( le_command_buffer_encoder_o *self, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance );
		void                         ( *draw_indexed           )( le_command_buffer_encoder_o *self, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
		void                         ( *draw_mesh_tasks        )( le_command_buffer_encoder_o *self, uint32_t taskCount, uint32_t fistTask);

		void                         (* dispatch               )( le_command_buffer_encoder_o *self, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ );

		void                         ( *set_line_width         )( le_command_buffer_encoder_o *self, float line_width_ );
		void                         ( *set_viewport           )( le_command_buffer_encoder_o *self, uint32_t firstViewport, const uint32_t viewportCount, const le::Viewport *pViewports );
		void                         ( *set_scissor            )( le_command_buffer_encoder_o *self, uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D *pViewports );

		void                         ( *bind_graphics_pipeline )( le_command_buffer_encoder_o *self, le_gpso_handle pipelineHandle);
		void                         ( *bind_compute_pipeline  )( le_command_buffer_encoder_o *self, le_cpso_handle pipelineHandle);

		void                         ( *bind_index_buffer      )( le_command_buffer_encoder_o *self, le_resource_handle_t const bufferId, uint64_t offset, le::IndexType const & indexType);
		void                         ( *bind_vertex_buffers    )( le_command_buffer_encoder_o *self, uint32_t firstBinding, uint32_t bindingCount, le_resource_handle_t const * pBufferId, uint64_t const * pOffsets );

		void                         ( *set_index_data         )( le_command_buffer_encoder_o *self, void const *data, uint64_t numBytes, le::IndexType const & indexType );
		void                         ( *set_vertex_data        )( le_command_buffer_encoder_o *self, void const *data, uint64_t numBytes, uint32_t bindingIndex );

		void                         ( *write_to_buffer        )( le_command_buffer_encoder_o *self, le_resource_handle_t const& resourceId, size_t offset, void const* data, size_t numBytes);
		void                         ( *write_to_image         )( le_command_buffer_encoder_o *self, le_resource_handle_t const& resourceId, le_write_to_image_settings_t const & writeInfo, void const *data, size_t numBytes );


		le::Extent2D const &         ( *get_extent             ) ( le_command_buffer_encoder_o* self );

		void                         ( *bind_argument_buffer   )( le_command_buffer_encoder_o *self, le_resource_handle_t const bufferId, uint64_t argumentName, uint64_t offset, uint64_t range );

		void                         ( *set_argument_data      )( le_command_buffer_encoder_o *self, uint64_t argumentNameId, void const * data, size_t numBytes);
		void                         ( *set_argument_texture   )( le_command_buffer_encoder_o *self, le_texture_handle const textureId, uint64_t argumentName, uint64_t arrayIndex);
		void                         ( *set_argument_image     )( le_command_buffer_encoder_o *self, le_resource_handle_t const imageId, uint64_t argumentName, uint64_t arrayIndex);
		void                         ( *set_argument_tlas      )( le_command_buffer_encoder_o *self, le_resource_handle_t const tlasId, uint64_t argumentName, uint64_t arrayIndex);

		void 						 ( *build_rtx_blas         )( le_command_buffer_encoder_o *self, le_resource_handle_t const* const blas_handles, const uint32_t handles_count);
        // one blas handle per instance
		void 						 ( *build_rtx_tlas         )( le_command_buffer_encoder_o *self, le_resource_handle_t const* tlas_handle, le_rtx_geometry_instance_t const * instances, le_resource_handle_t const * blas_handles, uint32_t instances_count);
        
        le_shader_binding_table_o*   ( *build_sbt              )(le_command_buffer_encoder_o* self, le_rtxpso_handle pipeline);
        void                         ( *sbt_set_ray_gen        )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_hit            )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_callable       )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_miss           )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_u32_param      )(le_shader_binding_table_o* sbt, uint32_t param);
        void                         ( *sbt_add_f32_param      )(le_shader_binding_table_o* sbt, float param);

        // returns nullptr if shader binding table is in invalid state, otherwise sbt
        le_shader_binding_table_o*   ( *sbt_validate           )(le_shader_binding_table_o* sbt);

        // NOTE pipeline is implicitly bound, as it is stored with shader binding table (sbt)
		void                         ( *bind_rtx_pipeline      )( le_command_buffer_encoder_o *self, le_shader_binding_table_o* shader_binding_table);
        void                         ( *trace_rays             )( le_command_buffer_encoder_o* self, uint32_t width, uint32_t height, uint32_t depth);

		le_pipeline_manager_o*       ( *get_pipeline_manager   )( le_command_buffer_encoder_o *self );
		void                         ( *get_encoded_data       )( le_command_buffer_encoder_o *self, void **data, size_t *numBytes, size_t *numCommands );
	};

	renderer_interface_t               le_renderer_i;
	renderpass_interface_t             le_renderpass_i;
	rendermodule_interface_t           le_render_module_i;
	rendergraph_interface_t            le_rendergraph_i;
	command_buffer_encoder_interface_t le_command_buffer_encoder_i;
	helpers_interface_t                helpers_i;
};
// clang-format on

LE_MODULE( le_renderer );
LE_MODULE_LOAD_DEFAULT( le_renderer );

#ifdef __cplusplus

namespace le_renderer {
static const auto &api = le_renderer_api_i;

static const auto &renderer_i      = api -> le_renderer_i;
static const auto &renderpass_i    = api -> le_renderpass_i;
static const auto &render_module_i = api -> le_render_module_i;
static const auto &rendergraph_i   = api -> le_rendergraph_i;
static const auto &encoder_i       = api -> le_command_buffer_encoder_i;
static const auto &helpers_i       = api -> helpers_i;

} // namespace le_renderer

namespace le {

class Renderer {

	le_renderer_o *self;

  public:
	Renderer()
	    : self( le_renderer::renderer_i.create() ) {
	}

	~Renderer() {
		le_renderer::renderer_i.destroy( self );
	}

	void setup( le_renderer_settings_t const &settings ) {
		le_renderer::renderer_i.setup( self, settings );
	}

	void setup( le_window_o *window ) {
		le_renderer::renderer_i.setup( self, le::RendererInfoBuilder( window ).build() );
	}

	void update( le_render_module_o *module ) {
		le_renderer::renderer_i.update( self, module );
	}

	le_shader_module_o *createShaderModule( char const *path, const le::ShaderStage &moduleType, char const *macro_definitions = nullptr ) const {
		return le_renderer::renderer_i.create_shader_module( self, path, { moduleType }, macro_definitions );
	}

	le_resource_handle_t getSwapchainResource() const {
		return le_renderer::renderer_i.get_swapchain_resource( self );
	}

	void getSwapchainExtent( uint32_t *pWidth, uint32_t *pHeight ) const {
		le_renderer::renderer_i.get_swapchain_extent( self, pWidth, pHeight );
	}

	const le::Extent2D getSwapchainExtent() const {
		le::Extent2D result;
		le_renderer::renderer_i.get_swapchain_extent( self, &result.width, &result.height );
		return result;
	}

	le_pipeline_manager_o *getPipelineManager() const {
		return le_renderer::renderer_i.get_pipeline_manager( self );
	}

	static le_texture_handle produceTextureHandle( char const *maybe_name ) {
		return le_renderer::renderer_i.produce_texture_handle( maybe_name );
	}

	operator auto() {
		return self;
	}
};

class RenderPass {

	le_renderpass_o *self;

  public:
	RenderPass( const char *name_, const LeRenderPassType &type_ = LE_RENDER_PASS_TYPE_DRAW )
	    : self( le_renderer::renderpass_i.create( name_, type_ ) ) {
	}

	RenderPass( const char *name_, const LeRenderPassType &type_, le_renderer_api::pfn_renderpass_setup_t fun_setup, le_renderer_api::pfn_renderpass_execute_t fun_exec, void *user_data )
	    : self( le_renderer::renderpass_i.create( name_, type_ ) ) {
		le_renderer::renderpass_i.set_setup_callback( self, user_data, fun_setup );
		le_renderer::renderpass_i.set_execute_callback( self, user_data, fun_exec );
	}

	// Create facade from pointer
	RenderPass( le_renderpass_o *self_ )
	    : self( self_ ) {
		le_renderer::renderpass_i.ref_inc( self );
	}

	// Destructor
	~RenderPass() {
		// We check for validity of self, as move assignment/constructor
		// set the moved-from to null after completion.
		if ( self ) {
			le_renderer::renderpass_i.ref_dec( self );
		}
	}

	// Copy constructor
	RenderPass( RenderPass const &rhs )
	    : self( rhs.self ) {
		le_renderer::renderpass_i.ref_inc( self );
	}

	// Copy assignment
	RenderPass &operator=( RenderPass const &rhs ) {
		self = rhs.self;
		le_renderer::renderpass_i.ref_inc( self );
		return *this;
	}

	// Move constructor
	RenderPass( RenderPass &&rhs ) noexcept
	    : self( rhs.self ) {
		rhs.self = nullptr;
	}

	// Move assignment
	RenderPass &operator=( RenderPass &&rhs ) {
		self     = rhs.self;
		rhs.self = nullptr;
		return *this;
	}

	operator auto() {
		return self;
	}

	operator const auto() const {
		return self;
	}

	RenderPass &setSetupCallback( void *user_data, le_renderer_api::pfn_renderpass_setup_t fun ) {
		le_renderer::renderpass_i.set_setup_callback( self, user_data, fun );
		return *this;
	}

	RenderPass &setExecuteCallback( void *user_data, le_renderer_api::pfn_renderpass_execute_t fun ) {
		le_renderer::renderpass_i.set_execute_callback( self, user_data, fun );
		return *this;
	}

	// ----

	/// \brief Adds a resource as an image attachment to the renderpass.
	/// \details resource is used for ColorAttachment and Write access, unless otherwise specified.
	///          Use an le_image_attachment_info_t struct to specialise parameters, such as LOAD_OP, CLEAR_OP, and Clear/Load Color.
	RenderPass &addColorAttachment( const le_resource_handle_t &      resource_id,
	                                const le_image_attachment_info_t &attachmentInfo = le_image_attachment_info_t() ) {
		le_renderer::renderpass_i.add_color_attachment( self, resource_id, &attachmentInfo );
		return *this;
	}

	RenderPass &addDepthStencilAttachment( const le_resource_handle_t &      resource_id,
	                                       const le_image_attachment_info_t &attachmentInfo = LeDepthAttachmentInfo() ) {
		le_renderer::renderpass_i.add_depth_stencil_attachment( self, resource_id, &attachmentInfo );
		return *this;
	}

	RenderPass &useImageResource( le_resource_handle_t resource_id, const LeImageUsageFlags &usage_flags ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, { LeResourceType::eImage, { { usage_flags } } } );
		return *this;
	}

	RenderPass &useBufferResource( le_resource_handle_t resource_id, const LeBufferUsageFlags &usage_flags ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, { LeResourceType::eBuffer, { { usage_flags } } } );
		return *this;
	}

#	ifdef LE_FEATURE_RTX
	RenderPass &useRtxBlasResource( le_resource_handle_t resource_id, const LeRtxBlasUsageFlags &usage_flags ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, { LeResourceType::eRtxBlas, { { usage_flags } } } );
		return *this;
	}

	RenderPass &useRtxTlasResource( le_resource_handle_t resource_id, const LeRtxTlasUsageFlags &usage_flags ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, { LeResourceType::eRtxTlas, { { usage_flags } } } );
		return *this;
	}
#	endif

	RenderPass &setIsRoot( bool isRoot = true ) {
		le_renderer::renderpass_i.set_is_root( self, isRoot );
		return *this;
	}

	RenderPass &sampleTexture( le_texture_handle textureName, const le_image_sampler_info_t &imageSamplerInfo ) {
		le_renderer::renderpass_i.sample_texture( self, textureName, &imageSamplerInfo );
		return *this;
	}

	RenderPass &setWidth( uint32_t width ) {
		le_renderer::renderpass_i.set_width( self, width );
		return *this;
	}

	RenderPass &setHeight( uint32_t height ) {
		le_renderer::renderpass_i.set_height( self, height );
		return *this;
	}

	RenderPass &setSampleCount( le::SampleCountFlagBits const &sampleCount ) {
		le_renderer::renderpass_i.set_sample_count( self, sampleCount );
		return *this;
	}
};

// ----------------------------------------------------------------------

class RenderModule {

	le_render_module_o *self;
	bool                is_reference = false;

  public:
	RenderModule()
	    : self( le_renderer::render_module_i.create() ) {
	}

	RenderModule( le_render_module_o *self_ )
	    : self( self_ )
	    , is_reference( true ) {
	}

	~RenderModule() {
		if ( !is_reference ) {
			le_renderer::render_module_i.destroy( self );
		}
	}

	operator auto() {
		return self;
	}

	RenderModule &addRenderPass( le_renderpass_o *renderpass ) {
		le_renderer::render_module_i.add_renderpass( self, renderpass );
		return *this;
	}

	RenderModule &declareResource( le_resource_handle_t const &resource_id, le_resource_info_t const &info ) {
		le_renderer::render_module_i.declare_resource( self, resource_id, info );
		return *this;
	}
};

// ----------------------------------------------------------------------

class ImageInfoBuilder : NoCopy, NoMove {
	le_resource_info_t             res = le_renderer::helpers_i.get_default_resource_info_for_image();
	le_resource_info_t::ImageInfo &img = res.image;

  public:
	// FIXME: This method does not check that the resource_info type is actually image!
	ImageInfoBuilder( const le_resource_info_t &info )
	    : res( info ) {
	}
	ImageInfoBuilder()  = default;
	~ImageInfoBuilder() = default;

	ImageInfoBuilder &setFormat( Format format ) {
		img.format = format;
		return *this;
	}

	ImageInfoBuilder &setCreateFlags( uint32_t flags = 0 ) {
		img.flags = flags;
		return *this;
	}

	ImageInfoBuilder &setArrayLayers( uint32_t arrayLayers = 1 ) {
		img.arrayLayers = arrayLayers;
		return *this;
	}

	ImageInfoBuilder &setExtent( uint32_t width, uint32_t height, uint32_t depth = 1 ) {
		img.extent.width  = width;
		img.extent.height = height;
		img.extent.depth  = depth;
		return *this;
	}

	ImageInfoBuilder &setUsageFlags( LeImageUsageFlags const &usageFlagBits ) {
		img.usage = usageFlagBits;
		return *this;
	}

	ImageInfoBuilder &addUsageFlags( LeImageUsageFlags const &usageFlagBits ) {
		img.usage |= usageFlagBits;
		return *this;
	}

	ImageInfoBuilder &setMipLevels( uint32_t mipLevels = 1 ) {
		img.mipLevels = mipLevels;
		return *this;
	}

	ImageInfoBuilder &setImageType( le::ImageType const &imageType = le::ImageType::e2D ) {
		img.imageType = imageType;
		return *this;
	}

	ImageInfoBuilder &setImageTiling( le::ImageTiling const &imageTiling = le::ImageTiling::eOptimal ) {
		img.tiling = imageTiling;
		return *this;
	}

	const le_resource_info_t &build() {
		return res;
	}
};

// ----------------------------------------------------------------------

class BufferInfoBuilder : NoCopy, NoMove {
	le_resource_info_t              res = le_renderer::helpers_i.get_default_resource_info_for_buffer();
	le_resource_info_t::BufferInfo &buf = res.buffer;

  public:
	// FIXME: This method does not check that the resource_info type is actually buffer!
	BufferInfoBuilder( const le_resource_info_t &info )
	    : res( info ) {
	}

	BufferInfoBuilder()  = default;
	~BufferInfoBuilder() = default;

	BufferInfoBuilder &setSize( uint32_t size ) {
		buf.size = size;
		return *this;
	}

	BufferInfoBuilder &setUsageFlags( LeBufferUsageFlags const &usageFlagBits ) {
		buf.usage = usageFlagBits;
		return *this;
	}

	BufferInfoBuilder &addUsageFlags( LeBufferUsageFlags const &usageFlagBits ) {
		buf.usage |= usageFlagBits;
		return *this;
	}

	const le_resource_info_t &build() {
		return res;
	}
};

// ----------------------------------------------------------------------

class Encoder {

	le_command_buffer_encoder_o *self = nullptr;

  public:
	Encoder()  = delete;
	~Encoder() = default;

	Encoder( le_command_buffer_encoder_o *self_ )
	    : self( self_ ) {
	}

	operator auto() {
		return self;
	}

	Extent2D const &getRenderpassExtent() {
		return le_renderer::encoder_i.get_extent( self );
	}

	Encoder &dispatch( const uint32_t &groupCountX = 1, const uint32_t &groupCountY = 1, const uint32_t &groupCountZ = 1 ) {
		le_renderer::encoder_i.dispatch( self, groupCountX, groupCountY, groupCountZ );
		return *this;
	}

	Encoder &draw( const uint32_t &vertexCount, const uint32_t &instanceCount = 1, const uint32_t &firstVertex = 0, const uint32_t &firstInstance = 0 ) {
		le_renderer::encoder_i.draw( self, vertexCount, instanceCount, firstVertex, firstInstance );
		return *this;
	}

	Encoder &drawIndexed( uint32_t const &indexCount, uint32_t const &instanceCount = 1, uint32_t const &firstIndex = 0, int32_t const &vertexOffset = 0, uint32_t const &firstInstance = 0 ) {
		le_renderer::encoder_i.draw_indexed( self, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance );
		return *this;
	}

	Encoder &drawMeshTasks( const uint32_t &taskCount, const uint32_t &firstTask = 0 ) {
		le_renderer::encoder_i.draw_mesh_tasks( self, taskCount, firstTask );
		return *this;
	}

	Encoder &traceRays( uint32_t const &width, uint32_t const &height, uint32_t const &depth = 1 ) {
		le_renderer::encoder_i.trace_rays( self, width, height, depth );
		return *this;
	}

	Encoder &setLineWidth( float const &lineWidth ) {
		le_renderer::encoder_i.set_line_width( self, lineWidth );
		return *this;
	}

	Encoder &setViewports( uint32_t firstViewport, const uint32_t &viewportCount, const le::Viewport *pViewports ) {
		le_renderer::encoder_i.set_viewport( self, firstViewport, viewportCount, pViewports );
		return *this;
	}

	Encoder &setScissors( uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D *pScissors ) {
		le_renderer::encoder_i.set_scissor( self, firstScissor, scissorCount, pScissors );
		return *this;
	}

	Encoder &bindGraphicsPipeline( le_gpso_handle pipelineHandle ) {
		le_renderer::encoder_i.bind_graphics_pipeline( self, pipelineHandle );
		return *this;
	}

	Encoder &bindRtxPipeline( le_shader_binding_table_o *sbt ) {
		le_renderer::encoder_i.bind_rtx_pipeline( self, sbt );
		return *this;
	}

	Encoder &bindComputePipeline( le_cpso_handle pipelineHandle ) {
		le_renderer::encoder_i.bind_compute_pipeline( self, pipelineHandle );
		return *this;
	}

	Encoder &bindIndexBuffer( le_resource_handle_t const &bufferId, uint64_t const &offset, IndexType const &indexType = IndexType::eUint16 ) {
		le_renderer::encoder_i.bind_index_buffer( self, bufferId, offset, indexType );
		return *this;
	}

	Encoder &bindVertexBuffers( uint32_t const &firstBinding, uint32_t const &bindingCount, le_resource_handle_t const *pBufferId, uint64_t const *pOffsets ) {
		le_renderer::encoder_i.bind_vertex_buffers( self, firstBinding, bindingCount, pBufferId, pOffsets );
		return *this;
	}

	/// \brief Set index data directly by uploading data via GPU scratch buffer
	/// \note if either `data == nullptr`, or numBytes == 0, this method call has no effect.
	Encoder &setIndexData( void const *data, uint64_t const &numBytes, IndexType const &indexType = IndexType::eUint16 ) {
		le_renderer::encoder_i.set_index_data( self, data, numBytes, indexType );
		return *this;
	}

	/// \brief Set vertex data directly by uploading data via GPU scratch buffer
	/// \note if either `data == nullptr`, or numBytes == 0, this method call has no effect.
	Encoder &setVertexData( void const *data, uint64_t const &numBytes, uint32_t const &bindingIndex ) {
		le_renderer::encoder_i.set_vertex_data( self, data, numBytes, bindingIndex );
		return *this;
	}

	Encoder &writeToBuffer( le_resource_handle_t const &resourceId, size_t const &byteOffset, void const *data, size_t const &numBytes ) {
		le_renderer::encoder_i.write_to_buffer( self, resourceId, byteOffset, data, numBytes );
		return *this;
	}

	Encoder &writeToImage( le_resource_handle_t const &resourceId, le_write_to_image_settings_t const &writeInfo, void const *data, size_t const &numBytes ) {
		le_renderer::encoder_i.write_to_image( self, resourceId, writeInfo, data, numBytes );
		return *this;
	}

	Encoder &setArgumentData( uint64_t const &argumentNameId, void const *data, size_t const &numBytes ) {
		le_renderer::encoder_i.set_argument_data( self, argumentNameId, data, numBytes );
		return *this;
	}

	Encoder &setArgumentTexture( uint64_t const &argumentName, le_texture_handle const &textureId, uint64_t const &arrayIndex = 0 ) {
		le_renderer::encoder_i.set_argument_texture( self, textureId, argumentName, arrayIndex );
		return *this;
	}

	Encoder &setArgumentImage( uint64_t const &argumentName, le_resource_handle_t const &imageId, uint64_t const &arrayIndex = 0 ) {
		le_renderer::encoder_i.set_argument_image( self, imageId, argumentName, arrayIndex );
		return *this;
	}

	Encoder &setArgumentTlas( uint64_t const &argumentName, le_resource_handle_t const &tlasId, uint64_t const &arrayIndex = 0 ) {
		le_renderer::encoder_i.set_argument_tlas( self, tlasId, argumentName, arrayIndex );
		return *this;
	}

	Encoder &bindArgumentBuffer( uint64_t const &argumentName, le_resource_handle_t const &bufferId, uint64_t const &offset = 0, uint64_t const &range = ( ~0ULL ) ) {
		le_renderer::encoder_i.bind_argument_buffer( self, bufferId, argumentName, offset, range );
		return *this;
	}

	class ShaderBindingTableBuilder {
		Encoder const &            parent;
		le_shader_binding_table_o *sbt = nullptr;

	  public:
		ShaderBindingTableBuilder( Encoder const &parent_, le_rtxpso_handle pso )
		    : parent( parent_ )
		    , sbt( le_renderer::encoder_i.build_sbt( parent.self, pso ) ) {
		}

		ShaderBindingTableBuilder &setRayGenIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_set_ray_gen( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder &addCallableIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_add_callable( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder &addHitIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_add_hit( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder &addMissIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_add_miss( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder &addParameterValueU32( uint32_t val ) {
			le_renderer::encoder_i.sbt_add_u32_param( sbt, val );
			return *this;
		}

		ShaderBindingTableBuilder &addParameterValueF32( uint32_t val ) {
			le_renderer::encoder_i.sbt_add_f32_param( sbt, val );
			return *this;
		}

		le_shader_binding_table_o *build() {
			return le_renderer::encoder_i.sbt_validate( sbt );
		}
	};

	le_pipeline_manager_o *getPipelineManager() {
		return le_renderer::encoder_i.get_pipeline_manager( self );
	}
};
// ----------------------------------------------------------------------

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H
