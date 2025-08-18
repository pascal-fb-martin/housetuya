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
HCAT=automation

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

install-runtime: install-preamble
	$(INSTALL) -m 0755 -s housetuya tuyacmd $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housetuya

install-app: install-ui install-runtime

uninstall-app:
	rm -f $(DESTDIR)$(prefix)/bin/housetuya
	rm -f $(DESTDIR)$(prefix)/bin/tuyacmd
	rm -rf $(DESTDIR)$(SHARE)/public/tuya

purge-app:

purge-config:
	rm -f $(DESTDIR)/etc/house/tuya.config
	rm -f $(DESTDIR)/etc/default/housetuya

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package:
	rm -rf build
	install -m 0755 -d build/$(HAPP)/DEBIAN
	cat debian/control | sed "s/{{arch}}/`dpkg --print-architecture`/" > build/$(HAPP)/DEBIAN/control
	install -m 0644 debian/copyright build/$(HAPP)/DEBIAN
	install -m 0644 debian/changelog build/$(HAPP)/DEBIAN
	install -m 0755 debian/postinst build/$(HAPP)/DEBIAN
	install -m 0755 debian/prerm build/$(HAPP)/DEBIAN
	install -m 0755 debian/postrm build/$(HAPP)/DEBIAN
	make DESTDIR=build/$(HAPP) install-package
	cd build/$(HAPP) ; find etc -type f | sed 's/etc/\/etc/' > DEBIAN/conffiles
	cd build ; fakeroot dpkg-deb -b $(HAPP) .

# System installation. ------------------------------------------

include $(SHARE)/install.mak

