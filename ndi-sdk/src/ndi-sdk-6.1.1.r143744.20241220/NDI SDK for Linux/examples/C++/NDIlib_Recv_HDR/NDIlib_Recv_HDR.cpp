#include <csignal>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <string>

#include "../ThirdParty/rapidxml.hpp"

#ifdef _WIN32
#include <windows.h>

#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64

#endif

#include <Processing.NDI.Lib.h>

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
	exit_loop = true;
}

int main(int argc, char* argv[])
{
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize()) {
		// Cannot run NDI. Most likely because the CPU is not sufficient (see SDK documentation).
		// you can check this directly with a call to NDIlib_is_supported_CPU()
		printf("Cannot run NDI.");
		return 0;
	}

	// Catch interrupt so that we can shut down gracefully
	signal(SIGINT, sigint_handler);

	// Create a finder
	const NDIlib_find_create_t NDI_find_create_desc; /* Default settings */
	NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2(&NDI_find_create_desc);
	if (!pNDI_find)
		return 0;

	// We wait until there is at least one HDR source on the network
	uint32_t no_sources = 0;
	const NDIlib_source_t* p_sources = nullptr;
	const NDIlib_source_t* p_hdr_source = nullptr;
	while (!p_hdr_source) {
		// Wait until the sources on the network have changed
		printf("Looking for HDR sources ...\n");
		NDIlib_find_wait_for_sources(pNDI_find, 1000);
		p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

		for (size_t i = 0; i < no_sources; i++) {
			// Note this is just looking for a source with 'HDR' in the name
			if (strstr(p_sources[i].p_ndi_name, "HDR")) {
				p_hdr_source = &p_sources[i];
				break;
			}
		}
	}

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_create_v3_t NDI_recv_create_desc;
	NDI_recv_create_desc.p_ndi_recv_name = "Example HDR Receiver";
	NDI_recv_create_desc.source_to_connect_to = *p_hdr_source;
	NDI_recv_create_desc.color_format = NDIlib_recv_color_format_best;

	// Create the receiver
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&NDI_recv_create_desc);
	if (!pNDI_recv) {
		NDIlib_find_destroy(pNDI_find);
		return 0;
	}

	// Destroy the NDI finder. We needed to have access to the pointers to p_hdr_source
	NDIlib_find_destroy(pNDI_find);

	// The descriptors
	NDIlib_video_frame_v2_t video_frame;
	NDIlib_audio_frame_v2_t audio_frame;

	// Run for one minute
	const auto start = std::chrono::high_resolution_clock::now();
	while (!exit_loop && std::chrono::high_resolution_clock::now() - start < std::chrono::minutes(1)) {
		switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, &audio_frame, nullptr, 1000)) {
			// No data
			case NDIlib_frame_type_none:
				printf("No data received.\n");
				break;

			// Video data
			case NDIlib_frame_type_video:
				printf("Video data received (%dx%d).\n", video_frame.xres, video_frame.yres);
				if (video_frame.p_metadata) {
					// Parsing XML metadata
					rapidxml::xml_document<> doc;
					doc.parse<0>((char*)video_frame.p_metadata);

					// Get root node
					rapidxml::xml_node<>* root = doc.first_node("ndi_color_info");

					// Get attributes
					rapidxml::xml_attribute<>* attr_transfer = root->first_attribute("transfer");
					rapidxml::xml_attribute<>* attr_matrix = root->first_attribute("matrix");
					rapidxml::xml_attribute<>* attr_primaries = root->first_attribute("primaries");

					// Display color information
					printf("Video metadata color info (transfer: %s, matrix: %s, primaries: %s)\n", attr_transfer->value(), attr_matrix->value(), attr_primaries->value());
				}
				if (video_frame.p_data) {
					// Video frame buffer.
				}
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
				break;

			// Audio data
			case NDIlib_frame_type_audio:
				printf("Audio data received (%d samples).\n", audio_frame.no_samples);
				NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
				break;

			// Everything else
			default:
				break;
		}
	}

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}

