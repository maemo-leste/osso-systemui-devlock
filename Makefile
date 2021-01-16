PREFIX := /usr
INCLUDEDIR := $(PREFIX)/include
PACKAGES=osso-systemui hildon-1 gconf-2.0 dbus-1 libdevlock1 codelockui

all: libsystemuiplugin_devlock.so

clean:
	$(RM) libsystemuiplugin_devlock.so

install: libsystemuiplugin_devlock.so
	install -d $(DESTDIR)/usr/lib/systemui
	install -m 644 libsystemuiplugin_devlock.so $(DESTDIR)/usr/lib/systemui
	install -d $(DESTDIR)$(INCLUDEDIR)/systemui
	install devlock-dbus-names.h $(DESTDIR)$(INCLUDEDIR)/systemui


libsystemuiplugin_devlock.so: osso-systemui-devlock.c
	$(CC) $^ -o $@ -shared -Wl,--as-needed -fPIC $(shell pkg-config --libs --cflags $(PACKAGES)) -Wl,-soname -Wl,$@ -Wl,-rpath -Wl,/usr/lib/hildon-desktop $(CFLAGS) $(LDFLAGS)

.PHONY: all clean install
