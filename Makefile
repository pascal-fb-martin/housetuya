
OBJS= housetuya.o housetuya_model.o housetuya_device.o housetuya_messages.o housetuya_crypto.o housetuya_crc.o
LIBOJS=

SHARE=/usr/local/share/house

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

install:
	if [ -e /etc/init.d/housetuya ] ; then systemctl stop housetuya ; fi
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/housetuya /etc/init.d/housetuya
	cp housetuya tuyacmd /usr/local/bin
	cp init.debian /etc/init.d/housetuya
	chown root:root /usr/local/bin/housetuya /usr/local/bin/tuyacmd /etc/init.d/housetuya
	chmod 755 /usr/local/bin/housetuya /usr/local/bin/tuyacmd /etc/init.d/housetuya
	mkdir -p $(SHARE)/public/tuya
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/tuya
	cp public/* $(SHARE)/public/tuya
	chown root:root $(SHARE)/public/tuya/*
	chmod 644 $(SHARE)/public/tuya/*
	touch /etc/default/housetuya
	systemctl daemon-reload
	systemctl enable housetuya
	systemctl start housetuya

uninstall:
	systemctl stop housetuya
	systemctl disable housetuya
	rm -f /usr/local/bin/housetuya /usr/local/bin/tuyacmd /etc/init.d/housetuya
	rm -rf $(SHARE)/public/tuya
	systemctl daemon-reload

purge: uninstall
	rm -rf /etc/house/tuya.config /etc/default/housetuya

