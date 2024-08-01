
VERSION = $(shell grep Version: deb/DEBIAN/control | cut -d' ' -f2)
# TODO: build module for all kernel versions
KVER ?= 6.1.21+
TARGET ?=  $(error TARGET not specified for deploy )
WSPDIR = $(shell dirname $(CURDIR) )
PKGNAME = mrhat-platform
KONAME = er-mrhat-plat

all: build/$(PKGNAME)_$(VERSION)-1_armhf.deb
	@true

build/$(PKGNAME)_$(VERSION)-1_armhf.deb : driver build/$(PKGNAME).dtbo deb/DEBIAN/*
	mkdir -p build
	mkdir -p deb/lib/modules/$(KVER)
	mkdir -p deb/boot/overlays/
	cp build/$(PKGNAME).dtbo deb/boot/overlays/
	dpkg-deb --root-owner-group --build deb build/$(PKGNAME)_$(VERSION)-1_armhf.deb

deb/lib/modules/$(KVER)/$(KONAME).ko: driver/*.c  driver/Makefile
	mkdir -p build
	mkdir -p deb/lib/modules/$(KVER)/
	rsync --delete -r  ./driver/ /tmp/$(WSPDIR)
	schroot -c buildroot -u root -d /tmp/$(WSPDIR) -- make KVER=$(KVER) BQCFLAGS=$(BQCFLAGS)
	cp /tmp/$(WSPDIR)/$(KONAME).ko deb/lib/modules/$(KVER)/$(KONAME).ko

driver: deb/lib/modules/$(KVER)/$(KONAME).ko
	@true

clean:
	rm -rf deb/boot/ deb/lib/ build/

build/$(PKGNAME).dts.pre: $(PKGNAME).dts
	mkdir -p build/
	cpp -nostdinc -undef -x assembler-with-cpp -I/var/chroot/buildroot/usr/src/linux-headers-$(KVER)/include -o build/$(PKGNAME).dts.pre $(PKGNAME).dts

build/$(PKGNAME).dtbo: build/$(PKGNAME).dts.pre
	mkdir -p build/
	dtc  -I dts -O dtb -o build/$(PKGNAME).dtbo build/$(PKGNAME).dts.pre

deploy: all
	rsync -e "ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null" -avhz --progress build/$(PKGNAME)_$(VERSION)-1_armhf.deb $(TARGET):/tmp/
	ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(TARGET) -- sudo dpkg -r $(PKGNAME)
	ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(TARGET) -- sudo dpkg -i /tmp/$(PKGNAME)_$(VERSION)-1_armhf.deb
	ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(TARGET) -- sudo sed -ri '/^\s*dtoverlay=$(PKGNAME)/d' /boot/config.txt
	ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(TARGET) -- "echo dtoverlay=$(PKGNAME) | sudo tee -a /boot/config.txt"

quickdeploy: driver
	scp deb/lib/modules/$(KVER)/$(KONAME).ko $(TARGET):/tmp/
	ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(TARGET) -- sudo cp /tmp/$(KONAME).ko /lib/modules/$(KVER)/
	ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(TARGET) -- "sudo rmmod $(KONAME) || true"
	ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(TARGET) -- "sudo modprobe $(KONAME) || true"
	

.PHONY: clean all deploy quickdeploy driver
