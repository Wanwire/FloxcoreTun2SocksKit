
HEV = hev-socks5-tunnel-2.7.0
PLATFORM ?= 

default: libhev

libhev: patched
	make -C $(HEV) -f ../tools/MakefileFramework $(PLATFORM)

patched:
	@for patchfile in patches/libhev*.patch; do \
		git apply --check $$patchfile; \
		if [ $$? == 0 ]; then \
			git apply --whitespace=fix $$patchfile || true; \
		fi; \
	done;
	touch $(HEV)/patched

clean:
	@git checkout $(HEV)
	@git clean -dxf $(HEV)

install:

.PHONY: clean install build libhev
