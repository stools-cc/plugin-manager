#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool oauth_start_flow(char *token_out, int token_out_sz);

#ifdef __cplusplus
}
#endif
