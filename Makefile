# HouseTuya - A simple home web server for control of Tuya devices.
#
# Copyright 2024, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

HAPP=housetuya
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS= housetuya.o housetuya_model.o housetuya_device.o housetuya_messages.o housetuya_crypto.o housetuya_crc.o
LIBOJS=

all: housetuya tuyacmd

clean:
	rm -f *.o *.a housetuya tuyacmd

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housetuya: $(OBJS)
	gcc -g -O -o housetuya $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

tuyacmd: tuyacmd.c housetuya_messages.o housetuya_crypto.o housetuya_crc.o
	gcc -g -O -o tuyacmd tuyacmd.c housetuya_messages.o housetuya_crypto.o housetuya_crc.o -lssl -lcrypto -lrt

# Distribution agnostic file installation -----------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housetuya $(HROOT)/bin/tuyacmd
	cp housetuya tuyacmd $(HROOT)/bin
	chown root:root $(HROOT)/bin/housetuya $(HROOT)/bin/tuyacmd
	chmod 755 $(HROOT)/bin/housetuya $(HROOT)/bin/tuyacmd
	mkdir -p $(SHARE)/public/tuya
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/tuya
	cp public/* $(SHARE)/public/tuya
	chown root:root $(SHARE)/public/tuya/*
	chmod 644 $(SHARE)/public/tuya/*
	touch /etc/default/housetuya

uninstall-app:
	rm -f $(HROOT)/bin/housetuya $(HROOT)/bin/tuyacmd
	rm -rf $(SHARE)/public/tuya

purge-app:

purge-config:
	rm -rf /etc/house/tuya.config /etc/default/housetuya

# System installation. ------------------------------------------

include $(SHARE)/install.mak

