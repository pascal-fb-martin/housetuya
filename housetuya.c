/* HouseTuya - A simple web service for control of Tuya devices
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
 * housetuya.c - Main loop of the housetuya program.
 *
 * SYNOPSYS:
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_json.h"
#include "echttp_static.h"
#include "houseportalclient.h"
#include "housediscover.h"
#include "houselog.h"
#include "houseconfig.h"
#include "housestate.h"
#include "housedepositor.h"

#include "housetuya.h"
#include "housetuya_model.h"
#include "housetuya_device.h"

static int WasLoadedFromDepot = 0;

static int LiveState = 0;

int housetuya_isdebug (void) {
    return echttp_isdebug();
}

static const char *housetuya_status (const char *method, const char *uri,
                                    const char *data, int length) {

    if (housestate_same (LiveState)) return "";

    static char buffer[65537];
    ParserToken token[1024];
    char pool[65537];
    char host[256];
    int count = housetuya_device_count();
    int i;

    gethostname (host, sizeof(host));

    ParserContext context = echttp_json_start (token, 1024, pool, sizeof(pool));

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", host);
    echttp_json_add_string (context, root, "proxy", houseportal_server());
    echttp_json_add_integer (context, root, "timestamp", (long)time(0));
    echttp_json_add_integer (context, root, "latest", housestate_current(LiveState));
    int top = echttp_json_add_object (context, root, "control");
    int container = echttp_json_add_object (context, top, "status");

    for (i = 0; i < count; ++i) {
        time_t pulsed = housetuya_device_deadline(i);
        const char *name = housetuya_device_name(i);
        const char *status = housetuya_device_failure(i);
        int priority = housetuya_device_priority(i);
        if (!status) status = housetuya_device_get(i)?"on":"off";
        const char *commanded = housetuya_device_commanded(i)?"on":"off";

        int point = echttp_json_add_object (context, container, name);
        echttp_json_add_string (context, point, "state", status);
        if (strcmp (status, commanded))
            echttp_json_add_string (context, point, "command", commanded);
        if (pulsed)
            echttp_json_add_integer (context, point, "pulse", (int)pulsed);
        if (priority)
            echttp_json_add_bool (context, point, "priority", priority);
        echttp_json_add_string (context, point, "gear", "light");
    }
    const char *error = echttp_json_export (context, buffer, sizeof(buffer));
    if (error) {
        echttp_error (500, error);
        return "";
    }
    echttp_content_type_json ();
    return buffer;
}

static const char *housetuya_set (const char *method, const char *uri,
                                 const char *data, int length) {

    const char *point = echttp_parameter_get("point");
    const char *statep = echttp_parameter_get("state");
    const char *pulsep = echttp_parameter_get("pulse");
    const char *cause = echttp_parameter_get("cause");
    int state;
    int pulse;
    int i;
    int count = housetuya_device_count();
    int found = 0;

    if (!point) {
        echttp_error (404, "missing point name");
        return "";
    }
    if (!statep) {
        echttp_error (400, "missing state value");
        return "";
    }
    if ((strcmp(statep, "on") == 0) || (strcmp(statep, "1") == 0)) {
        state = 1;
    } else if ((strcmp(statep, "off") == 0) || (strcmp(statep, "0") == 0)) {
        state = 0;
    } else {
        echttp_error (400, "invalid state value");
        return "";
    }

    pulse = pulsep ? atoi(pulsep) : 0;
    if (pulse < 0) {
        echttp_error (400, "invalid pulse value");
        return "";
    }

    for (i = 0; i < count; ++i) {
       if ((strcmp (point, "all") == 0) ||
           (strcmp (point, housetuya_device_name(i)) == 0)) {
           found = 1;
           housetuya_device_set (i, state, pulse, cause);
       }
    }

    if (! found) {
        echttp_error (404, "invalid point name");
        return "";
    }
    return housetuya_status (method, uri, data, length);
}

static const char *housetuya_export (void) {

    static char buffer[65537];
    static char pool[65537];
    static ParserToken token[1024];
    ParserContext context = echttp_json_start (token, 1024, pool, sizeof(pool));
    int root = echttp_json_add_object (context, 0, 0);
    int top  = echttp_json_add_object (context, root, "tuya");
    housetuya_device_live_config (context, top);
    housetuya_model_live_config (context, top);
    const char *error = echttp_json_export (context, buffer, sizeof(buffer));
    if (error) {
        houselog_trace (HOUSE_FAILURE, "CONFIG",
                        "Cannot export configuration: %s\n", error);
        return 0;
    }
    return buffer;
}

static void housetuya_refresh (void) {
    housetuya_device_refresh();
    housetuya_model_refresh();
}

static void housetuya_save_to_depot (const char *data, int length) {
    if (!WasLoadedFromDepot) return;
    houselog_event ("CONFIG", houseconfig_name(), "SAVE", "TO DEPOT");
    housedepositor_put ("config", houseconfig_name(), data, length);
}

static const char *housetuya_config (const char *method, const char *uri,
                                  const char *data, int length) {

    if (strcmp ("GET", method) == 0) {
        const char *response = housetuya_export();
        if (response) {
            echttp_content_type_json ();
            return response;
        }
        echttp_error (400, "No configuration");
    } else if (strcmp ("POST", method) == 0) {
        const char *error = houseconfig_update(data);
        if (error) {
            echttp_error (400, error);
        } else {
            housestate_changed (LiveState);
            housetuya_refresh();
            housetuya_save_to_depot (data, length);
        }
    } else {
        echttp_error (400, "invalid method");
    }
    return "";
}

static void housetuya_background (int fd, int mode) {

    static time_t LastCall = 0;
    time_t now = time(0);

    if (now == LastCall) return;
    LastCall = now;

    houseportal_background (now);
    housetuya_device_periodic(now);
    if (housetuya_device_changed() || housetuya_model_changed()) {
        const char *buffer = housetuya_export();
        houseconfig_update(buffer);
        housetuya_save_to_depot (buffer, strlen(buffer));
        if (echttp_isdebug()) fprintf (stderr, "Configuration saved\n");
    }
    housediscover (now);
    houselog_background (now);
    housedepositor_periodic (now);
}

static void housetuya_config_listener (const char *name, time_t timestamp,
                                       const char *data, int length) {
    houselog_event ("CONFIG", houseconfig_name(), "LOAD", "FROM DEPOT %s", name);
    if (!houseconfig_update (data)) {
        housetuya_refresh();
        WasLoadedFromDepot = 1;
    }
}

static void housetuya_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    const char *error;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        static const char *path[] = {"control:/tuya"};
        houseportal_initialize (argc, argv);
        houseportal_declare (echttp_port(4), path, 1);
    }
    housediscover_initialize (argc, argv);
    houselog_initialize ("tuya", argc, argv);
    housedepositor_initialize (argc, argv);

    houseconfig_default ("--config=tuya");
    error = houseconfig_load (argc, argv);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "CONFIG", "Cannot load configuration: %s\n", error);
    }

    LiveState = housestate_declare ("live");

    error = housetuya_device_initialize (argc, argv, LiveState);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "PLUG", "Cannot initialize: %s\n", error);
        exit(1);
    }
    housedepositor_subscribe ("config", houseconfig_name(), housetuya_config_listener);

    echttp_cors_allow_method("GET");
    echttp_protect (0, housetuya_protect);

    echttp_route_uri ("/tuya/status", housetuya_status);
    echttp_route_uri ("/tuya/set",    housetuya_set);

    echttp_route_uri ("/tuya/config", housetuya_config);

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&housetuya_background);
    houselog_event ("SERVICE", "tuya", "STARTED", "ON %s", houselog_host());
    echttp_loop();
}

