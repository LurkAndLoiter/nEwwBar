DEBUG ?= 0

CFLAGS = -Wall -g

ifeq ($(DEBUG), 1)
    CFLAGS += -DDEBUG
endif

CC=gcc $(CFLAGS)

all: audio_in audio_out bluetooth_adapter bluetooth_connect bluetooth_devices bluetooth_scanner date_simple mpris_fetch mpris_position wlan_monitor wlan_scan workspace_focus workspace_list 

audio_in: audio_in.c 
	$(CC) -o ../bin/audio_in audio_in.c `pkg-config --libs libpulse`

audio_out: audio_out.c
	$(CC) -o ../bin/audio_out audio_out.c `pkg-config --libs libpulse`

bluetooth_adapter: bluetooth_adapter.c
	$(CC) -o ../bin/bluetooth_adapter bluetooth_adapter.c `pkg-config --cflags --libs glib-2.0 gio-2.0 json-glib-1.0`

bluetooth_connect: bluetooth_connect.c 
	$(CC) -o ../bin/bluetooth_connect bluetooth_connect.c -ldbus-1 `pkg-config --cflags --libs dbus-1`

bluetooth_devices: bluetooth_devices.c
	$(CC) -o ../bin/bluetooth_devices bluetooth_devices.c `pkg-config --cflags --libs dbus-1`

bluetooth_scanner: bluetooth_scanner.c
	$(CC) -o ../bin/bluetooth_scanner bluetooth_scanner.c `pkg-config --cflags --libs dbus-1`

date_simple: date_simple.c 
	$(CC) -o ../bin/date_simple date_simple.c

mpris_fetch: mpris_fetch.c
	$(CC) -o ../bin/mpris_fetch mpris_fetch.c `pkg-config --cflags json-glib-1.0 --libs glib-2.0 playerctl json-glib-1.0 libpulse libpulse-mainloop-glib` 

mpris_position: mpris_position.c
	$(CC) -o ../bin/mpris_position mpris_position.c `pkg-config --cflags json-glib-1.0 --libs playerctl` 

wlan_monitor: wlan_monitor.c 
	$(CC) -o ../bin/wlan_monitor wlan_monitor.c `pkg-config --cflags --libs gio-2.0`

wlan_scan: wlan_scan.c 
	$(CC) -o ../bin/wlan_scan wlan_scan.c `pkg-config --cflags --libs dbus-1`

workspace_focus: workspace_focus.c 
	$(CC) -o ../bin/workspace_focus workspace_focus.c

workspace_list: workspace_list.c
	$(CC) -o ../bin/workspace_list workspace_list.c

clean:
	[ -f ../bin/audio_in ] && rm ../bin/audio_in || true
	[ -f ../bin/audio_out ] && rm ../bin/audio_out || true
	[ -f ../bin/bluetooth_adapter ] && rm ../bin/bluetooth_adapter || true
	[ -f ../bin/bluetooth_connect ] && rm ../bin/bluetooth_connect || true
	[ -f ../bin/bluetooth_devices ] && rm ../bin/bluetooth_devices || true
	[ -f ../bin/bluetooth_scanner ] && rm ../bin/bluetooth_scanner || true
	[ -f ../bin/date_simple ] && rm ../bin/date_simple || true  
	[ -f ../bin/mpris_fetch ] && rm ../bin/mpris_fetch || true 
	[ -f ../bin/mpris_position ] && rm ../bin/mpris_position || true
	[ -f ../bin/wlan_monitor ] && rm ../bin/wlan_monitor || true  
	[ -f ../bin/wlan_scan ] && rm ../bin/wlan_scan || true  
	[ -f ../bin/workspace_focus ] && rm ../bin/workspace_focus || true  
	[ -f ../bin/workspace_list ] && rm ../bin/workspace_list || true  
