#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_idf_version.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int cmd_reboot_handler(int argc, char **argv)
{
    printf("Rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

static int cmd_version_handler(int argc, char **argv)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t info;
    esp_chip_info(&info);

    printf("App:  %s %s (%s %s)\n",
           app->project_name, app->version, app->date, app->time);
    printf("IDF:  %s\n", esp_get_idf_version());
    printf("Chip: " CONFIG_IDF_TARGET ", %d core(s), rev %d.%d\n",
           info.cores, info.revision / 100, info.revision % 100);

    return 0;
}

void cli_register_cmd_system(void)
{
    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Restart the ESP32",
        .hint = NULL,
        .func = &cmd_reboot_handler,
    };
    esp_console_cmd_register(&reboot_cmd);

    const esp_console_cmd_t version_cmd = {
        .command = "version",
        .help = "Show firmware and chip info",
        .hint = NULL,
        .func = &cmd_version_handler,
    };
    esp_console_cmd_register(&version_cmd);
}
