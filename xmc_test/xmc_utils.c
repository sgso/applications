/*
 * Copyright (C) 2015 Sebastian Sontberg <sebastian@sontberg.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char months[12][3] = { "Jan", "Feb", "Mar", "Apr", "Mai", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
                                  };

void get_build_datetime(struct tm *date)
{
    char *token = token = strtok(__DATE__, " ");

    for (int i = 0; i < 12; i++) {
        if (strncmp(token, months[i], 3) == 0) {
            date->tm_mon = i;
            break;
        }
    }

    /* day of month */
    date->tm_mday = atoi(strtok(NULL, " "));

    /* year */
    token = strtok(NULL, " ");
    date->tm_year = atoi(token) - 1900;

    /* hours */
    token = strtok(__TIME__, ":");
    date->tm_hour = atoi(token);

    /* minutes */
    token = strtok(NULL, ":");
    date->tm_min = atoi(token);

    /* seconds */
    token = strtok(NULL, ":");
    date->tm_sec = atoi(token);
}
