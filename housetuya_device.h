/* HouseWiz - A simple home web server for control of Philips Wiz devices.
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
 * housetuya_device.h - An implementation of the Tuya protocol.
 *
 */
const char *housetuya_device_initialize (int argc, const char **argv);
const char *housetuya_device_refresh (void);

int housetuya_device_changed (void);

int housetuya_device_count (void);
const char *housetuya_device_name (int point);

void housetuya_device_live_config (ParserContext context, int top);

const char *housetuya_device_failure (int point);

int    housetuya_device_commanded (int point);
time_t housetuya_device_deadline  (int point);
int    housetuya_device_get       (int point);
int    housetuya_device_set       (int point, int state, int pulse);

void housetuya_device_periodic (time_t now);

