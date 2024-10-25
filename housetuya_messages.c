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
 * housetuya_messages.c - Library to encode and decode Tuya messages.
 *
 * SYNOPSYS:
 *
 * typedef struct {
 *     char *id;
 *     char *key;
 *     char *version;
 * } TuyaSecret;
 *
 * int housetuya_control (char *buffer, int size, const TuyaSecret *access,
 *                        int sequence, int dps, int value);
 *
 *    Prepare a control message in buffer, return its length (or 0 on error).
 *
 * int housetuya_query (char *buffer, int size, const TuyaSecret *access,
 *                      int sequence);
 *
 *    Prepare a query message in buffer, return its length (or 0 on error).
 *
 * int housetuya_extract (char *buffer, int size, const TuyaSecret *access,
 *                        int *code, int *sequence, const char *raw, int length);
 *
 *    Extract the JSON payload from the specified message and return its
 *    length, or 0 on error.
 *
 *    The JSON payload is still to be decoded and interpreted according to
 *    the value of code.
 *
 * PROTOCOL:
 *
 * The supported versions of the Tuya protocol are: 3.1, 3.3, 3.4
 *
 * If no version is specified, the program uses 3.3. It might not always work.
 *
 * Tuya packets format (here, 'data' is encrypted JSON):
 *
 * 3.2 & 3.3 command packets:
 *
 *  prefix(4), seq(4), cmd(4), length(4), ["3.3"(15)], data, crc(4), suffix(4)
 *                                        ^--(optional extended header)
 *
 * (No "3.3"(15) header for QUERY or REFRESH.)
 *
 * 3.2 & 3.3 response packets:
 *
 *  prefix(4), seq(4), cmd(4), length(4), [code(4)], data, crc(4), suffix(4)
 *
 * 3.4 command packets:
 *
 *  prefix(4), seq(4), cmd(4), length(4), ['3.4'(15)], data, hmac(32), suffix(4)
 *                                                           --------
 *
 * (No "3.4"(15) header for QUERY or REFRESH.)
 *
 * 3.4 response packets:
 *
 *  prefix(4), seq(4), cmd(4), length(4), [code(4)], data, hmac(32), suffix(4)
 *                                                         --------
 */

#include <time.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>

#include "housetuya.h"
#include "housetuya_crypto.h"
#include "housetuya_crc.h"
#include "housetuya_messages.h"


#define DEBUGDUMP if (housetuya_isdebug()) housetuya_dump
#define DEBUG     if (housetuya_isdebug()) printf

static void housetuya_dump (const char *intro, int ip, const char *data, int size) {
    if (!housetuya_isdebug()) return;
    char origin[200];
    if (ip) {
        snprintf (origin, sizeof(origin), " from %d.%d.%d.%d",
                  0xff & (ip >> 24), 0xff & (ip >> 16), 0xff & (ip >> 8), 0xff & ip);
    } else origin[0] = 0;

    char hex[1024];
    int i;
    int cursor = 0;
    for (i = 0; i < size; ++i) {
        cursor += snprintf (hex+cursor, sizeof(hex)-cursor, "%02x", data[i]);
        if (cursor >= sizeof(hex)) break;
    }
    printf ("%s%s, length %d: %s\n", intro, origin, size, hex);
}


static int housetuya_start_envelop (char *buffer, int sequence, int code) {
    int *header = (int *)buffer;
    header[0] = htonl(0x000055aa);
    header[1] = htonl(sequence);
    header[2] = htonl(code);

    return 16;
}

static int housetuya_end_envelop_pre34 (char *buffer, int length) {
    int *header = (int *)buffer;
    header[3] = htonl(length-8); // Skip 16 bytes header, add 8 bytes trailer.
    int *trailer = (int *)(buffer+length);
    trailer[0] = htonl(housetuya_crc (buffer+16, length-16));
    trailer[1] = htonl(0x0000aa55);
    return length + 8;
}

