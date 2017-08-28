
#include <editor_plugin_api/editor_plugin_api.h>

#include <string>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF

#include <stb_image.h>
#include <stb_image_write.h>

namespace PLUGIN_NAMESPACE {

ConfigDataApi* config_data_api = nullptr;
EditorLoggingApi* logging_api = nullptr;
EditorEvalApi* eval_api = nullptr;
EditorAllocatorApi* allocator_api = nullptr;
EditorAllocator plugin_allocator = nullptr;

/**
 * Return plugin extension name.
 */
const char* get_name() { return "Editor Plugin Extension"; }

/**
 * Return plugin version.
 */
const char* get_version() { return "1.0.0"; }

/**
* Load GIF animations frames from the specified file path..
*/
STBIDEF unsigned char *gif_load_frames(char const *filename, int *x, int *y, int *frames)
{
	typedef struct gif_result_t {
		int delay;
		unsigned char *data;
		struct gif_result_t *next;
	} gif_result;

	FILE *f;
	stbi__context s;
	unsigned char *result;

	if (!((f = stbi__fopen(filename, "rb"))))
		return stbi__errpuc("can't fopen", "Unable to open file");

	stbi__start_file(&s, f);

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

	fclose(f);
	return result;
}

/**
* Extract GIF frames and save them to disk as PNG files.
*/
ConfigValue extract_frames(ConfigValueArgs args, int num)
{
	if (num != 1)
		return nullptr;
	auto file_path_cv = &args[0];
	std::string file_path = config_data_api->to_string(file_path_cv);

	int w, h, frames;
	auto frames_data = gif_load_frames(file_path.c_str(), &w, &h, &frames);

	// Create config data array to return all generated PNG file paths.
	auto result_file_paths = config_data_api->make(nullptr);
	config_data_api->set_array(result_file_paths, frames);

	unsigned int frame_size = 4 * w * h;
	for (int i = 0; i < frames; ++i) {
		char png_filename[256];
		auto frame_data = frames_data + i * (frame_size + 2);
		auto delay = (stbi__uint16)*(frame_data + frame_size);

		size_t dot_index = file_path.find_last_of(".");
		auto raw_name = file_path.substr(0, dot_index);

		sprintf(png_filename, "%s_%02d.png", raw_name.c_str(), i);
		auto image_written = stbi_write_png(png_filename, w, h, 4, frame_data, 0);
		if (!image_written)
			continue;

		char generation_log_info[1024];
		sprintf(generation_log_info, "Generated `%s` with frame delay %d", png_filename, delay);
		logging_api->info(generation_log_info);

		auto file_path_item = config_data_api->array_item(result_file_paths, i);
		config_data_api->set_string(file_path_item, png_filename);
	}

	return result_file_paths;
}

/**
 * Setup plugin resources and define client JavaScript APIs.
 */
void plugin_loaded(GetEditorApiFunction get_editor_api)
{
	auto api = static_cast<EditorApi*>(get_editor_api(EDITOR_API_ID));
	config_data_api = static_cast<ConfigDataApi*>(get_editor_api(CONFIGDATA_API_ID));
	logging_api = static_cast<EditorLoggingApi*>(get_editor_api(EDITOR_LOGGING_API_ID));
	eval_api = static_cast<EditorEvalApi*>(get_editor_api(EDITOR_EVAL_API_ID));

	api->register_native_function("nativeGiphy", "extractFrames", &extract_frames);
}

/**
* Release plugin resources and exposed APIs.
*/
void plugin_unloaded(GetEditorApiFunction get_editor_api)
{
	auto api = static_cast<EditorApi*>(get_editor_api(EDITOR_API_ID));
	api->unregister_native_function("nativeGiphy", "extractFrames");
}

} // end namespace

/**
 * Setup plugin APIs.
 */
extern "C" __declspec(dllexport) void *get_editor_plugin_api(unsigned api)
{
	if (api == EDITOR_PLUGIN_SYNC_API_ID) {
		static struct EditorPluginSyncApi editor_api = {nullptr};
		editor_api.get_name = &PLUGIN_NAMESPACE::get_name;
		editor_api.get_version = &PLUGIN_NAMESPACE::get_version;
		editor_api.plugin_loaded = &PLUGIN_NAMESPACE::plugin_loaded;
		editor_api.shutdown = &PLUGIN_NAMESPACE::plugin_unloaded;
		return &editor_api;
	}

	return nullptr;
}
