
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

clean:
	$(MAKE) -C thirdparty clean
	@rm -rf "$(DESTDIR)"

.PHONY: all clean local
