#include <obs-module.h>
#include <obs-frontend-api.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

#include "compat.h"
#include "debug-log.h"
#include "auth.h"
#include "downloader.h"
#include "manager-dialog.hpp"

OBS_DECLARE_MODULE()

#define PLUGIN_NAME "stools Plugin Manager"

static const char *g_locale = NULL;

/* ---- Background init thread ---- */

static pthread_t g_init_thread;
static volatile bool g_init_done = false;

static void *init_thread_func(void *arg)
{
	(void)arg;

	os_set_thread_name("stools-pm-init");

	auth_init();

	if (auth_is_logged_in()) {
		struct plugin_list plugins = {0};
		if (downloader_fetch_plugin_list(auth_get_token(), &plugins)) {
			char obs_dir[512];
			if (downloader_get_obs_plugin_dir(obs_dir,
							  sizeof(obs_dir))) {
				downloader_detect_installed(&plugins, obs_dir);

				for (int i = 0; i < plugins.count; i++) {
					struct plugin_info *pi =
						&plugins.items[i];
					if (!pi->installed ||
					    pi->update_available) {
						dbg_log(LOG_INFO,
							"[%s] Auto-installing %s v%s",
							PLUGIN_NAME, pi->slug,
							pi->latest_version);
						if (downloader_install_plugin(
							    auth_get_token(),
							    pi->slug,
							    obs_dir)) {
							char vp[512];
							snprintf(vp,
								 sizeof(vp),
								 "%s"
#ifdef _WIN32
								 "\\"
#else
								 "/"
#endif
								 "%s.version",
								 obs_dir,
								 pi->slug);
							FILE *f = fopen(vp,
									"w");
							if (f) {
								fputs(pi->latest_version,
								      f);
								fclose(f);
							}
						}
					}
				}
			}
		}
	}

	g_init_done = true;
	return NULL;
}

/* ---- Tools menu ---- */

static void tools_menu_cb(void *private_data)
{
	(void)private_data;
	manager_dialog_show(g_locale);
}

/* ---- Module lifecycle ---- */

bool obs_module_load(void)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);

	g_locale = obs_get_locale();

	if (pthread_create(&g_init_thread, NULL, init_thread_func, NULL) != 0) {
		dbg_log(LOG_ERROR, "[%s] Failed to create init thread",
			PLUGIN_NAME);
	}

	dbg_log(LOG_INFO, "[%s] Plugin loaded (v%s)", PLUGIN_NAME,
		PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	bool de = g_locale && g_locale[0] == 'd' && g_locale[1] == 'e';
	obs_frontend_add_tools_menu_item(
		de ? "stools Plugin Manager" : "stools Plugin Manager",
		tools_menu_cb, NULL);
}

void obs_module_unload(void)
{
	if (!g_init_done) {
		pthread_join(g_init_thread, NULL);
	}
	auth_shutdown();
	curl_global_cleanup();
	dbg_log(LOG_INFO, "[%s] Plugin unloaded", PLUGIN_NAME);
}
