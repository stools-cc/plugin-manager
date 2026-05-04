#include "auth.h"
#include "obfuscation.h"
#include "debug-log.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#define MAX_TOKEN_LEN 512
#define MAX_USERNAME_LEN 128

static char g_token[MAX_TOKEN_LEN] = "";
static char g_username[MAX_USERNAME_LEN] = "";
static bool g_logged_in = false;

/* ---- Token file path ---- */

static void get_token_path(char *buf, size_t sz)
{
#ifdef _WIN32
	char appdata[MAX_PATH];
	SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata);
	snprintf(buf, sz, "%s\\stools\\pluginmanager_token", appdata);
#else
	const char *home = getenv("HOME");
	if (!home) {
		struct passwd *pw = getpwuid(getuid());
		home = pw ? pw->pw_dir : "/tmp";
	}
	snprintf(buf, sz, "%s/.config/stools/pluginmanager_token", home);
#endif
}

static void ensure_dir(const char *path)
{
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char *last_sep = strrchr(dir, '/');
#ifdef _WIN32
	char *last_bs = strrchr(dir, '\\');
	if (last_bs && (!last_sep || last_bs > last_sep))
		last_sep = last_bs;
#endif
	if (last_sep) {
		*last_sep = '\0';
#ifdef _WIN32
		CreateDirectoryA(dir, NULL);
#else
		mkdir(dir, 0700);
#endif
	}
}

static void save_token(const char *token)
{
	char path[512];
	get_token_path(path, sizeof(path));
	ensure_dir(path);
	FILE *f = fopen(path, "w");
	if (f) {
		fputs(token, f);
		fclose(f);
	}
}

static bool load_token(char *buf, size_t sz)
{
	char path[512];
	get_token_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f) return false;
	if (!fgets(buf, (int)sz, f)) {
		fclose(f);
		return false;
	}
	fclose(f);
	size_t len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	return len > 0;
}

static void delete_token(void)
{
	char path[512];
	get_token_path(path, sizeof(path));
	remove(path);
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
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
	curl_set_ssl_opts(curl);

	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK || http_code != 200) {
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

static char *json_get_string_val(const char *json, const char *key)
{
	const char *v = json_find_key(json, key);
	if (!v || *v != '"') return NULL;
	v++;
	const char *end = strchr(v, '"');
	if (!end) return NULL;
	size_t len = (size_t)(end - v);
	char *result = (char *)malloc(len + 1);
	memcpy(result, v, len);
	result[len] = '\0';
	return result;
}

/* ---- Public API ---- */

bool auth_validate_token(const char *token)
{
	if (!token || !token[0]) return false;

	char *json = api_get(obf_api_me_path(), token);
	if (!json) return false;

	char *name = json_get_string_val(json, "username");
	if (!name)
		name = json_get_string_val(json, "name");

	free(json);

	if (name) {
		snprintf(g_username, sizeof(g_username), "%s", name);
		free(name);
		return true;
	}

	return false;
}

bool auth_login(const char *token)
{
	if (!auth_validate_token(token))
		return false;

	snprintf(g_token, sizeof(g_token), "%s", token);
	g_logged_in = true;
	save_token(token);

	dbg_log(LOG_INFO, "[%s] Logged in as %s", PLUGIN_NAME, g_username);
	return true;
}

void auth_logout(void)
{
	g_token[0] = '\0';
	g_username[0] = '\0';
	g_logged_in = false;
	delete_token();
	dbg_log(LOG_INFO, "[%s] Logged out", PLUGIN_NAME);
}

void auth_init(void)
{
	char token[MAX_TOKEN_LEN];
	if (load_token(token, sizeof(token))) {
		if (auth_validate_token(token)) {
			snprintf(g_token, sizeof(g_token), "%s", token);
			g_logged_in = true;
			dbg_log(LOG_INFO, "[%s] Auto-login as %s",
				PLUGIN_NAME, g_username);
		} else {
			dbg_log(LOG_WARNING,
				"[%s] Saved token invalid, login required",
				PLUGIN_NAME);
		}
	}
}

void auth_shutdown(void)
{
	/* nothing to clean up */
}

bool auth_is_logged_in(void)
{
	return g_logged_in;
}

const char *auth_get_token(void)
{
	return g_token;
}

const char *auth_get_username(void)
{
	return g_username;
}
