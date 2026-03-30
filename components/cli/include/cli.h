#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the CLI: register all commands, configure UART0 REPL,
 * suppress default logging, start the REPL task.
 */
esp_err_t cli_init(void);

#ifdef __cplusplus
}
#endif
