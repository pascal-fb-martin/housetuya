/* HouseTuya - A simple home web server for control Tuya-based Devices
 *
 * Copyright 2024, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housetuya_model.c - Handle the database of Tuya products.
 *
 * SYNOPSYS:
 *
 * const char *housetuya_model_initialize (int argc, const char **argv);
 *
 *    Initialize this module at startup.
 *
 * int housetuya_model_changed (void);
 *
 *    Indicate if the configuration was changed due to discovery, which
 *    means it must be saved.
 *
 * void housetuya_model_live_config (ParserContext context, int top);
 *
 *    Recover the current live config, typically to save it to disk after
 *    a change has been detected.
 *
 * const char *housetuya_model_refresh (void);
 *
 *    Re-evaluate the configuration after it changed.
 *
 * const char *housetuya_model_get_name (const char *id);
 *
 *    Return a user-friendly name for the device model.
 *
 * int housetuya_model_get_control (const char *id);
 *
 *    Return the data point number used to control this model of devices.
 *
 * KNOWN ISSUES
 *
 *    Searching through the database of models is linear. As the list of
 *    product grows, this might not be sustainable.
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "echttp.h"
#include "echttp_json.h"
#include "houselog.h"
#include "houseconfig.h"

#include "housetuya_model.h"


struct ModelMap {
    char *id;
    char *name;
    int control;
};

static int ModelListChanged = 0;

static struct ModelMap *Models;
static int ModelsCount = 0;
static int ModelsSpace = 0;

int housetuya_model_changed (void) {
    int result = ModelListChanged;
    ModelListChanged = 0;
    return result;
}

static int housetuya_model_search (const char *id) {
    int i;
    for (i = 0; i < ModelsCount; ++i) {
        if (!strcasecmp(id, Models[i].id)) return i;
    }
    return -1;
}

const char *housetuya_model_get_name (const char *id) {
    int i = housetuya_model_search (id);
    if (i < 0) return 0;
    return Models[i].name;
}

int housetuya_model_get_control (const char *id) {
    int i = housetuya_model_search (id);
    if (i < 0) return 0;
    return Models[i].control;
}

static int housetuya_model_add (const char *id) {
    if (ModelsCount >= ModelsSpace) {
        ModelsSpace = ModelsCount + 16;
        Models = realloc (Models, ModelsSpace * sizeof(struct ModelMap));
        memset (Models+ModelsSpace, 0,
                (ModelsSpace - ModelsCount) * sizeof(struct ModelMap));
    }
    int i = ModelsCount++;
    Models[i].id = strdup(id);
    return i;
}

const char *housetuya_model_refresh (void) {

    int i;
    int models = 0;
    int count = 0;

    if (houseconfig_active()) {
        models = houseconfig_array (0, ".tuya.models");
        if (models < 0) return "cannot find models array";

        count = houseconfig_array_length (models);
        if (echttp_isdebug()) fprintf (stderr, "found %d models\n", count);
    } else {
        ModelsCount = 0;
        return 0;
    }

    // Allocate, or reallocate without loosing what we already have.
    //
    int needed = count + 16;
    if (needed > ModelsSpace) {
        Models = realloc (Models, needed * sizeof(struct ModelMap));
        if (!Models) return "no more memory";
        memset (Models+ModelsSpace, 0,
                (needed - ModelsSpace) * sizeof(struct ModelMap));
        ModelsSpace = needed;
    }

    int *list = calloc (count, sizeof(int));
    count = houseconfig_enumerate (models, list, count);
    for (i = 0; i < count; ++i) {
        int model = list[i];
        if (model <= 0) continue;
        const char *id = houseconfig_string (model, ".id");
        const char *name = houseconfig_string (model, ".name");
        int control = houseconfig_integer (model, ".control");
        if ((!id) || (!name) || (!control)) continue;
        int idx = housetuya_model_search (id);
        if (idx < 0) {
            idx = housetuya_model_add (id);
            ModelListChanged = 1;
        }
        if (Models[idx].name) {
            if (strcmp (Models[idx].name, name)) {
                free (Models[idx].name);
                Models[idx].name = strdup (name);
                ModelListChanged = 1;
            }
        } else {
            Models[idx].name = strdup (name);
            ModelListChanged = 1;
        }
        if (Models[idx].control != control) {
            Models[idx].control = control;
            ModelListChanged = 1;
        }
    }
    free (list);

    return 0;
}

void housetuya_model_live_config (ParserContext context, int top) {

    int items = echttp_json_add_array (context, top, "models");

    int i;
    for (i = 0; i < ModelsCount; ++i) {
        int device = echttp_json_add_object (context, items, 0);
        echttp_json_add_string (context, device, "id", Models[i].id);
        echttp_json_add_string (context, device, "name", Models[i].name);
        echttp_json_add_integer (context, device, "control", Models[i].control);
    }
}

