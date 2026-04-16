#include <obs-module.h>
#ifdef MW_ENABLE_FRONTEND
#include <obs-frontend-api.h>
#endif
#include "mp3-writer-filter.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-mp3-writer", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Audio Stem Exporter for OBS — record any audio source directly "
	       "to MP3, WAV, or AIFF in a real-time background thread. "
	       "Perfect for DJ mixes, stems, and multi-source sessions.";
}

bool obs_module_load(void)
{
	mp3_writer_filter_register();
	blog(LOG_INFO, "[obs-mp3-writer] Plugin loaded (v1.0.0)");
	return true;
}

void obs_module_unload(void)
{
#if defined(MW_ENABLE_QT) && defined(MW_ENABLE_FRONTEND)
	obs_frontend_remove_dock("obs-mp3-writer-dock");
#endif
	blog(LOG_INFO, "[obs-mp3-writer] Plugin unloaded");
}
