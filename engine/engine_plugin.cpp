#include <engine_plugin_api/plugin_api.h>
#include <plugin_foundation/platform.h>
#include <plugin_foundation/id_string.h>
#include <plugin_foundation/string.h>
#include <plugin_foundation/allocator.h>

#if _DEBUG
	#include <stdlib.h>
	#include <time.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF

#include <stb_image.h>
#include <stb_image_write.h>

namespace PLUGIN_NAMESPACE {

using namespace stingray_plugin_foundation;

// Common constants
unsigned int INVALID_HANDLE = UINT32_MAX;

// Engine APIs
DataCompilerApi *data_compiler = nullptr;
DataCompileParametersApi * data_compile_params = nullptr;
AllocatorApi *allocator_api = nullptr;
AllocatorObject *allocator_object = nullptr;
ApiAllocator _allocator(nullptr, nullptr);
LoggingApi *log = nullptr;
ErrorApi *error = nullptr;
UnitApi* unit = nullptr;
ResourceManagerApi* resource_manager = nullptr;
RenderBufferApi* render_buffer = nullptr;

// C Scripting API
namespace stingray {
	struct UnitCApi* Unit = nullptr;
	struct MeshCApi* Mesh = nullptr;
	struct MaterialCApi* Material = nullptr;
	struct DynamicScriptDataCApi* Data = nullptr;
}

// Data compiler resource properties
int RESOURCE_VERSION = 1;
const char *RESOURCE_EXTENSION = "gif";
const IdString64 RESOURCE_ID = IdString64(RESOURCE_EXTENSION);

struct UnitGiphy
{
	// Used to reused released giphy slots
	bool used;

	// Used to find an existing giphy data.
	CApiUnit* unit_instance;

	// Gif image data
	unsigned width;
	unsigned height;
	unsigned char* gif_data;

	// Texture data
	unsigned texture_buffer_handle;

