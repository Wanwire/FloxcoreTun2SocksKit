
HEV = hev-socks5-tunnel-2.7.5
PLATFORM ?= 

default: libhev

libhev:
	make -C $(HEV) -f ../tools/MakefileFramework $(PLATFORM)

clean:
	@git checkout $(HEV)
	@git clean -dxf $(HEV)

install:

.PHONY: clean install build libhev
