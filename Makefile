# Thin Makefile wrapper around the meson build, so `make` / `make install`
# still work as usual even though meson+ninja do the actual building.
BUILDDIR   ?= build
RELEASEDIR ?= release-build
PREFIX     ?= /usr/local
.PHONY: all build devel clean distclean install uninstall run reconfigure

all: build

# Create an optimized release version
release:
	@if [ ! -d $(RELEASEDIR) ]; then \
		meson setup $(RELEASEDIR) --prefix=$(PREFIX) --buildtype=release; \
	fi
	meson compile -C "$(RELEASEDIR)"

# (Re-)create a development build directory only if it doesn't exist yet,
# then compile.
build:
	@if [ ! -d $(BUILDDIR) ]; then \
		meson setup $(BUILDDIR) --buildtype=debug; \
	fi
	meson compile -C $(BUILDDIR)

# Force meson to regenerate the build directory (e.g. after editing
# meson.build).
reconfigure:
	meson setup $(BUILDDIR) --reconfigure

install: release
	meson install -C $(RELEASEDIR)

uninstall:
	ninja -C $(RELEASEDIR) uninstall

run: devel
	$(BUILDDIR)/src/vlp $(ARGS)

clean:
	@if [ -d "$(BUILDDIR)" ]; then meson compile -C "$(BUILDDIR)" --clean; fi
	@if [ -d "$(RELEASEDIR)" ]; then meson compile -C "$(RELEASEDIR)" --clean; fi

distclean:
	rm -rf $(BUILDDIR)
	rm -rf $(RELEASEDIR)
