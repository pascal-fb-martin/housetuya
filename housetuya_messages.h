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
 * housetuya_messages.h - Library to encode and decode Tuya messages.
 */

typedef struct {
    char *id;
    char *key;
    char *version;
} TuyaSecret;

#define TUYA_STATUS     8
#define TUYA_CONTROL    7
#define TUYA_QUERY     10
#define TUYA_UPDATE    18

int housetuya_control (char *buffer, int size, const TuyaSecret *access,
                       int sequence, int dps, int value);

int housetuya_query (char *buffer, int size, const TuyaSecret *access,
                     int sequence);

int housetuya_extract (char *buffer, int size, const TuyaSecret *access,
                       int *code, int *sequence, const char *raw, int length);

