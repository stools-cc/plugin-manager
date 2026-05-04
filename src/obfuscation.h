#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char *obf_https_prefix(void);
const char *obf_stools_host(void);
const char *obf_auth_bearer_fmt(void);
const char *obf_ua_prefix(void);
const char *obf_api_me_path(void);
const char *obf_api_products_path(void);
const char *obf_api_releases_fmt(void);
const char *obf_api_downloads_fmt(void);

#ifdef __cplusplus
}
#endif
