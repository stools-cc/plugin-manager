#include "oauth.h"
#include "obfuscation.h"
#include "debug-log.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")
#define OAUTH_SOCKET SOCKET
#define OAUTH_INVALID INVALID_SOCKET
#define oauth_close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define OAUTH_SOCKET int
#define OAUTH_INVALID (-1)
#define oauth_close close
#endif

#define LISTEN_PORT_START 19850
#define LISTEN_PORT_END   19860

static const char *HTML_SUCCESS =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html; charset=utf-8\r\n"
	"Connection: close\r\n\r\n"
	"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
	"<link href=\"https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@400;500;600&display=swap\" rel=\"stylesheet\">"
	"<style>"
	"*{margin:0;padding:0;box-sizing:border-box}"
	"body{font-family:'Plus Jakarta Sans',-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
	"background:#08090e;color:rgba(255,255,255,0.85);"
	"display:flex;align-items:center;justify-content:center;min-height:100vh}"
	"::selection{background:rgba(59,126,228,0.3)}"
	".card{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);"
	"backdrop-filter:blur(20px);border-radius:16px;padding:48px;text-align:center;"
	"max-width:380px;width:100%}"
	".icon{width:56px;height:56px;border-radius:50%;display:flex;align-items:center;"
	"justify-content:center;margin:0 auto 20px;"
	"background:linear-gradient(135deg,rgba(72,187,120,0.15),rgba(72,187,120,0.08));"
	"border:1px solid rgba(72,187,120,0.2)}"
	".icon svg{width:28px;height:28px;color:#48bb78}"
	"h2{font-size:1.125rem;font-weight:600;color:#fff;margin-bottom:6px}"
	"p{font-size:0.875rem;color:rgba(255,255,255,0.5);line-height:1.5}"
	"</style></head><body>"
	"<div class=\"card\">"
	"<div class=\"icon\"><svg xmlns=\"http://www.w3.org/2000/svg\" fill=\"none\" viewBox=\"0 0 24 24\" stroke=\"currentColor\" stroke-width=\"2\"><path stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"M5 13l4 4L19 7\"/></svg></div>"
	"<h2>Login erfolgreich</h2>"
	"<p>Du kannst dieses Fenster schlie&szlig;en und zu OBS zur&uuml;ckkehren.</p>"
	"</div></body></html>";

static const char *HTML_ERROR =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html; charset=utf-8\r\n"
	"Connection: close\r\n\r\n"
	"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
	"<link href=\"https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@400;500;600&display=swap\" rel=\"stylesheet\">"
	"<style>"
	"*{margin:0;padding:0;box-sizing:border-box}"
	"body{font-family:'Plus Jakarta Sans',-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
	"background:#08090e;color:rgba(255,255,255,0.85);"
	"display:flex;align-items:center;justify-content:center;min-height:100vh}"
	"::selection{background:rgba(59,126,228,0.3)}"
	".card{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);"
	"backdrop-filter:blur(20px);border-radius:16px;padding:48px;text-align:center;"
	"max-width:380px;width:100%}"
	".icon{width:56px;height:56px;border-radius:50%;display:flex;align-items:center;"
	"justify-content:center;margin:0 auto 20px;"
	"background:linear-gradient(135deg,rgba(239,68,68,0.15),rgba(239,68,68,0.08));"
	"border:1px solid rgba(239,68,68,0.2)}"
	".icon svg{width:28px;height:28px;color:#ef4444}"
	"h2{font-size:1.125rem;font-weight:600;color:#fff;margin-bottom:6px}"
	"p{font-size:0.875rem;color:rgba(255,255,255,0.5);line-height:1.5}"
	"</style></head><body>"
	"<div class=\"card\">"
	"<div class=\"icon\"><svg xmlns=\"http://www.w3.org/2000/svg\" fill=\"none\" viewBox=\"0 0 24 24\" stroke=\"currentColor\" stroke-width=\"2\"><path stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"M6 18L18 6M6 6l12 12\"/></svg></div>"
	"<h2>Login fehlgeschlagen</h2>"
	"<p>Bitte versuche es erneut.</p>"
	"</div></body></html>";

