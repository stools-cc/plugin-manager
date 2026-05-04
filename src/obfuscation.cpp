#include "obfuscation.h"
#include <string>

#define OBF_FUNC(name, literal)                       \
	static const std::string &name##_storage()    \
	{                                             \
		static const std::string s{literal};  \
		return s;                             \
	}                                             \
	extern "C" const char *name(void)             \
	{                                             \
		return name##_storage().c_str();       \
	}

OBF_FUNC(obf_https_prefix, "https://")
OBF_FUNC(obf_stools_host, "stools.cc")
OBF_FUNC(obf_auth_bearer_fmt, "Authorization: Bearer %s")
OBF_FUNC(obf_ua_prefix, "stools-pluginmanager/")
OBF_FUNC(obf_api_me_path, "/api/me")
OBF_FUNC(obf_api_plugins_path, "/api/plugins")
OBF_FUNC(obf_api_plugin_download_fmt, "/api/plugins/%s/download?platform=%s")