	// Playback data
	unsigned frame_count;
	unsigned current_frame;
	float next_frame_delay;
};

/**
* Hold all loaded Giphies.
*/
Array<UnitGiphy>* giphies = nullptr;

/**
 * Returns the plugin name.
 */
const char* get_name() { return "giphy_plugin"; }

/**
 * Clone the source resource data and append it's buffer size in front of the 
 * data result buffer.
 */
DataCompileResult pack_source_data_with_size(DataCompileParameters *input, const DataCompileResult& result_data)
{
	DataCompileResult result = { nullptr };
	unsigned length_with_data_size = result_data.data.len + sizeof(unsigned);
	result.data.p = (char*)allocator_api->allocate(data_compile_params->allocator(input), length_with_data_size, 4);
	result.data.len = length_with_data_size;
	memcpy(result.data.p, &result_data.data.len, sizeof(unsigned));
	memcpy(result.data.p + sizeof(unsigned), result_data.data.p, result_data.data.len);
	return result;
}

/**
* Define plugin resource compiler.
*/
DataCompileResult gif_compiler(DataCompileParameters *input)
{
	auto source_data = data_compile_params->read(input);
	if (source_data.error)
		return source_data;
	return pack_source_data_with_size(input, source_data);
}

/**
 * Define plugin resource compiler.
 */
DataCompileResult my_resource_compiler(DataCompileParameters *input)
{
	auto source_data = data_compile_params->read(input);
	if (source_data.error)
		return source_data;
	return pack_source_data_with_size(input, source_data);
}

/**
 * Setup runtime and compiler common resources, such as allocators.
 */
void setup_common_api(GetApiFunction get_engine_api)
{
	allocator_api = (AllocatorApi*)get_engine_api(ALLOCATOR_API_ID);
	if (allocator_object == nullptr) {
		allocator_object = allocator_api->make_plugin_allocator(get_name());
		_allocator = ApiAllocator(allocator_api, allocator_object);
	}

	log = (LoggingApi*)get_engine_api(LOGGING_API_ID);
	error = (ErrorApi*)get_engine_api(ERROR_API_ID);
	resource_manager = (ResourceManagerApi*)get_engine_api(RESOURCE_MANAGER_API_ID);
}

/**
* Load all GIF animations from a memory buffer.
*/
STBIDEF unsigned char *gif_load_frames(stbi_uc const *buffer, int len, int *x, int *y, int *frames)
{
	typedef struct gif_result_t {
		int delay;
		unsigned char *data;
		struct gif_result_t *next;
	} gif_result;

	stbi__context s;
	unsigned char *result;

	stbi__start_mem(&s, buffer, len);

	if (stbi__gif_test(&s)) {
		int c;
		stbi__gif g;
		gif_result head;
		gif_result *prev = nullptr, *gr = &head;

		memset(&g, 0, sizeof(g));
		memset(&head, 0, sizeof(head));

		*frames = 0;

		while ((gr->data = stbi__gif_load_next(&s, &g, &c, 4))) {
			if (gr->data == (unsigned char*)&s) {
				gr->data = nullptr;
				break;
			}

			if (prev) prev->next = gr;
			gr->delay = g.delay;
			prev = gr;
			gr = (gif_result*)stbi__malloc(sizeof(gif_result));
			memset(gr, 0, sizeof(gif_result));
			++(*frames);
		}

		STBI_FREE(g.out);

		if (gr != &head)
			STBI_FREE(gr);

		if (*frames > 0) {
			*x = g.w;
			*y = g.h;
		}

		result = head.data;

		if (*frames > 1) {
			unsigned int size = 4 * g.w * g.h;
			unsigned char *p;

			result = (unsigned char*)stbi__malloc(*frames * (size + 2));
			gr = &head;
			p = result;

			while (gr) {
				prev = gr;
				memcpy(p, gr->data, size);
				p += size;
				*p++ = gr->delay & 0xFF;
				*p++ = (gr->delay & 0xFF00) >> 8;
				gr = gr->next;

				STBI_FREE(prev->data);
				if (prev != &head) STBI_FREE(prev);
			}
		}
	} else {
		stbi__result_info result_info;
		result = (unsigned char*)stbi__load_main(&s, x, y, frames, 4, &result_info, 0);
		*frames = !!result;
	}

	return result;
}

/**
* Release giphy data and mark slot as unused.
*/
void release_giphy(UnitGiphy& giphy)
{
	// Mark this slot as unused, so reusable.
	giphy.used = false;

	// Dispose of GIF animation image data.
	STBI_FREE(giphy.gif_data);

	// Release the texture buffer resource.
	render_buffer->destroy_buffer(giphy.texture_buffer_handle);
	giphy.texture_buffer_handle = INVALID_HANDLE;
}

/**
 * Setup plugin runtime resources.
 */
void setup_plugin(GetApiFunction get_engine_api)
{
	setup_common_api(get_engine_api);

	unit = (UnitApi*)get_engine_api(UNIT_API_ID);
	render_buffer = (RenderBufferApi*)get_engine_api(RENDER_BUFFER_API_ID);
	auto c_api = (ScriptApi*)get_engine_api(C_API_ID);
	stingray::Unit = c_api->Unit;
	stingray::Mesh = c_api->Mesh;
	stingray::Material = c_api->Material;
	stingray::Data = c_api->DynamicScriptData;

	giphies = MAKE_NEW(_allocator, Array<UnitGiphy>, _allocator);
}

/**
 * Setup plugin compiler resources.
 */
void setup_data_compiler(GetApiFunction get_engine_api)
{
	setup_common_api(get_engine_api);

	#if _DEBUG
		// Always trigger the resource compiler in debug mode.
		srand(time(nullptr));
		RESOURCE_VERSION = rand();
	#endif

	data_compiler = (DataCompilerApi*)get_engine_api(DATA_COMPILER_API_ID);
	data_compile_params = (DataCompileParametersApi*)get_engine_api(DATA_COMPILE_PARAMETERS_API_ID);
	data_compiler->add_compiler(RESOURCE_EXTENSION, RESOURCE_VERSION, my_resource_compiler);
}

/**
 * Indicate to the resource manager that we'll be using our plugin resource type.
 */
void setup_resources(GetApiFunction get_engine_api)
{
	setup_common_api(get_engine_api);
	resource_manager->register_type(RESOURCE_EXTENSION);
}

/**
 * Called per game frame.
 */
void update_plugin(float dt)
{
	for (unsigned g = 0; g < giphies->size(); ++g) {
		auto& ug = (*giphies)[g];

		// Skip unit that do not have a Giphy
		if (!ug.used)
			continue;

		// Update frame delay
		ug.next_frame_delay -= dt;

		// Play next frame if the delay was reached.
		if (ug.next_frame_delay <= 0.0f) {
			ug.current_frame = (ug.current_frame + 1) % ug.frame_count;
			const auto frame_size = ug.width * ug.height * 4;
			auto next_frame_data = ug.gif_data + ug.current_frame * (frame_size + 2); // + 2 for delay info
			render_buffer->update_buffer(ug.texture_buffer_handle, frame_size, next_frame_data);

			auto delay = (stbi__uint16)*(next_frame_data + frame_size);
			ug.next_frame_delay = delay / 100.0f;
		}
	}
}

/**
 * Release plugin resources.
 */
void shutdown_plugin()
{
	if (giphies) {
		for (unsigned i = 0; i < giphies->size(); ++i) {
			auto& ug = (*giphies)[i];
			if (!ug.used)
				continue;
			release_giphy(ug);
		}
		MAKE_DELETE(_allocator, giphies);
	}

	if (allocator_object != nullptr) {
		XENSURE(_allocator.api());
		_allocator = ApiAllocator(nullptr, nullptr);
		allocator_api->destroy_plugin_allocator(allocator_object);
		allocator_object = nullptr;
	}
}

/**
* Searches for a unit's giphy.
*/
UnitGiphy* find_giphy(CApiUnit* unit)
{
	for (unsigned g = 0; g < giphies->size(); ++g) {
		auto& ug = (*giphies)[g];
		if (ug.used && ug.unit_instance == unit)
			return &ug;
	}
	return nullptr;
}

/**
* When new units spawn, we check if they have a giphy resource assigned and
* update their respective mesh material.
*/
void units_spawned(CApiUnit **units, unsigned count)
{
	#if _DEBUG
	#define LOG_AND_CONTINUE(msg, ...) { log->warning(RESOURCE_EXTENSION, error->eprintf(msg, ##__VA_ARGS__)); continue; }
	#else
	#define LOG_AND_CONTINUE(msg, ...) { continue;  }
	#endif

	for (unsigned i = 0; i < count; ++i) {
		auto unit_ref = unit->reference(units[i]);

		#if _DEBUG
		auto unit_resource_name = unit->unit_resource_name(units[i]);
		#endif

		// Define script data field name to get.
		const auto giphy_resource_indice = "giphy_resource";
		const auto mesh_index_indice = "giphy_mesh_index";
		const auto material_slot_name_indice = "giphy_material_slot_name";

		// Do not continue if this unit does not have any Giphy resource.
		if (!stingray::Data->Unit->has_data(unit_ref, 1, giphy_resource_indice))
			continue;

		// Make sure the unit has all the data we need to display a Giphy on it.
		if (stingray::Unit->num_meshes(unit_ref) == 0 ||
			!stingray::Data->Unit->has_data(unit_ref, 1, mesh_index_indice) ||
			!stingray::Data->Unit->has_data(unit_ref, 1, material_slot_name_indice))
			LOG_AND_CONTINUE("Unit #ID[%016llx] is missing Giphy property script data.", unit_resource_name);

		// Get script data values
		auto giphy_mesh_index = (unsigned)*(float*)stingray::Data->Unit->get_data(unit_ref, 1, mesh_index_indice).pointer;
		auto giphy_resource_name = (const char*)stingray::Data->Unit->get_data(unit_ref, 1, giphy_resource_indice).pointer;
		auto giphy_material_slot_name = (const char*)stingray::Data->Unit->get_data(unit_ref, 1, material_slot_name_indice).pointer;

		// We need a valid material slot name
		if (strlen(giphy_material_slot_name) == 0)
			LOG_AND_CONTINUE("Unit #ID[%016llx] has an invalid material slot name", unit_resource_name);

		// Make sure we can load the Giphy resource.
		if (!resource_manager->can_get(RESOURCE_EXTENSION, giphy_resource_name))
			LOG_AND_CONTINUE("Cannot get unit #ID[%016llx] giphy resource", unit_resource_name);

		// Get the unit mesh reference on which to display the Giphy.
		auto unit_mesh = stingray::Unit->mesh(unit_ref, giphy_mesh_index, nullptr);
		if (stingray::Mesh->num_materials(unit_mesh) == 0)
			LOG_AND_CONTINUE("Unit #ID[%016llx] has no material", unit_resource_name);

		// Get compiled GIF resource data and length.
		unsigned gif_data_len = 0;
		unsigned char* gif_resource_data = (unsigned char*)resource_manager->get(RESOURCE_EXTENSION, giphy_resource_name);
		memcpy(&gif_data_len, gif_resource_data, sizeof(gif_data_len));
		gif_resource_data += sizeof(gif_data_len);

		// Load GIF image data.
		int width = 0, height = 0, frames = 0;
		auto gif_frames_data = gif_load_frames((stbi_uc*)gif_resource_data, gif_data_len, &width, &height, &frames);
		if (gif_frames_data == nullptr)
			LOG_AND_CONTINUE("Cannot parse unit #ID[%016llx] giphy resource data", unit_resource_name);

		// Create texture buffer view
		RB_TextureBufferView texture_buffer_view;
		memset(&texture_buffer_view, 0, sizeof(texture_buffer_view));
		texture_buffer_view.width = width;
		texture_buffer_view.height = height;
		texture_buffer_view.depth = 1;
		texture_buffer_view.mip_levels = 1;
		texture_buffer_view.slices = 1;
		texture_buffer_view.type = RB_TEXTURE_TYPE_2D;
		texture_buffer_view.format = render_buffer->format(RB_INTEGER_COMPONENT, false, true, 8, 8, 8, 8); // ImageFormat::PF_R8G8B8A8;

																										   // Create and initialize texture buffer with first GIF frame.
		auto frame_size = width * height * 4;
		auto texture_buffer_handle = render_buffer->create_buffer(frame_size, RB_VALIDITY_UPDATABLE, RB_TEXTURE_BUFFER_VIEW, &texture_buffer_view, gif_frames_data);
		auto texture_buffer = render_buffer->lookup_resource(texture_buffer_handle);

		// Update the mesh material with the newly created texture buffer resource.
		auto mesh_mat = stingray::Mesh->material(unit_mesh, 0);
		auto material_slot_id = IdString32(giphy_material_slot_name).id();
		stingray::Material->set_resource(mesh_mat, material_slot_id, texture_buffer);

		// Associate and track the Giphy data for this unit.
		UnitGiphy ug;
		ug.used = true;
		ug.unit_instance = units[i];
		ug.gif_data = gif_frames_data;
		ug.width = width;
		ug.height = height;
		ug.texture_buffer_handle = texture_buffer_handle;

		// Initialize playback data.
		auto delay = (stbi__uint16)*(gif_frames_data + frame_size);
		ug.current_frame = 0;
		ug.frame_count = frames;
		ug.next_frame_delay = delay / 100.0f;

		// Find an unused giphy slot.
		bool reused = false;
		for (unsigned g = 0; g < giphies->size(); ++g) {
			if ((*giphies)[g].used)
				continue;
			(*giphies)[g] = ug;
			reused = true;
			break;
		}
		if (!reused)
			giphies->push_back(ug);
	}
}

/**
* When units gets unspawned, lets if we have a associated giphy, if yes,
* lets release it.
*/
void units_unspawned(CApiUnit **units, unsigned count)
{
	for (unsigned i = 0; i < count; ++i) {
		auto unit = units[i];
		auto unit_giphy = find_giphy(unit);
		if (unit_giphy)
			release_giphy(*unit_giphy);
	}
}

}

extern "C" {

	/**
	 * Load and define plugin APIs.
	 */
	PLUGIN_DLLEXPORT void *get_plugin_api(unsigned api)
	{
		using namespace PLUGIN_NAMESPACE;

		if (api == PLUGIN_API_ID) {
			static PluginApi plugin_api = { nullptr };
			plugin_api.get_name = get_name;
			plugin_api.setup_game = setup_plugin;
			plugin_api.update_game = update_plugin;
			plugin_api.setup_resources = setup_resources;
			plugin_api.shutdown_game = shutdown_plugin;
			plugin_api.setup_data_compiler = setup_data_compiler;
			plugin_api.shutdown_data_compiler = shutdown_plugin;

			plugin_api.units_spawned = units_spawned;
			plugin_api.units_unspawned = units_unspawned;

			return &plugin_api;
		}
		return nullptr;
	}

}
