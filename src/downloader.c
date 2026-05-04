#include "downloader.h"
#include "obfuscation.h"
#include "debug-log.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <unistd.h>
#define PATH_SEP '/'
#endif

/* ---- Error reporting ---- */

static char s_last_error[512] = "";

static void set_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s_last_error, sizeof(s_last_error), fmt, ap);
	va_end(ap);
}

const char *downloader_last_error(void)
{
	return s_last_error;
}

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
	if (token && token[0]) {
		char auth_header[512];
		snprintf(auth_header, sizeof(auth_header),
			 obf_auth_bearer_fmt(), token);
		headers = curl_slist_append(headers, auth_header);
	}

	char ua[128];
	snprintf(ua, sizeof(ua), "%s%s", obf_ua_prefix(), PLUGIN_VERSION);

	struct mem_buf buf = {NULL, 0};

	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (headers)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
	curl_set_ssl_opts(curl);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	if (headers)
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

static int json_extract_int(const char *json, const char *key)
{
	const char *v = json_find_key(json, key);
	if (!v) return -1;
	return atoi(v);
}

/* ---- OBS plugin directory ---- */

static void ensure_dir_exists(const char *path)
{
#ifdef _WIN32
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 3; *p; p++) {
		if (*p == '\\' || *p == '/') {
			*p = '\0';
			CreateDirectoryA(tmp, NULL);
			*p = '\\';
		}
	}
	CreateDirectoryA(tmp, NULL);
#else
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
	system(cmd);
#endif
}

