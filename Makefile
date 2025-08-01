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
#
# WARNING
#       
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house
        
INSTALL=/usr/bin/install

HAPP=housetuya

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
	gcc -g -O -o housetuya $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lmagic -lmagic -lrt

tuyacmd: tuyacmd.c housetuya_messages.o housetuya_crypto.o housetuya_crc.o
	gcc -g -O -o tuyacmd tuyacmd.c housetuya_messages.o housetuya_crypto.o housetuya_crc.o -lssl -lcrypto -lrt

# Distribution agnostic file installation -----------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/tuya
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/tuya

install-app: install-ui
	$(INSTALL) -m 0755 -s housetuya tuyacmd $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housetuya

uninstall-app:
	rm -f $(DESTDIR)$(prefix)/bin/housetuya
	rm -f $(DESTDIR)$(prefix)/bin/tuyacmd
	rm -rf $(DESTDIR)$(SHARE)/public/tuya

purge-app:

purge-config:
	rm -f $(DESTDIR)/etc/house/tuya.config
	rm -f $(DESTDIR)/etc/default/housetuya

# System installation. ------------------------------------------

include $(SHARE)/install.mak

