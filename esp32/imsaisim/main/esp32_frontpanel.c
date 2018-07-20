/**
 *
 */
#ifdef ESP_PLATFORM
#include <unistd.h>
#include <termios.h>
#include "esp_system.h"
#include "esp_console.h"

#include "sim.h"
#include "simglb.h"
#include "frontpanel.h"

#include <signal.h>

/**
 *  Stubs to overcome missing vio variables
 */
char bg_color[] = "#000000";
char fg_color[] = "#000000";
int slf = 0;

#endif //ESP_PLATFORM
