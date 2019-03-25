#include "le_backend_vk.h"

// NOTE: This header *must not* be included by anyone else but le_backend_vk.cpp or le_pipeline_builder.cpp.
//       Its sole purpose of being is to create a dependency inversion, so that both these compilation units
//       may share the same types for creating pipelines.

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include <vector>
#include "le_renderer/private/le_renderer_types.h" // for `le_vertex_input_attribute_description`, `le_vertex_input_binding_description`, `le_resource_handle`, `LeRenderPassType`

constexpr uint8_t VK_MAX_BOUND_DESCRIPTOR_SETS = 8;
constexpr uint8_t VK_MAX_COLOR_ATTACHMENTS     = 16; // maximum number of color attachments to a renderpass

// ----------------------------------------------------------------------
// Preprocessor Macro utilities
//

// Wraps an enum of `enum_name` in a struct with `struct_name` so
// that it can be opaquely passed around, then unwrapped.
#define LE_WRAP_ENUM_IN_STRUCT( enum_name, struct_name ) \
	struct struct_name {                                 \
	    enum_name data;                                  \
	    operator const enum_name &() const {             \
	        return data;                                 \
	    }                                                \
	    operator enum_name &() {                         \
	        return data;                                 \
	    }                                                \
	}

// ----------------------------------------------------------------------
// Utility methods
//
template <typename T>
static constexpr typename std::underlying_type<T>::type enumToNum( const T &enumVal ) {
	return static_cast<typename std::underlying_type<T>::type>( enumVal );
};

// ----------------------------------------------------------------------

LE_WRAP_ENUM_IN_STRUCT( vk::Format, VkFormatEnum ); // define wrapper struct `VkFormatEnum`

// ----------------------------------------------------------------------

struct le_graphics_pipeline_builder_data {

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{};
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	vk::PipelineTessellationStateCreateInfo  tessellationState{};
	vk::PipelineMultisampleStateCreateInfo   multisampleState{};
	vk::PipelineDepthStencilStateCreateInfo  depthStencilState{};

	std::array<vk::PipelineColorBlendAttachmentState, VK_MAX_COLOR_ATTACHMENTS> blendAttachmentStates{};
};

struct graphics_pipeline_state_o {
	le_graphics_pipeline_builder_data data{};

	std::vector<le_shader_module_o *> shaderStages; // non-owning; refers opaquely to a shader modules (or not)

	std::vector<le_vertex_input_attribute_description> explicitVertexAttributeDescriptions;    // only used if contains values, otherwise use from vertex shader reflection
	std::vector<le_vertex_input_binding_description>   explicitVertexInputBindingDescriptions; // only used if contains values, otherwise use from vertex shader reflection
};

struct compute_pipeline_state_o {
	le_shader_module_o *shaderStage; // non-owning; refers opaquely to a compute shader module (or not)
};

// clang-format off
// FIXME: microsoft places members in bitfields low to high (LSB first)
// and gcc appears to do the same - sorting is based on this assumption, and we must somehow test for it when compiling.
struct le_shader_binding_info {
	union {
		struct{
		uint64_t dynamic_offset_idx :  8; // only used when binding pipeline
		uint64_t stage_bits         :  6; // vkShaderFlags : which stages this binding is used for (must be at least 6 bits wide)
		uint64_t range              : 27; // only used for ubos (sizeof ubo)
		uint64_t type               :  4; // vkDescriptorType descriptor type
		uint64_t count              :  8; // number of elements
		uint64_t binding            :  8; // |\                           : binding index within set
		uint64_t setIndex           :  3; // |/ keep together for sorting : set index [0..7]
		};
		uint64_t data;
	};

	uint64_t name_hash; // const_char_hash of parameter name as given in shader

	bool operator < ( le_shader_binding_info const & lhs ){
		return data < lhs.data;
	}
};
// clang-format on

// ----------------------------------------------------------------------
struct le_descriptor_set_layout_t {
	std::vector<le_shader_binding_info> binding_info;                  // binding info for this set
	vk::DescriptorSetLayout             vk_descriptor_set_layout;      // vk object
	vk::DescriptorUpdateTemplate        vk_descriptor_update_template; // template used to update such a descriptorset based on descriptor data laid out in flat DescriptorData elements
};

// Everything a possible vulkan descriptor binding might contain.
// Type of descriptor decides which values will be used.

struct DescriptorData {
	// NOTE: explore use of union of DescriptorImageInfo/DescriptorBufferInfo to tighten this up/simplify
	vk::Sampler        sampler       = nullptr;                                   // |
	vk::ImageView      imageView     = nullptr;                                   // | > keep in this order, so we can pass address for sampler as descriptorImageInfo
	vk::ImageLayout    imageLayout   = vk::ImageLayout::eShaderReadOnlyOptimal;   // |
	vk::DescriptorType type          = vk::DescriptorType::eUniformBufferDynamic; //
	vk::Buffer         buffer        = nullptr;                                   // |
	vk::DeviceSize     offset        = 0;                                         // | > keep in this order, as we can cast this to a DescriptorBufferInfo
	vk::DeviceSize     range         = VK_WHOLE_SIZE;                             // |
	uint32_t           bindingNumber = 0;                                         // <-- may be sparse, may repeat (for arrays of images bound to the same binding), but must increase monotonically (may only repeat or up over the series inside the samplerBindings vector).
	uint32_t           arrayIndex    = 0;                                         // <-- must be in sequence for array elements of same binding
};

struct AbstractPhysicalResource {
	enum Type : uint64_t {
		eUndefined = 0,
		eBuffer,
		eImage,
		eImageView,
		eSampler,
		eFramebuffer,
		eRenderPass,
	};
	union {
		uint64_t      asRawData;
		VkBuffer      asBuffer;
		VkImage       asImage;
		VkImageView   asImageView;
		VkSampler     asSampler;
		VkFramebuffer asFramebuffer;
		VkRenderPass  asRenderPass;
	};
	Type type;
};

struct AttachmentInfo {
	le_resource_handle_t    resource_id{};      ///< which resource to look up for resource state
	vk::Format              format;             ///
	vk::AttachmentLoadOp    loadOp;             ///
	vk::AttachmentStoreOp   storeOp;            ///
	vk::ClearValue          clearValue;         ///< either color or depth clear value, only used if loadOp is eClear
	vk::SampleCountFlagBits numSamples;         /// < number of samples, default 1
	uint32_t                initialStateOffset; ///< sync state of resource before entering the renderpass (offset is into resource specific sync chain )
	uint32_t                finalStateOffset;   ///< sync state of resource after exiting the renderpass (offset is into resource specific sync chain )
};

struct LeRenderPass {

	AttachmentInfo attachments[ 16 ];          // maximum of 16 color output attachments
	uint16_t       numColorAttachments;        // 0..16
	uint16_t       numDepthStencilAttachments; // 0..1

	LeRenderPassType type;

	vk::Framebuffer framebuffer;
	vk::RenderPass  renderPass;
	uint32_t        width;
	uint32_t        height;
	uint64_t        renderpassHash; ///< spooky hash of elements that could influence renderpass compatibility

	struct le_command_buffer_encoder_o *encoder;
};