static OAUTH_SOCKET start_server(int *port_out)
{
	for (int port = LISTEN_PORT_START; port <= LISTEN_PORT_END; port++) {
		OAUTH_SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == OAUTH_INVALID)
			continue;

		int reuse = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
			   (const char *)&reuse, sizeof(reuse));

		struct sockaddr_in addr = {0};
		addr.sin_family = AF_INET;
		addr.sin_port = htons((uint16_t)port);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
		    listen(sock, 1) == 0) {
			*port_out = port;
			return sock;
		}

		oauth_close(sock);
	}
	return OAUTH_INVALID;
}

static bool extract_param(const char *request, const char *key,
			  char *out, size_t out_sz)
{
	char search[64];
	snprintf(search, sizeof(search), "%s=", key);
	const char *pos = strstr(request, search);
	if (!pos) return false;
	pos += strlen(search);

	size_t i = 0;
	while (pos[i] && pos[i] != '&' && pos[i] != ' ' &&
	       pos[i] != '\r' && pos[i] != '\n' && i < out_sz - 1) {
		out[i] = pos[i];
		i++;
	}
	out[i] = '\0';
	return i > 0;
}

bool oauth_start_flow(char *token_out, int token_out_sz)
{
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

	int port = 0;
	OAUTH_SOCKET server = start_server(&port);
	if (server == OAUTH_INVALID) {
		dbg_log(LOG_ERROR, "[%s] OAuth: failed to start local server",
			PLUGIN_NAME);
		return false;
	}

	char redirect_uri[128];
	snprintf(redirect_uri, sizeof(redirect_uri),
		 "http://localhost:%d/callback", port);

	char url[512];
	snprintf(url, sizeof(url),
		 "%s%s/auth/connect?app=plugin-manager&redirect_uri=%s",
		 obf_https_prefix(), obf_stools_host(), redirect_uri);

#ifdef _WIN32
	ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
	system(cmd);
#else
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", url);
	system(cmd);
#endif

	dbg_log(LOG_INFO, "[%s] OAuth: waiting for callback on port %d",
		PLUGIN_NAME, port);

	/* Set timeout on accept: 120 seconds */
#ifdef _WIN32
	int timeout_ms = 120000;
	setsockopt(server, SOL_SOCKET, SO_RCVTIMEO,
		   (const char *)&timeout_ms, sizeof(timeout_ms));
#else
	struct timeval tv = {120, 0};
	setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

	struct sockaddr_in client_addr;
	int client_len = sizeof(client_addr);
	OAUTH_SOCKET client = accept(server, (struct sockaddr *)&client_addr,
#ifdef _WIN32
				     &client_len
#else
				     (socklen_t *)&client_len
#endif
	);

	bool success = false;

	if (client != OAUTH_INVALID) {
		char request[4096] = {0};
		recv(client, request, sizeof(request) - 1, 0);

		char token[512] = "";
		char error[64] = "";

		extract_param(request, "token", token, sizeof(token));
		extract_param(request, "error", error, sizeof(error));

		if (token[0] && !error[0]) {
			send(client, HTML_SUCCESS, (int)strlen(HTML_SUCCESS), 0);
			snprintf(token_out, token_out_sz, "%s", token);
			success = true;
			dbg_log(LOG_INFO, "[%s] OAuth: token received",
				PLUGIN_NAME);
		} else {
			send(client, HTML_ERROR, (int)strlen(HTML_ERROR), 0);
			dbg_log(LOG_WARNING,
				"[%s] OAuth: denied or error (%s)",
				PLUGIN_NAME, error[0] ? error : "no token");
		}

		oauth_close(client);
	} else {
		dbg_log(LOG_WARNING, "[%s] OAuth: timeout waiting for callback",
			PLUGIN_NAME);
	}

	oauth_close(server);
	return success;
}
