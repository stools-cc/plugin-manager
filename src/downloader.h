#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PLUGINS 32
#define MAX_NAME_LEN 128
#define MAX_VER_LEN 32
#define MAX_SLUG_LEN 64

struct plugin_info {
	char slug[MAX_SLUG_LEN];
	char name[MAX_NAME_LEN];
	char latest_version[MAX_VER_LEN];
	char installed_version[MAX_VER_LEN];
	int download_asset_id;
	bool installed;
	bool update_available;
};

struct plugin_list {
	struct plugin_info items[MAX_PLUGINS];
	int count;
};

bool downloader_fetch_plugin_list(const char *token, struct plugin_list *out);
bool downloader_install_plugin(const char *token, const char *slug,
			       int asset_id, const char *version,
			       const char *obs_plugin_dir);
bool downloader_uninstall_plugin(const char *slug, const char *obs_plugin_dir);
void downloader_detect_installed(struct plugin_list *list,
				 const char *obs_plugin_dir);
bool downloader_get_obs_plugin_dir(char *buf, size_t sz);
bool downloader_write_version_file(const char *obs_plugin_dir,
				   const char *slug, const char *version);
const char *downloader_last_error(void);

#ifdef __cplusplus
}
#endif
