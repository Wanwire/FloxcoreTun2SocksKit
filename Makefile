
DESTDIR ?= build
PACKAGE ?= Tun2SocksKit
PLATFORM ?= all

default: $(DESTDIR)/$(PACKAGE)

$(DESTDIR)/$(PACKAGE): export DESTDIR := $(CURDIR)/$(DESTDIR)
$(DESTDIR)/$(PACKAGE): $(DESTDIR)/HevSocks5Tunnel.xcframework

$(DESTDIR)/HevSocks5Tunnel.xcframework: export PLATFORM := $(PLATFORM)
$(DESTDIR)/HevSocks5Tunnel.xcframework: export DESTDIR := $(CURDIR)/$(DESTDIR)
$(DESTDIR)/HevSocks5Tunnel.xcframework:
	$(MAKE) -C thirdparty libhev

local: $(DESTDIR)/$(PACKAGE)
	rm -rf "$(DESTDIR)/$(PACKAGE)"
	mkdir -p "$(DESTDIR)/$(PACKAGE)/xcframeworks"
	cp -r "$(DESTDIR)/HevSocks5Tunnel.xcframework" "$(DESTDIR)/$(PACKAGE)/xcframeworks/"
	cp "Templates/Package.local.template" "$(DESTDIR)/$(PACKAGE)/Package.swift"
	cp README.md "$(DESTDIR)/$(PACKAGE)/"

clean:
	$(MAKE) -C thirdparty clean
	@rm -rf "$(DESTDIR)"

.PHONY: all clean local
