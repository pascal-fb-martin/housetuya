/* tuyacmd - A simple program to control a Tuya device
 *
 * Copyright 2020, Pascal Martin
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
 * tuyacmd.c - control a Tuya device
 *
 * SYNOPSYS:
 *
 * tuyacmd
 * tuyacmd <host> <id> <key> [type]
 * tuyacmd <host> <id> <key> [type] on [<version>]
 * tuyacmd <host> <id> <key> [type] off [<version>]
 *
 * Supported commands are: 
 *    alias: change the alias name configured in the device.
 *    on:    turn the device on.
 *    off:   turn the device off.
 *
 * With no parameter specified, the program listens to the devies broadcast
 * to discover all devices present, and prints system information from all
 * responding devices.
 *
 * With no command specified, the program requests and prints status
 * information for the specified device.
 *
 * In both cases the tool prints the JSON messages from the device.
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "housetuya_messages.h"
#include "housetuya_crypto.h"


static int TuyaUdpPort[2] = {6666, 6667};
static int TuyaTcpPort = 6668;
static int TuyaUdpSocket[2] = {-1, -1};

static int Debug = 0;

int housetuya_isdebug (void) {
    return Debug;
}

#define DEBUG if (Debug) printf

static int tuyacmd_connect (const char *host) {
    int s = -1;
    char service[12];
    struct addrinfo hints = {0};
    struct addrinfo *resolved;
    struct addrinfo *cursor;
    hints.ai_flags = 0;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf (service, sizeof(service), "%d", TuyaTcpPort);
    DEBUG ("Connecting to %s (port %s)\n", host, service);
    int status = getaddrinfo (host, service, &hints, &resolved);
    if (status) {
        printf ("** cannot resolve %s: %s\n", host, (status == EAI_SYSTEM)? strerror(errno):gai_strerror(status));
        return -1;
    }

    for (cursor = resolved; cursor; cursor = cursor->ai_next) {
        if (cursor->ai_family != AF_INET) continue;
        s = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (s < 0) continue;
        if (connect(s, cursor->ai_addr, cursor->ai_addrlen) != 0) {
            printf ("** connection to %s failed: %s\n", host, strerror(errno));
            close (s);
            s = -1;
            continue;
        }
        DEBUG ("Connected to %s\n", host);
        break;
    }
    freeaddrinfo(resolved);
    if (s < 0) printf ("** cannot connect to %s\n", host);
    return s;
}

static int tuyacmd_wait(int s) {

    fd_set receive;
    struct timeval timeout;

    FD_ZERO(&receive);
    FD_SET(s, &receive);

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    return select (s+1, &receive, 0, 0, &timeout);
}

static void tuyacmd_receive (int s, const TuyaSecret *secret, int expected) {

    for (;;) {
        if (tuyacmd_wait(s) <= 0) {
            printf ("** No response.\n");
            return;
        }
        char buffer[1024];
        char coded[1024];
        int code;
        int sequence;
        int size = read (s, coded, sizeof(coded));
        if (size <= 0) {
            printf ("** Empty response.\n");
            return;
        }
        size = housetuya_extract (buffer, sizeof(buffer), secret, &code, &sequence, coded, size);
        if (size <= 0) continue;
        buffer[size] = 0;
        printf ("Response: %s\n", buffer);
        if (code == expected) {
            DEBUG ("Expected code %d received\n", code);
            return;
        }
    }
}

static void tuyacmd_command (int s, const TuyaSecret *secret, int dps, int value) {
    char command[1024];
    int length =
        housetuya_control (command, sizeof(command), secret, 0, dps, value);

    int sent = write (s, command, length);
    if (sent < 0) {
        printf ("** send() error: %s\n", strerror(errno));
        exit(1);
    }
    tuyacmd_receive (s, secret, TUYA_STATUS);
}

static void tuyacmd_refresh (int s, const TuyaSecret *secret) {
    char command[1024];
    int length = housetuya_query (command, sizeof(command), secret, 0);

    int sent = write (s, command, length);
    if (sent < 0) {
        printf ("** send() error: %s\n", strerror(errno));
        exit(1);
    }

    tuyacmd_receive (s, secret, TUYA_QUERY);
}


static void tuyacmd_discovery_sockets (void) {

    int i;
    struct sockaddr_in listenaddr;

    listenaddr.sin_family = AF_INET;
    listenaddr.sin_addr.s_addr = INADDR_ANY;

    for (i = 0; i < 2; ++i) {
        listenaddr.sin_port = htons(TuyaUdpPort[i]);
        TuyaUdpSocket[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (TuyaUdpSocket[i] < 0) {
            printf ("** cannot open UDP socket for port %d: %s\n",
                    TuyaUdpPort[i], strerror(errno));
            exit(1);
        }

        if (bind(TuyaUdpSocket[i],
                 (struct sockaddr *)(&listenaddr), sizeof(listenaddr)) < 0) {
            printf ("** cannot bind to port %d: %s\n",
                    TuyaUdpPort[i], strerror(errno));
            exit(1);
        }

        int value = 1;
        if (setsockopt(TuyaUdpSocket[i], SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) < 0) {
                printf ("** cannot broadcast: %s\n", strerror(errno));
            exit(1);
        }
        DEBUG ("UDP socket port %d is ready.\n", TuyaUdpPort[i]);
    }
}

static int tuyacmd_discovery (time_t wait) {

    char input[1025];
    char buffer[1025];
    int code;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    static TuyaSecret DiscoverySecret = {0};
    if (!DiscoverySecret.key)
        DiscoverySecret.key = strdup(housetuya_discoverykey());

    fd_set receive;
    struct timeval timeout;

    FD_ZERO(&receive);
    FD_SET(TuyaUdpSocket[0], &receive);
    FD_SET(TuyaUdpSocket[1], &receive);

    timeout.tv_sec = wait;
    timeout.tv_usec = 0;

    int ready = select (TuyaUdpSocket[1]+1, &receive, 0, 0, &timeout);
    if (ready <= 0) return ready;

    int size = 0;
    if (FD_ISSET(TuyaUdpSocket[0], &receive)) {
        DEBUG ("Received data on port %d\n", TuyaUdpPort[0]);
        size = recvfrom (TuyaUdpSocket[0], input, sizeof(input), 0,
                         (struct sockaddr *)(&addr), &addrlen);
        size = housetuya_extract (buffer, sizeof(buffer), 0, &code, 0, input, size);
    }
    else if (FD_ISSET(TuyaUdpSocket[1], &receive)) {
        DEBUG ("Received data on port %d\n", TuyaUdpPort[1]);
        size = recvfrom (TuyaUdpSocket[1], input, sizeof(input), 0,
                         (struct sockaddr *)(&addr), &addrlen);
        size = housetuya_extract (buffer, sizeof(buffer), &DiscoverySecret, &code, 0, input, size);
    } else {
        printf ("** no socket received anything?\n");
        return 1;
    }
    if (size == 0) {
        printf ("** no data?\n");
        return 1;
    }
    if (size < 0) {
        printf ("** recvfrom() error: %s\n", strerror(errno));
        return 1;
    }
    buffer[size] = 0;
    int ip = htonl(addr.sin_addr.s_addr);
    printf ("Message from %d.%d.%d.%d: %s\n",
            0xff & (ip >> 24), 0xff & (ip >> 16),
            0xff & (ip >> 8), 0xff & ip, buffer);
    return 1;
}

int main (int argc, char **argv) {

    TuyaSecret secret;

    const char *host = 0;
    const char *id = 0;
    const char *key = 0;
    const char *type = 0;
    const char *cmd = 0;
    const char *version = "3.3";
    int dps = 20;

    char buffer[256];


    secret.version = "3.3";

    if (argc >= 2) {
       if (!strcmp(argv[1], "-h")) {
           printf ("tuyacmd                     : scan for devices.\n");
           printf ("tuyacmd host id key on/off  : turn device on/off.\n");
           return 0;
       }
       if (!strcmp(argv[1], "-d")) {
           Debug = 1;
           argv += 1;
           argc -= 1;
       }
    }

    if (argc > 3) {
       host = argv[1];
       secret.id = argv[2];
       secret.key = argv[3];
       if (argc > 4) {
           cmd = argv[4];
           if (strcmp(cmd, "get") && strcmp(cmd, "on") && strcmp(cmd, "off")) {
               if (argc <= 5) {
                   printf ("** invalid command %s\n", cmd);
                   exit(1);
               }
               type = cmd;
               cmd = argv[5];
               argc--;
               argv++;
           }
           if (argc > 5) {
               secret.version = argv[5];
           }
       }
    }
    
    if (type) {
        static struct {
            const char *name;
            int  dps;
        } DpsMap[] = {{"bulb", 20}, {"light", 20}, {"switch", 1}, {0, 0}};

        int i;
        for (i = 0; DpsMap[i].name; ++i) {
            if (strcmp(DpsMap[i].name, type)) continue;
            dps = DpsMap[i].dps;
            break;
        }
        if (!DpsMap[i].name) {
            printf ("** Invalid device type %s\n", type);
            exit(1);
        }
    }

    if (!cmd) {
        tuyacmd_discovery_sockets ();
        time_t start = time(0);
        while (tuyacmd_discovery(5-(time(0)-start)) > 0) ;
        return 0;
    } else if ((!strcmp (cmd, "on")) || (!strcmp (cmd, "off"))) {
        int s = tuyacmd_connect (host);
        if (s < 0) exit(1);
        tuyacmd_command (s, &secret, dps, !strcmp (cmd, "on"));
        return 0;
    } else if (!strcmp (cmd, "get")) {
        int s = tuyacmd_connect (host);
        if (s < 0) exit(1);
        tuyacmd_refresh (s, &secret);
        return 0;
    }
    printf ("** Invalid command.\n");
    return 1;
}

