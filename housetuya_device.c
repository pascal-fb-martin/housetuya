/* HouseKasa - A simple home web server for control of TP-Link Kasa Devices
 *
 * Copyright 2021, Pascal Martin
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
 * housetuya_device.c - Control a TP-Link Kasa Device.
 *
 * SYNOPSYS:
 *
 * const char *housetuya_device_initialize (int argc, const char **argv);
 *
 *    Initialize this module at startup.
 *
 * int housetuya_device_changed (void);
 *
 *    Indicate if the configuration was changed due to discovery, which
 *    means it must be saved.
 *
 * void housetuya_device_live_config (ParserContext context, int top);
 *
 *    Recover the current live config, typically to save it to disk after
 *    a change has been detected.
 *
 * const char *housetuya_device_refresh (void);
 *
 *    Re-evaluate the configuration after it changed.
 *
 * int housetuya_device_count (void);
 *
 *    Return the number of configured devices available.
 *
 * const char *housetuya_device_name (int point);
 *
 *    Return the name of a tuya device.
 *
 * const char *housetuya_device_failure (int point);
 *
 *    Return a string describing the failure, or a null pointer if healthy.
 *
 * int    housetuya_device_commanded (int point);
 * time_t housetuya_device_deadline (int point);
 *
 *    Return the last commanded state, or the command deadline, for
 *    the specified tuya device.
 *
 * int housetuya_device_get (int point);
 *
 *    Get the actual state of the device.
 *
 * int housetuya_device_set (int point, int state,
 *                           int pulse, const char *cause);
 *
 *    Set the specified point to the on (1) or off (0) state for the pulse
 *    length specified. The pulse length is in seconds. If pulse is 0, the
 *    device is maintained to the requested state until a new state is issued.
 *
 *    Return 1 on success, 0 if the device is not known and -1 on error.
 *
 * void housetuya_device_periodic (void);
 *
 *    This function must be called every second. It runs the Kasa device
 *    discovery and ends the expired pulses.
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <net/if.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "echttp.h"
#include "echttp_json.h"
#include "houselog.h"
#include "houseconfig.h"

#include "housetuya_crypto.h"
#include "housetuya_messages.h"
#include "housetuya_model.h"
#include "housetuya_device.h"


struct DeviceMap {
    char *name;
    TuyaSecret secret;
    char *model;
    char *description;
    long ipaddress;
    char *host;
    time_t detected;
    int socket;
    int encrypted;
    int status;
    int commanded;
    time_t pending;
    time_t deadline;
    time_t last_sense;
    char out[1024];
    int outlength;
    int control; // Data point to use for on/off controls.
};

static struct sockaddr_in DeviceEmptyAddress = {0};
static struct DeviceMap DeviceEmptyContext = {0};

static int DeviceListChanged = 0;

static struct DeviceMap *Devices = 0;
static int DevicesCount = 0;
static int DevicesSpace = 0;

static char *TuyaTcpPort = "6668";
static int TuyaUdpPort[2] = {6666, 6667};

static int TuyaUdpSocket[2] = {-1, -1};


static const char *housetuya_hexdump (const char *data, int length) {

    static char hex[1024];
    int i;
    int cursor = 0;
    for (i = 0; i < length; ++i)
        cursor += snprintf (hex+cursor, sizeof(hex)-cursor, "%02x", data[i]);
    return hex;
}

int housetuya_device_count (void) {
    return DevicesCount;
}

int housetuya_device_changed (void) {
    int result = DeviceListChanged;
    DeviceListChanged = 0;
    return result;
}

const char *housetuya_device_name (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].name;
}

int housetuya_device_commanded (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].commanded;
}

time_t housetuya_device_deadline (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].deadline;
}

const char *housetuya_device_failure (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    if (!Devices[point].detected) return "silent";
    return 0;
}

int housetuya_device_get (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].status;
}

static int housetuya_device_id_search (const char *id) {
    int i;
    for (i = 0; i < DevicesCount; ++i) {
        if (!strcmp(id, Devices[i].secret.id)) return i;
    }
    return -1;
}

static int housetuya_device_socket_search (int s) {
    int i;
    for (i = 0; i < DevicesCount; ++i) {
        if (Devices[i].socket == s) return i;
    }
    return -1;
}

static void housetuya_device_close (int i) {
    if (Devices[i].socket >= 0) {
        echttp_forget (Devices[i].socket);
        close (Devices[i].socket);
        Devices[i].socket = -1;
        Devices[i].outlength = 0;
    }
}

static void housetuya_device_reset (int i, int status) {
    Devices[i].commanded = Devices[i].status = status;
    Devices[i].pending = Devices[i].deadline = 0;
    housetuya_device_close (i);
}

static int housetuya_device_add (const char *name,
                                 const char *id,
                                 const char *model) {
    if (DevicesCount >= DevicesSpace) {
        DevicesSpace = DevicesCount + 16;
        Devices = realloc (Devices, DevicesSpace * sizeof(struct DeviceMap));
    }
    int i = DevicesCount++;
    Devices[i] = DeviceEmptyContext;
    Devices[i].name = strdup(name);
    Devices[i].secret.id = strdup (id);
    Devices[i].model = strdup (model);
    Devices[i].socket = -1;
    DeviceListChanged = 1;
    return i;
}

static void housetuya_device_refresh_string (char **store, const char *value) {
    if (value) {
        if (*store) {
            if (! strcmp (*store, value)) return; // No change needed
            free (*store);
        }
        *store = strdup(value);
        DeviceListChanged = 1;
    } else {
        if (*store) {
            free (*store);
            *store = 0;
        }
    }
}

// ******* DEVICE DISCOVERY

static void housetuya_device_discovery (int fd, int mode) {

    int code;
    char raw[1024];
    char input[1024];
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    int size = recvfrom (fd, raw, sizeof(raw), 0,
                         (struct sockaddr *)(&addr), &addrlen);

    if (fd == TuyaUdpSocket[0]) {
        size = housetuya_extract (input, sizeof(input), 0, &code, 0, raw, size);
    } else if (fd == TuyaUdpSocket[1]) {
        static TuyaSecret DiscoverySecret = {0};
        if (!DiscoverySecret.key)
            DiscoverySecret.key = strdup(housetuya_discoverykey());
        size = housetuya_extract (input, sizeof(input), &DiscoverySecret, &code, 0, raw, size);
    } else {
        return;
    }
    if (size <= 4) return;
    int ip = htonl(addr.sin_addr.s_addr);

    ParserToken json[16];
    int jsoncount = 16;

    const char *error = echttp_json_parse (input, json, &jsoncount);
    if (error) {
        houselog_trace (HOUSE_FAILURE, "DISCOVERY", "%s: %s", error, input);
        return;
    }
    int id = echttp_json_search (json, ".gwId");
    if ((id < 0) || (json[id].type != PARSER_STRING)) return;

    int product = echttp_json_search (json, ".productKey");
    if ((product < 0) || (json[id].type != PARSER_STRING)) return;

    int need_encryption = 0;
    int encrypt = echttp_json_search (json, ".encrypt");
    if (json[encrypt].type == PARSER_BOOL)
        need_encryption = json[encrypt].value.bool;

    int version = echttp_json_search (json, ".version");
    if ((version < 0) || (json[version].type != PARSER_STRING)) return;

    int index = housetuya_device_id_search (json[id].value.string);
    if (index < 0) {
        // Newly discovered device.
        char name[64];

        snprintf (name, sizeof(name), "new_%d", DevicesCount);
        index =  housetuya_device_add (name,
                                       json[id].value.string,
                                       json[product].value.string);
    }

    // The following items always come from the device, overwrite existing.
    //
    housetuya_device_refresh_string
        (&(Devices[index].model), json[product].value.string);
    housetuya_device_refresh_string
        (&(Devices[index].secret.version), json[version].value.string);
    Devices[index].encrypted = need_encryption;

    if (Devices[index].ipaddress != addr.sin_addr.s_addr) {
        Devices[index].ipaddress = addr.sin_addr.s_addr;
        housetuya_device_refresh_string
            (&(Devices[index].host), inet_ntoa(addr.sin_addr));
    }
    if (!Devices[index].detected) {
        houselog_event ("DEVICE", Devices[index].name, "DETECTED",
                        "ADDRESS %s", Devices[index].host);
        Devices[index].last_sense = 0; // Force immediate query.
    }
    Devices[index].detected = time(0);
}

static void housetuya_device_discovery_sockets (void) {

    int i;
    struct sockaddr_in listenaddr;

    listenaddr.sin_family = AF_INET;
    listenaddr.sin_addr.s_addr = INADDR_ANY;

    for (i = 0; i < 2; ++i) {
        listenaddr.sin_port = htons(TuyaUdpPort[i]);
        TuyaUdpSocket[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (TuyaUdpSocket[i] < 0) {
            houselog_trace (HOUSE_FAILURE, "DISCOVERY",
                            "Cannot open socket for port %d: %s",
                            TuyaUdpPort[i], strerror(errno));
            continue;
        }

        if (bind(TuyaUdpSocket[i],
                 (struct sockaddr *)(&listenaddr), sizeof(listenaddr)) < 0) {
            houselog_trace (HOUSE_FAILURE, "DISCOVERY",
                            "Cannot bind to port %d: %s",
                            TuyaUdpPort[i], strerror(errno));
            close (TuyaUdpSocket[i]);
            TuyaUdpSocket[i] = -1;
            continue;
        }

        int value = 1;
        if (setsockopt(TuyaUdpSocket[i], SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) < 0) {
            houselog_trace (HOUSE_FAILURE, "DISCOVERY",
                            "Cannot broadcast on port %d: %s",
                            TuyaUdpPort[i], strerror(errno));
        }
        echttp_listen (TuyaUdpSocket[i], 1, housetuya_device_discovery, 0);
        houselog_trace (HOUSE_INFO, "DISCOVERY",
                        "UDP port %d is now open", TuyaUdpPort[i]);
    }
}

// ******* DEVICE POLLING AND CONTROL

static void housetuya_device_status_update (int device, int status) {
    if (device < 0) return;
    if (status != Devices[device].status) {
        if (Devices[device].pending &&
                (status == Devices[device].commanded)) {
            houselog_event ("DEVICE", Devices[device].name,
                            "CONFIRMED", "FROM %s TO %s",
                            Devices[device].status?"on":"off",
                            status?"on":"off");
            Devices[device].pending = 0;
        } else {
            houselog_event ("DEVICE", Devices[device].name,
                            "CHANGED", "FROM %s TO %s",
                            Devices[device].status?"on":"off",
                            status?"on":"off");
            // Device commanded by someone else.
            Devices[device].commanded = status;
            Devices[device].pending = 0;
        }
        Devices[device].status = status;
    }
    Devices[device].detected = time(0);
}

static void housetuya_device_receive (int fd, int mode) {

    char raw[1600];
    int length = read (fd, raw, sizeof(raw));
    int device = housetuya_device_socket_search (fd);
    if (device < 0) {
        echttp_forget (fd);
        close (fd);
        return;
    }
    if (length <= 0) {
        housetuya_device_close (device);
        return;
    }
    struct DeviceMap *dev = Devices + device;
    if (echttp_isdebug())
        houselog_trace (HOUSE_INFO, "PROTOCOL", "received from %s (%d bytes): %s", dev->secret.id, length, housetuya_hexdump(raw, length));
    char payload[1600];
    int code;
    int sequence;
    length = housetuya_extract (payload, sizeof(payload), &(dev->secret),
                                &code, &sequence, raw, length);
    if (code == TUYA_CONTROL) return; // That's the device command response.
    if (length <= 4) return;

    // No matter what happens, we are done with this socket.
    housetuya_device_close (device);

    if ((code != TUYA_STATUS) && (code != TUYA_QUERY)) return;

    // The STATUS response is a subset of the QUERY response. Both
    // return the value of the control data point, which is the only
    // item we actually care about here.

    ParserToken json[256]; // Plan for devices with a lot of data points.
    int jsoncount = 256;

    payload[length] = 0;
    char *input = strdup(payload); // Parsing will destruct the string.
    const char *error = echttp_json_parse (payload, json, &jsoncount);
    if (error) {
        houselog_trace (HOUSE_FAILURE, "PROTOCOL", "%s: %s", error, input);
        free (input);
        return;
    }
    free (input);

    char path[32];
    snprintf (path, sizeof(path), ".dps.%d", dev->control);
    int state = echttp_json_search (json, path);
    if (state < 0) {
        houselog_trace (HOUSE_FAILURE, "PROTOCOL", "missing item %s", path);
        return;
    }
    if (json[state].type != PARSER_BOOL) {
        houselog_trace (HOUSE_FAILURE, "PROTOCOL", "item %s is not a boolean", path);
        return;
    }

    housetuya_device_status_update (device, json[state].value.bool);
}

static void housetuya_device_send (int fd, int mode) {

    int device = housetuya_device_socket_search (fd);
    if (device < 0) {
        echttp_forget (fd);
        close (fd);
        return;
    }
    if (Devices[device].outlength > 0) {
        write (fd, Devices[device].out, Devices[device].outlength);
        Devices[device].outlength = 0;
    }
    echttp_listen (fd, 1, housetuya_device_receive, 0);
    return;
}

static struct DeviceMap *housetuya_device_preamble (int device) {
    struct DeviceMap *dev = Devices + device;
    if (!dev->ipaddress) return 0;
    if (dev->encrypted && (!dev->secret.id)) return 0;
    if (dev->control <= 0) {
        dev->control = housetuya_model_get_control (dev->model);
        if (dev->control <= 0) return 0;
    }
    housetuya_device_close (device); // Cleanup.
    dev->socket = echttp_connect (dev->host, TuyaTcpPort);
    if (dev->socket < 0) return 0;
    return dev;
}

static void housetuya_device_sense (int device) {
    struct DeviceMap *dev = housetuya_device_preamble (device);
    if (!dev) return;
    dev->outlength =
        housetuya_query (dev->out, sizeof(dev->out), &(dev->secret), 0);
    if (echttp_isdebug())
        houselog_trace (HOUSE_INFO, "PROTOCOL", "Sending QUERY to %s (%d bytes): %s", dev->secret.id, dev->outlength, housetuya_hexdump(dev->out, dev->outlength));
    echttp_listen (dev->socket, 2, housetuya_device_send, 0);
}

static void housetuya_device_control (int device, int state) {
    struct DeviceMap *dev = housetuya_device_preamble (device);
    if (!dev) return;
    dev->outlength =
        housetuya_control (dev->out, sizeof(dev->out), &(dev->secret), 0, dev->control, state);
    if (echttp_isdebug())
        houselog_trace (HOUSE_INFO, "PROTOCOL", "Sending CONTROL %d to %s (%d bytes): %s", state, dev->secret.id, dev->outlength, housetuya_hexdump(dev->out, dev->outlength));
    echttp_listen (dev->socket, 2, housetuya_device_send, 0);
}

int housetuya_device_set (int device, int state, int pulse, const char *cause) {

    const char *namedstate = state?"on":"off";
    time_t now = time(0);

    char comment[256];
    if (cause)
        snprintf (comment, sizeof(comment), " (%s)", cause);
    else
        comment[0] = 0;
    if (device < 0 || device > DevicesCount) return 0;

    if (echttp_isdebug()) {
        if (pulse) fprintf (stderr, "set %s to %s at %ld (pulse %ds)%s\n", Devices[device].name, namedstate, now, pulse, comment);
        else       fprintf (stderr, "set %s to %s at %ld%s\n", Devices[device].name, namedstate, now, comment);
    }

    if (pulse > 0) {
        Devices[device].deadline = now + pulse;
        houselog_event ("DEVICE", Devices[device].name, "SET",
                        "%s FOR %d SECONDS%s", namedstate, pulse, comment);
    } else {
        Devices[device].deadline = 0;
        houselog_event ("DEVICE", Devices[device].name, "SET",
                        "%s%s", namedstate, comment);
    }
    Devices[device].commanded = state;
    if (Devices[device].pending) return 1; // Don't overstep.
    Devices[device].pending = now + 10;

    // Only send a command if we detected the device on the network.
    //
    if (Devices[device].detected) {
        housetuya_device_control (device, state);
    }
    return 0;
}

void housetuya_device_periodic (time_t now) {

    static time_t LastRetry = 0;

    if (now < LastRetry + 5) return;
    LastRetry = now;

    int i;
    for (i = 0; i < DevicesCount; ++i) {

        if (now >= Devices[i].last_sense + 35) {
            if ((!Devices[i].pending) && (Devices[i].ipaddress != 0)) {
                housetuya_device_close (i); // Cleanup, if ever.
                housetuya_device_sense (i);
            }
            Devices[i].last_sense = now;
        }

        // If we did not detect a device for 3 senses, consider it failed.
        if (Devices[i].detected > 0 && Devices[i].detected < now - 100) {
            houselog_event ("DEVICE", Devices[i].name, "SILENT",
                            "ADDRESS %s", Devices[i].host);
            housetuya_device_close (i);
            housetuya_device_reset (i, 0);
            Devices[i].detected = 0;
        }

        if (Devices[i].deadline > 0 && now >= Devices[i].deadline) {
            houselog_event ("DEVICE", Devices[i].name, "RESET", "END OF PULSE");
            Devices[i].commanded = 0;
            Devices[i].pending = now + 5;
            Devices[i].deadline = 0;
        }
        if (Devices[i].status != Devices[i].commanded) {
            if (Devices[i].pending > now) {
                if (Devices[i].detected) {
                    const char *state = Devices[i].commanded?"on":"off";
                    houselog_event ("DEVICE", Devices[i].name, "RETRY", state);
                    housetuya_device_control (i, Devices[i].commanded);
                }
            } else if (Devices[i].pending) {
                houselog_event ("DEVICE", Devices[i].name, "TIMEOUT", "");
                housetuya_device_close (i);
                housetuya_device_reset (i, Devices[i].status);
            }
        }
    }
}

// ******* CONFIGURATION

const char *housetuya_device_refresh (void) {

    if (! houseconfig_active()) return 0;

    int devices = houseconfig_array (0, ".tuya.devices");
    if (devices < 0) return "cannot find devices array";

    int count = houseconfig_array_length (devices);
    if (echttp_isdebug()) fprintf (stderr, "found %d devices\n", count);

    // Allocate, or reallocate without loosing what we already have.
    //
    int needed = count + 16;
    if (needed > DevicesSpace) {
        Devices = realloc (Devices, needed * sizeof(struct DeviceMap));
        if (!Devices) {
            DevicesSpace = DevicesCount = 0;
            return "no more memory";
        }
        DevicesSpace = needed;
    }

    int i;
    for (i = 0; i < count; ++i) {
        int device = houseconfig_array_object (devices, i);
        if (device <= 0) continue;
        const char *name = houseconfig_string (device, ".name");
        const char *id = houseconfig_string (device, ".id");
        const char *model = houseconfig_string (device, ".model");
        if ((!id) || (!name) || (!model)) continue;
        int idx = housetuya_device_id_search (id);
        if (idx < 0) {
            idx = housetuya_device_add (name, id, model);
        } else {
            housetuya_device_refresh_string (&(Devices[idx].name), name);
        }
        housetuya_device_refresh_string (&(Devices[idx].secret.key),
                                         houseconfig_string (device, ".key"));
        housetuya_device_refresh_string (&(Devices[idx].description),
                                         houseconfig_string (device, ".description"));
        if (echttp_isdebug()) fprintf (stderr, "load device %s, ID %s%s\n", Devices[idx].name, Devices[idx].secret.id);
        housetuya_device_reset (idx, Devices[idx].status);
    }
    return 0;
}

void housetuya_device_live_config (ParserContext context, int top) {

    int items = echttp_json_add_array (context, top, "devices");

    int i;
    for (i = 0; i < DevicesCount; ++i) {
        int device = echttp_json_add_object (context, items, 0);
        if (Devices[i].name && Devices[i].name[0])
            echttp_json_add_string (context, device, "name", Devices[i].name);
        if (Devices[i].secret.id && Devices[i].secret.id[0])
            echttp_json_add_string (context, device, "id", Devices[i].secret.id);
        if (Devices[i].model && Devices[i].model[0])
            echttp_json_add_string (context, device, "model", Devices[i].model);
        if (Devices[i].host && Devices[i].host[0])
            echttp_json_add_string (context, device, "host", Devices[i].host);
        if (Devices[i].secret.key && Devices[i].secret.key[0])
            echttp_json_add_string (context, device, "key", Devices[i].secret.key);
        if (Devices[i].description && Devices[i].description[0])
            echttp_json_add_string (context, device, "description", Devices[i].description);
    }
}

const char *housetuya_device_initialize (int argc, const char **argv) {
    housetuya_device_discovery_sockets ();
    return 0;
}

