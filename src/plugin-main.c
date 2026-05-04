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

static char g_update_msg[1024] = "";

static void show_update_alert(void *param)
{
	(void)param;
	if (g_update_msg[0])
		manager_dialog_show(g_locale);
}

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

				int updates = 0;
				for (int i = 0; i < plugins.count; i++) {
					struct plugin_info *pi =
						&plugins.items[i];
					if (pi->installed &&
					    pi->update_available) {
						updates++;
						dbg_log(LOG_INFO,
							"[%s] Update available: %s %s -> %s",
							PLUGIN_NAME, pi->name,
							pi->installed_version,
							pi->latest_version);
					}
				}

				if (updates > 0) {
					bool de = g_locale &&
						  g_locale[0] == 'd' &&
						  g_locale[1] == 'e';
					if (updates == 1)
						snprintf(g_update_msg,
							 sizeof(g_update_msg),
							 de ? "1 Plugin-Update verfügbar."
							    : "1 plugin update available.");
					else
						snprintf(g_update_msg,
							 sizeof(g_update_msg),
							 de ? "%d Plugin-Updates verfügbar."
							    : "%d plugin updates available.",
							 updates);

					obs_queue_task(OBS_TASK_UI,
						       show_update_alert,
						       NULL, false);
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
