#pragma once

#include "esp_err.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the CLI: register all commands, configure UART0 REPL,
 * suppress default logging, start the REPL task.
 *
 * cfg: pointer to the live app_config_t owned by main.
 *      The CLI reads and modifies this struct in-place.
 *      Caller must ensure it outlives the CLI.
 */
esp_err_t cli_init(app_config_t *cfg);

/**
 * Return the live config pointer passed to cli_init().
 * Used internally by command handlers.
 */
app_config_t *cli_get_config(void);

#ifdef __cplusplus
}
#endif