bool downloader_get_obs_plugin_dir(char *buf, size_t sz)
{
#ifdef _WIN32
	char exe_path[MAX_PATH];
	GetModuleFileNameA(NULL, exe_path, MAX_PATH);

	/* Go up from 64bit to obs-studio root */
	char *last_sep = strrchr(exe_path, '\\');
	if (last_sep) *last_sep = '\0'; /* remove obs64.exe */
	last_sep = strrchr(exe_path, '\\');
	if (last_sep) *last_sep = '\0'; /* remove 64bit */
	last_sep = strrchr(exe_path, '\\');
	if (last_sep) *last_sep = '\0'; /* remove bin */

	snprintf(buf, sz, "%s\\obs-plugins\\64bit", exe_path);
	ensure_dir_exists(buf);
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

/* ---- Parse plugin list from /api/products ---- */

static const char *platform_suffix(void)
{
#ifdef _WIN32
	return "windows";
#elif defined(__APPLE__)
	return "macos";
#else
	return "linux";
#endif
}

bool downloader_fetch_plugin_list(const char *token, struct plugin_list *out)
{
	memset(out, 0, sizeof(*out));

	/* /api/products is public, but send token if available */
	char *json = api_get(obf_api_products_path(), token);
	if (!json) return false;

	/*
	 * Response: array of product objects
	 * [{"id":"easy-irl-stream","name":"Easy IRL Stream",
	 *   "type":"download","accessLevel":"free",...}, ...]
	 *
	 * We only show products of type "download"
	 */
	const char *pos = json;
	while (out->count < MAX_PLUGINS) {
		const char *obj_start = strchr(pos, '{');
		if (!obj_start) break;

		/* Find matching closing brace (simplified: first } works for
		 * flat objects; nested JSON could break this but products are flat) */
		int depth = 0;
		const char *p = obj_start;
		const char *obj_end = NULL;
		while (*p) {
			if (*p == '{') depth++;
			else if (*p == '}') {
				depth--;
				if (depth == 0) { obj_end = p; break; }
			}
			p++;
		}
		if (!obj_end) break;

		size_t obj_len = (size_t)(obj_end - obj_start + 1);
		char *obj_buf = (char *)malloc(obj_len + 1);
		if (!obj_buf) break;
		memcpy(obj_buf, obj_start, obj_len);
		obj_buf[obj_len] = '\0';

		char type[64] = "";
		json_extract_string(obj_buf, "type", type, sizeof(type));

		/* Only include downloadable plugins */
		if (strcmp(type, "download") == 0) {
			struct plugin_info *pi = &out->items[out->count];
			json_extract_string(obj_buf, "id", pi->slug,
					    sizeof(pi->slug));
			json_extract_string(obj_buf, "name", pi->name,
					    sizeof(pi->name));

			if (pi->slug[0] && pi->name[0])
				out->count++;
		}

		free(obj_buf);
		pos = obj_end + 1;
	}

	free(json);

	/* Now fetch latest version for each plugin from /api/releases/{slug} */
	for (int i = 0; i < out->count; i++) {
		struct plugin_info *pi = &out->items[i];

		char path[256];
		snprintf(path, sizeof(path), obf_api_releases_fmt(), pi->slug);

		char *rel_json = api_get(path, token);
		if (!rel_json) continue;

		/*
		 * Response: {"product":{...},"releases":[{...},...]}}
		 * First release is latest. Has "version":"vX.Y.Z" and
		 * "assets":[{"id":123,"filename":"...","platform":"windows",...}]
		 */
		const char *releases_key = strstr(rel_json, "\"releases\"");
		if (!releases_key) { free(rel_json); continue; }

		/* Find first release object */
		const char *first_rel = strchr(releases_key, '{');
		if (first_rel) {
			char version[64] = "";
			json_extract_string(first_rel, "version", version,
					    sizeof(version));
			/* Strip leading 'v' if present */
			const char *ver = version;
			if (ver[0] == 'v' || ver[0] == 'V') ver++;
			snprintf(pi->latest_version,
				 sizeof(pi->latest_version), "%s", ver);

			/* Find matching asset for our platform */
			const char *assets_key = strstr(first_rel, "\"assets\"");
			if (assets_key) {
				const char *asset_pos = assets_key;
				const char *plat = platform_suffix();

				while ((asset_pos = strchr(asset_pos, '{')) != NULL) {
					char asset_plat[64] = "";
					char asset_fn[256] = "";
					json_extract_string(asset_pos,
							    "platform",
							    asset_plat,
							    sizeof(asset_plat));
					json_extract_string(asset_pos,
							    "filename",
							    asset_fn,
							    sizeof(asset_fn));

					int asset_id = json_extract_int(
						asset_pos, "id");

					if (strcmp(asset_plat, plat) == 0 &&
					    asset_id > 0) {
						pi->download_asset_id = asset_id;
						break;
					}
					asset_pos++;
				}
			}
		}

		free(rel_json);
	}

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
	snprintf(path, sizeof(path), "%s%c.%s.version", dir, PATH_SEP, slug);
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

/* ---- Archive extraction via system tools ---- */

static bool is_zip(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	unsigned char sig[4] = {0};
	fread(sig, 1, 4, f);
	fclose(f);
	return sig[0] == 'P' && sig[1] == 'K' &&
	       sig[2] == 0x03 && sig[3] == 0x04;
}

static bool is_targz(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	unsigned char sig[2] = {0};
	fread(sig, 1, 2, f);
	fclose(f);
	return sig[0] == 0x1f && sig[1] == 0x8b;
}

static bool get_temp_dir(char *buf, size_t sz)
{
#ifdef _WIN32
	GetTempPathA((DWORD)sz, buf);
	return true;
#else
	snprintf(buf, sz, "/tmp");
	return true;
#endif
}

/*
 * Extract archive to a temp directory, then find the plugin binary.
 *
 * Release ZIP structure (Windows):
 *   easy-irl-stream/obs-plugins/64bit/easy-irl-stream.dll
 *
 * Release tar.gz structure (Linux):
 *   obs-plugins/easy-irl-stream.so
 */
static bool extract_archive(const char *archive_path, const char *extract_dir)
{
#ifdef _WIN32
	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
		 "powershell -NoProfile -Command \"Expand-Archive -Force "
		 "-Path '%s' -DestinationPath '%s'\"",
		 archive_path, extract_dir);
	int ret = system(cmd);
	return ret == 0;
#else
	char cmd[2048];
	if (is_targz(archive_path)) {
		snprintf(cmd, sizeof(cmd),
			 "mkdir -p '%s' && tar xzf '%s' -C '%s'",
			 extract_dir, archive_path, extract_dir);
	} else {
		snprintf(cmd, sizeof(cmd),
			 "mkdir -p '%s' && unzip -o '%s' -d '%s'",
			 extract_dir, archive_path, extract_dir);
	}
	int ret = system(cmd);
	return ret == 0;
#endif
}

/*
 * Recursively search for the plugin binary (slug.dll/.so) in extract_dir.
 * This handles any nested folder structure.
 */
static bool find_plugin_binary(const char *dir, const char *filename,
			       char *result, size_t result_sz)
{
#ifdef _WIN32
	char search[512];
	snprintf(search, sizeof(search), "%s\\*", dir);

	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(search, &fd);
	if (hFind == INVALID_HANDLE_VALUE) return false;

	do {
		if (fd.cFileName[0] == '.') continue;

		char full[512];
		snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (find_plugin_binary(full, filename, result, result_sz))
			{
				FindClose(hFind);
				return true;
			}
		} else if (_stricmp(fd.cFileName, filename) == 0) {
			snprintf(result, result_sz, "%s", full);
			FindClose(hFind);
			return true;
		}
	} while (FindNextFileA(hFind, &fd));

	FindClose(hFind);
	return false;
#else
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
		 "find '%s' -name '%s' -type f 2>/dev/null | head -1",
		 dir, filename);
	FILE *p = popen(cmd, "r");
	if (!p) return false;
	if (fgets(result, (int)result_sz, p)) {
		size_t len = strlen(result);
		while (len > 0 && (result[len-1] == '\n' || result[len-1] == '\r'))
			result[--len] = '\0';
		pclose(p);
		return len > 0;
	}
	pclose(p);
	return false;
