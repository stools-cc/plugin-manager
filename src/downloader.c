#include "downloader.h"
#include "obfuscation.h"
#include "debug-log.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <unistd.h>
#define PATH_SEP '/'
#endif

/* ---- cURL helpers ---- */

struct mem_buf {
	char *data;
	size_t size;
};

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t total = size * nmemb;
	struct mem_buf *buf = (struct mem_buf *)userp;
	char *tmp = (char *)realloc(buf->data, buf->size + total + 1);
	if (!tmp) return 0;
	buf->data = tmp;
	memcpy(buf->data + buf->size, contents, total);
	buf->size += total;
	buf->data[buf->size] = '\0';
	return total;
}

static size_t write_file_cb(void *contents, size_t size, size_t nmemb,
			    void *userp)
{
	FILE *f = (FILE *)userp;
	return fwrite(contents, size, nmemb, f);
}

static void curl_set_ssl_opts(CURL *curl)
{
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#ifdef _WIN32
	curl_easy_setopt(curl, CURLOPT_SSLVERSION,
			 CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NO_REVOKE);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 0L);
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
#endif
}

static char *api_get(const char *path, const char *token)
{
	CURL *curl = curl_easy_init();
	if (!curl) return NULL;

	char url[512];
	snprintf(url, sizeof(url), "%s%s%s",
		 obf_https_prefix(), obf_stools_host(), path);

	struct curl_slist *headers = NULL;
	char auth_header[512];
	snprintf(auth_header, sizeof(auth_header), obf_auth_bearer_fmt(), token);
	headers = curl_slist_append(headers, auth_header);

	char ua[128];
	snprintf(ua, sizeof(ua), "%s%s", obf_ua_prefix(), PLUGIN_VERSION);

	struct mem_buf buf = {NULL, 0};

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
	curl_set_ssl_opts(curl);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK || http_code != 200) {
		dbg_log(LOG_WARNING, "[%s] API GET %s: %s (HTTP %ld)",
			PLUGIN_NAME, path,
			res != CURLE_OK ? curl_easy_strerror(res) : "error",
			http_code);
		free(buf.data);
		return NULL;
	}

	return buf.data;
}

/* ---- JSON helpers ---- */

static const char *json_find_key(const char *json, const char *key)
{
	char search[256];
	snprintf(search, sizeof(search), "\"%s\"", key);
	const char *pos = strstr(json, search);
	if (!pos) return NULL;
	pos += strlen(search);
	while (*pos == ' ' || *pos == ':') pos++;
	return pos;
}

static void json_extract_string(const char *json, const char *key,
				char *out, size_t out_sz)
{
	out[0] = '\0';
	const char *v = json_find_key(json, key);
	if (!v || *v != '"') return;
	v++;
	const char *end = strchr(v, '"');
	if (!end) return;
	size_t len = (size_t)(end - v);
	if (len >= out_sz) len = out_sz - 1;
	memcpy(out, v, len);
	out[len] = '\0';
}

/* ---- OBS plugin directory ---- */

bool downloader_get_obs_plugin_dir(char *buf, size_t sz)
{
#ifdef _WIN32
	char appdata[MAX_PATH];
	SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata);
	snprintf(buf, sz, "%s\\obs-studio\\obs-plugins\\64bit", appdata);
	CreateDirectoryA(buf, NULL);
	return true;
#elif defined(__APPLE__)
	const char *home = getenv("HOME");
	if (!home) return false;
	snprintf(buf, sz, "%s/Library/Application Support/obs-studio/obs-plugins",
		 home);
	mkdir(buf, 0755);
	return true;
#else
	const char *home = getenv("HOME");
	if (!home) return false;
	snprintf(buf, sz, "%s/.config/obs-studio/plugins", home);
	mkdir(buf, 0755);
	return true;
#endif
}

/* ---- Parse plugin list from API ---- */

bool downloader_fetch_plugin_list(const char *token, struct plugin_list *out)
{
	memset(out, 0, sizeof(*out));

	char *json = api_get(obf_api_plugins_path(), token);
	if (!json) return false;

	/*
	 * Expected JSON format:
	 * [{"slug":"easy-irl-stream","name":"Easy IRL Stream",
	 *   "version":"1.1.4","platform":{"windows":".dll","linux":".so"}}, ...]
	 */
	const char *pos = json;
	while (out->count < MAX_PLUGINS) {
		const char *obj_start = strchr(pos, '{');
		if (!obj_start) break;

		const char *obj_end = strchr(obj_start, '}');
		if (!obj_end) break;

		size_t obj_len = (size_t)(obj_end - obj_start + 1);
		char obj_buf[2048];
		if (obj_len >= sizeof(obj_buf)) {
			pos = obj_end + 1;
			continue;
		}
		memcpy(obj_buf, obj_start, obj_len);
		obj_buf[obj_len] = '\0';

		struct plugin_info *pi = &out->items[out->count];
		json_extract_string(obj_buf, "slug", pi->slug, sizeof(pi->slug));
		json_extract_string(obj_buf, "name", pi->name, sizeof(pi->name));
		json_extract_string(obj_buf, "version", pi->latest_version,
				    sizeof(pi->latest_version));

		if (pi->slug[0] && pi->name[0])
			out->count++;

		pos = obj_end + 1;
	}

	free(json);
	dbg_log(LOG_INFO, "[%s] Fetched %d plugins", PLUGIN_NAME, out->count);
	return out->count > 0;
}

