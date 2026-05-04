#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void auth_init(void);
void auth_shutdown(void);

bool auth_is_logged_in(void);
const char *auth_get_token(void);
const char *auth_get_username(void);

bool auth_login(const char *token);
void auth_logout(void);

bool auth_validate_token(const char *token);

#ifdef __cplusplus
}
#endif