static int housetuya_encode (char *buffer, int size, const TuyaSecret *access,
                             int code, int sequence, char *data) {

    int length = strlen(data);
    if (length > size-15) {
        printf ("** Data too large to encode: %d (max %d)\n", length, size-15);
        return 0;
    }
    // TBD: no 3.4 support, only versions 3.2 & 3.3 for now.
    //
    int cursor = housetuya_start_envelop (buffer, sequence, code);
    if ((code != TUYA_QUERY) && (code != TUYA_UPDATE)) {
        // REFRESH and QUERY have no extended header. Others do.
        memset (buffer+cursor, 0, 15);
        strncpy (buffer+cursor, access->version, 15);
        cursor += 15;
    }
    length = cursor + housetuya_encrypt (access->key, buffer+cursor, data, length);
    length = housetuya_end_envelop_pre34 (buffer, length);
    housetuya_dump ("Encrypted command", 0, buffer, length);
    return length;
}

int housetuya_control (char *buffer, int size, const TuyaSecret *access,
                       int sequence, int dps, int value) {

    static const char Format[] =
        "{\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%d\",\"dps\":{\"%d\":%s}}";
    char command[1024];
    snprintf (command, sizeof(command), Format,
              access->id, access->id, (int)time(0), dps, value?"true":"false");
    DEBUG ("Command: %s\n", command);
    return housetuya_encode (buffer, size, access, TUYA_CONTROL, sequence, command);
}

int housetuya_query (char *buffer, int size, const TuyaSecret *access,
                     int sequence) {

    static const char Format[] =
        "{\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%d\"}";
    char command[1024];
    snprintf (command, sizeof(command), Format,
              access->id, access->id, (int)time(0));
    return housetuya_encode (buffer, size, access, TUYA_QUERY, sequence, command);
}

static int housetuya_open_envelop (const char *version,
                                   const char *buffer, int length,
                                   const char **data, int *code, int *sequence) {
    const int *header = (const int *)buffer;
    int prefix = ntohl(header[0]);
    if (prefix != 0x000055aa) {
        DEBUG ("** invalid prefix %04x\n", prefix);
        return 0;
    }
    if (sequence) *sequence = ntohl(header[1]);
    if (code) *code = ntohl(header[2]);
    int payload_length = ntohl(header[3]);
    if (payload_length != length - 16) {
        DEBUG ("** invalid length %d (expected %d)\n", payload_length, length - 16);
        return 0;
    }
    const int *trailer = (const int *)(buffer + length - 8);
    int suffix = ntohl(trailer[1]);
    if (suffix != 0x0000aa55) {
        DEBUG ("** invalid suffix %04x\n", suffix);
        return 0;
    }

    // Do not check the CRC for now: the CRC of commands does not even seem
    // to be checked by the devices (makes sense: UDP and TCP data is already
    // protected by at least two layers of CRC).

    // Apparently some messages might not have a return code?
    // Return codes are always in the range 0..255. We ignore them for now..
    if (ntohl(header[4]) & 0xffffff00) {
        *data = buffer + 16;
        length -= (16 + 8);
    } else {
        *data = buffer + 20; // + return code.
        length -= (20 + 8);
    }

    // some (most, actually) messages have an extended header
    if (version && (!strcmp(version, *data))) {
        length -= 15;
        DEBUG ("Found extended header for version %s, length = %d\n", *data, length);
        *data += 15;
    }
    return length;
}

int housetuya_extract (char *buffer, int size, const TuyaSecret *secret,
                       int *code, int *sequence, const char *raw, int length) {

    if (length <= 0) {
        DEBUG ("** Empty response.\n");
        return 0;
    }
    DEBUGDUMP ("Raw data received", 0, raw, length);

    const char *data = 0;
    int datalen;

    if (secret) {
        datalen = housetuya_open_envelop (secret->version, raw, length, &data, code, sequence);
        if (datalen <= 0) return 0;
        datalen = housetuya_decrypt (secret->key, data, buffer, datalen);
        if (datalen <= 0) return 0;
    } else {
        datalen = housetuya_open_envelop (0, raw, length, &data, code, sequence);
        memcpy (buffer, data, datalen);
    }
    DEBUGDUMP ("Decoded data received", 0, buffer, datalen);
    if (datalen < size) buffer[datalen] = 0;
    return datalen;
}

