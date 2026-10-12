#include "model.h"

/* Tranche 2 : pas encore d'alarmes ni de chronomètre (Tranche 3). */
bool model_next_alarm(struct tm *out)
{
    (void)out;
    return false;
}

uint32_t model_chrono_elapsed_ms(void) { return 0; }

bool model_chrono_running(void) { return false; }