/* ---- Detect installed plugins ---- */

static bool file_exists(const char *path)
{
#ifdef _WIN32
	DWORD attrs = GetFileAttributesA(path);
	return attrs != INVALID_FILE_ATTRIBUTES;
#else
	return access(path, F_OK) == 0;
#endif
}

static bool read_version_file(const char *dir, const char *slug,
			      char *ver, size_t ver_sz)
{
	char path[512];
	snprintf(path, sizeof(path), "%s%c%s.version", dir, PATH_SEP, slug);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	if (!fgets(ver, (int)ver_sz, f)) {
		fclose(f);
		return false;
	}
	fclose(f);
	size_t len = strlen(ver);
	while (len > 0 && (ver[len - 1] == '\n' || ver[len - 1] == '\r'))
		ver[--len] = '\0';
	return len > 0;
}

static int compare_versions(const char *a, const char *b)
{
	int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
	sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
	sscanf(b, "%d.%d.%d", &b1, &b2, &b3);
	if (a1 != b1) return a1 - b1;
	if (a2 != b2) return a2 - b2;
	return a3 - b3;
}

void downloader_detect_installed(struct plugin_list *list,
				 const char *obs_plugin_dir)
{
	for (int i = 0; i < list->count; i++) {
		struct plugin_info *pi = &list->items[i];

		char dll_path[512];
#ifdef _WIN32
		snprintf(dll_path, sizeof(dll_path), "%s\\%s.dll",
			 obs_plugin_dir, pi->slug);
#elif defined(__APPLE__)
		snprintf(dll_path, sizeof(dll_path), "%s/%s.so",
			 obs_plugin_dir, pi->slug);
#else
		snprintf(dll_path, sizeof(dll_path), "%s/%s.so",
			 obs_plugin_dir, pi->slug);
#endif

		pi->installed = file_exists(dll_path);

		if (pi->installed) {
			if (read_version_file(obs_plugin_dir, pi->slug,
					      pi->installed_version,
					      sizeof(pi->installed_version))) {
				pi->update_available =
					compare_versions(pi->latest_version,
							 pi->installed_version) > 0;
			} else {
				snprintf(pi->installed_version,
					 sizeof(pi->installed_version), "?");
				pi->update_available = true;
			}
		}
	}
}

/* ---- Download and install a plugin ---- */

bool downloader_install_plugin(const char *token, const char *slug,
			       const char *obs_plugin_dir)
{
	const char *platform =
#ifdef _WIN32
		"windows";
#elif defined(__APPLE__)
		"macos";
#else
		"linux";
#endif

	char path[512];
	snprintf(path, sizeof(path), obf_api_plugin_download_fmt(),
		 slug, platform);

	char url[512];
	snprintf(url, sizeof(url), "%s%s%s",
		 obf_https_prefix(), obf_stools_host(), path);

	const char *ext =
#ifdef _WIN32
		".dll";
#else
		".so";
#endif

	char tmp_path[512], final_path[512], ver_path[512];
	snprintf(tmp_path, sizeof(tmp_path), "%s%c%s%s.tmp",
		 obs_plugin_dir, PATH_SEP, slug, ext);
	snprintf(final_path, sizeof(final_path), "%s%c%s%s",
		 obs_plugin_dir, PATH_SEP, slug, ext);
	snprintf(ver_path, sizeof(ver_path), "%s%c%s.version",
		 obs_plugin_dir, PATH_SEP, slug);

	FILE *f = fopen(tmp_path, "wb");
	if (!f) {
		dbg_log(LOG_ERROR, "[%s] Cannot open %s for writing",
			PLUGIN_NAME, tmp_path);
		return false;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fclose(f);
		remove(tmp_path);
		return false;
	}

	struct curl_slist *headers = NULL;
	char auth_header[512];
	snprintf(auth_header, sizeof(auth_header), obf_auth_bearer_fmt(), token);
	headers = curl_slist_append(headers, auth_header);

	char ua[128];
	snprintf(ua, sizeof(ua), "%s%s", obf_ua_prefix(), PLUGIN_VERSION);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_set_ssl_opts(curl);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	fclose(f);

	if (res != CURLE_OK || http_code != 200) {
		dbg_log(LOG_ERROR, "[%s] Download %s failed: %s (HTTP %ld)",
			PLUGIN_NAME, slug, curl_easy_strerror(res), http_code);
		remove(tmp_path);
		return false;
	}

	/* Atomic replace: delete old, rename tmp */
	remove(final_path);
	if (rename(tmp_path, final_path) != 0) {
		dbg_log(LOG_ERROR, "[%s] Failed to rename %s -> %s",
			PLUGIN_NAME, tmp_path, final_path);
		remove(tmp_path);
		return false;
	}

	/* Write version file from server response header or plugin list */
	dbg_log(LOG_INFO, "[%s] Installed %s to %s", PLUGIN_NAME, slug,
		final_path);
	return true;
}