#endif
}

static void remove_directory(const char *dir)
{
#ifdef _WIN32
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", dir);
	system(cmd);
#else
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	system(cmd);
#endif
}

/*
 * Copy a file with UAC elevation on Windows.
 * Launches a hidden PowerShell process as Administrator.
 */
#ifdef _WIN32
static bool copy_elevated(const char *src, const char *dst)
{
	char ps_args[2048];
	snprintf(ps_args, sizeof(ps_args),
		 "-NoProfile -WindowStyle Hidden -Command \""
		 "Copy-Item -Force -Path '%s' -Destination '%s'\"",
		 src, dst);

	SHELLEXECUTEINFOA sei;
	memset(&sei, 0, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = "runas";
	sei.lpFile = "powershell.exe";
	sei.lpParameters = ps_args;
	sei.nShow = SW_HIDE;

	if (!ShellExecuteExA(&sei)) {
		dbg_log(LOG_ERROR, "[%s] UAC elevation denied or failed",
			PLUGIN_NAME);
		return false;
	}

	HANDLE proc = sei.hProcess;
	WaitForSingleObject(proc, 30000);

	DWORD exit_code = 1;
	GetExitCodeProcess(proc, &exit_code);
	CloseHandle(proc);

	return exit_code == 0;
}
#endif

static void hide_file(const char *path)
{
#ifdef _WIN32
	SetFileAttributesA(path, FILE_ATTRIBUTE_HIDDEN);
#else
	(void)path;
#endif
}

bool downloader_write_version_file(const char *obs_plugin_dir,
				   const char *slug, const char *version)
{
	char ver_path[512];
	snprintf(ver_path, sizeof(ver_path), "%s%c.%s.version",
		 obs_plugin_dir, PATH_SEP, slug);

	FILE *f = fopen(ver_path, "w");
	if (f) {
		fputs(version, f);
		fclose(f);
		hide_file(ver_path);
		return true;
	}

#ifdef _WIN32
	/* Try elevated write */
	char tmp_ver[512];
	char tmp_d[512];
	get_temp_dir(tmp_d, sizeof(tmp_d));
	snprintf(tmp_ver, sizeof(tmp_ver), "%s%cst_pm_%s.version",
		 tmp_d, PATH_SEP, slug);

	f = fopen(tmp_ver, "w");
	if (!f) return false;
	fputs(version, f);
	fclose(f);

	bool ok = copy_elevated(tmp_ver, ver_path);
	remove(tmp_ver);
	return ok;
#else
	return false;
#endif
}

/* ---- Download and install a plugin ---- */

bool downloader_install_plugin(const char *token, const char *slug,
			       int asset_id, const char *obs_plugin_dir)
{
	s_last_error[0] = '\0';

	if (asset_id <= 0) {
		set_error("No download asset found for %s", slug);
		dbg_log(LOG_ERROR, "[%s] No asset ID for %s", PLUGIN_NAME, slug);
		return false;
	}

	char path[512];
	snprintf(path, sizeof(path), obf_api_downloads_fmt(), slug, asset_id);

	char url[512];
	snprintf(url, sizeof(url), "%s%s%s",
		 obf_https_prefix(), obf_stools_host(), path);

	const char *ext =
#ifdef _WIN32
		".dll";
#else
		".so";
#endif

	/* Download to temp file */
	char tmp_dir[512];
	get_temp_dir(tmp_dir, sizeof(tmp_dir));

	char archive_path[512];
	snprintf(archive_path, sizeof(archive_path),
		 "%s%cst_pm_%s_download", tmp_dir, PATH_SEP, slug);

	FILE *f = fopen(archive_path, "wb");
	if (!f) {
		dbg_log(LOG_ERROR, "[%s] Cannot open %s for writing",
			PLUGIN_NAME, archive_path);
		return false;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fclose(f);
		remove(archive_path);
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
		set_error("Download failed: %s (HTTP %ld)",
			  res != CURLE_OK ? curl_easy_strerror(res) : "server error",
			  http_code);
		dbg_log(LOG_ERROR, "[%s] Download %s failed: %s (HTTP %ld)",
			PLUGIN_NAME, slug, curl_easy_strerror(res), http_code);
		remove(archive_path);
		return false;
	}

	/* Check if it's an archive or a raw binary */
	bool success = false;
	char final_path[512];
	snprintf(final_path, sizeof(final_path), "%s%c%s%s",
		 obs_plugin_dir, PATH_SEP, slug, ext);

	if (is_zip(archive_path) || is_targz(archive_path)) {
		/* Extract to temp, then find the binary */
		char extract_dir[512];
		snprintf(extract_dir, sizeof(extract_dir),
			 "%s%cst_pm_%s_extract", tmp_dir, PATH_SEP, slug);

		remove_directory(extract_dir);

		dbg_log(LOG_INFO, "[%s] Extracting archive for %s",
			PLUGIN_NAME, slug);

		if (!extract_archive(archive_path, extract_dir)) {
			set_error("Failed to extract archive for %s", slug);
			dbg_log(LOG_ERROR, "[%s] Failed to extract archive for %s",
				PLUGIN_NAME, slug);
			remove(archive_path);
			remove_directory(extract_dir);
			return false;
		}

		/* Find the DLL/SO inside the extracted tree */
		char dll_filename[128];
		snprintf(dll_filename, sizeof(dll_filename), "%s%s", slug, ext);

		char found_path[512] = "";
		if (!find_plugin_binary(extract_dir, dll_filename,
					found_path, sizeof(found_path))) {
			set_error("Could not find %s in archive", dll_filename);
			dbg_log(LOG_ERROR,
				"[%s] Could not find %s in extracted archive",
				PLUGIN_NAME, dll_filename);
			remove(archive_path);
			remove_directory(extract_dir);
			return false;
		}

		dbg_log(LOG_INFO, "[%s] Found binary at: %s",
			PLUGIN_NAME, found_path);

		/* Ensure target dir exists */
		ensure_dir_exists(obs_plugin_dir);

		/* Copy to OBS plugin dir (try normal, then elevated) */
		remove(final_path);

#ifdef _WIN32
		success = CopyFileA(found_path, final_path, FALSE);
		if (!success) {
			DWORD copy_err = GetLastError();
			dbg_log(LOG_INFO,
				"[%s] Normal copy failed (err %lu), trying elevated",
				PLUGIN_NAME, copy_err);
			success = copy_elevated(found_path, final_path);
		}
#else
		{
			char cp_cmd[1024];
			snprintf(cp_cmd, sizeof(cp_cmd),
				 "cp '%s' '%s'", found_path, final_path);
			success = system(cp_cmd) == 0;
			if (!success) {
				snprintf(cp_cmd, sizeof(cp_cmd),
					 "pkexec cp '%s' '%s'",
					 found_path, final_path);
				success = system(cp_cmd) == 0;
			}
		}
#endif

		remove_directory(extract_dir);
	} else {
		/* Raw binary - just move to final location */
		remove(final_path);
#ifdef _WIN32
		success = CopyFileA(archive_path, final_path, FALSE);
		if (!success) {
			dbg_log(LOG_INFO,
				"[%s] Normal copy failed, trying elevated",
				PLUGIN_NAME);
			success = copy_elevated(archive_path, final_path);
		}
#else
		success = rename(archive_path, final_path) == 0;
		if (!success) {
			char cp_cmd[1024];
			snprintf(cp_cmd, sizeof(cp_cmd),
				 "pkexec cp '%s' '%s'",
				 archive_path, final_path);
			success = system(cp_cmd) == 0;
		}
#endif
	}

	remove(archive_path);

	if (success) {
		dbg_log(LOG_INFO, "[%s] Installed %s to %s",
			PLUGIN_NAME, slug, final_path);
	} else {
#ifdef _WIN32
		DWORD err = GetLastError();
		set_error("Failed to copy to %s (error %lu)", final_path, err);
#else
		set_error("Failed to copy to %s", final_path);
#endif
		dbg_log(LOG_ERROR, "[%s] Failed to install %s to %s",
			PLUGIN_NAME, slug, final_path);
	}

	return success;
}
