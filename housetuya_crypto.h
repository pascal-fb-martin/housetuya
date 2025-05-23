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
 * housetuya_crypto.h - Cryptographic support for the Tuya protocol
 */
const char *housetuya_discoverykey (void);

int housetuya_encrypt (const unsigned char *key,
                       char *encrypted, char *clear, int length);
int housetuya_decrypt (const unsigned char *key,
                       const char *encrypted, char *clear, int length);

