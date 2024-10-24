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
 * housetuya_model.h - Handle the database of Tuya products.
 */

const char *housetuya_model_initialize (int argc, const char **argv);
int housetuya_model_changed (void);
void housetuya_model_live_config (ParserContext context, int top);
const char *housetuya_model_refresh (void);
const char *housetuya_model_get_name (const char *id);
int housetuya_model_get_control (const char *id);
