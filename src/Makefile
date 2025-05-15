DEBUG ?= 0

CFLAGS = -Wall -g

ifeq ($(DEBUG), 1)
    CFLAGS += -DDEBUG
endif

CC=gcc $(CFLAGS)

all: audio_in audio_out bluetooth_active bluetooth_devices date_simple mpris_combined mpris_fetch mpris_position wlan_monitor wlan_scan workspace_focus workspace_list 

audio_in: audio_in.c 
	$(CC) -o ../bin/audio_in audio_in.c `pkg-config --libs libpulse`

audio_out: audio_out.c
	$(CC) -o ../bin/audio_out audio_out.c `pkg-config --libs libpulse`

bluetooth_active: bluetooth_active.c
	$(CC) -o ../bin/bluetooth_active bluetooth_active.c `pkg-config --cflags --libs dbus-1`

bluetooth_devices: bluetooth_devices.c
	$(CC) -o ../bin/bluetooth_devices bluetooth_devices.c `pkg-config --cflags --libs dbus-1`

date_simple: date_simple.c 
	$(CC) -o ../bin/date_simple date_simple.c

mpris_combined: mpris_combined.c 
	$(CC) -o ../bin/mpris_combined mpris_combined.c `pkg-config --cflags dbus-1 --libs dbus-1 libpulse`
mpris_fetch: mpris_fetch.c
	$(CC) -o ../bin/mpris_fetch mpris_fetch.c `pkg-config --cflags json-glib-1.0 --libs glib-2.0 playerctl json-glib-1.0 libpulse libpulse-mainloop-glib` 

mpris_position: mpris_position.c
	$(CC) -o ../bin/mpris_position mpris_position.c `pkg-config --cflags json-glib-1.0 --libs playerctl` 

wlan_monitor: wlan_monitor.c 
	# dependency for compiling 'gio' packaged in 'glib2'
	$(CC) -o ../bin/wlan_monitor wlan_monitor.c `pkg-config --cflags --libs gio-2.0`

wlan_scan: wlan_scan.c 
	$(CC) -o ../bin/wlan_scan wlan_scan.c `pkg-config --cflags --libs dbus-1`

workspace_focus: workspace_focus.c 
	# dependency for compilation 'json-c'
	$(CC) -o ../bin/workspace_focus workspace_focus.c `pkg-config --libs json-c`

workspace_list: workspace_list.c
	#dependency for compilation 'json-c'
	$(CC) -o ../bin/workspace_list workspace_list.c `pkg-config --libs json-c`

clean:
	[ -f ../bin/audio_in ] && rm ../bin/audio_in || true
	[ -f ../bin/audio_out ] && rm ../bin/audio_out || true
	[ -f ../bin/bluetooth_active ] && rm ../bin/bluetooth_active || true
	[ -f ../bin/bluetooth_devices ] && rm ../bin/bluetooth_devices || true
	[ -f ../bin/date_simple ] && rm ../bin/date_simple || true  
	[ -f ../bin/mpris_combined ] && rm ../bin/mpris_combined || true  
	[ -f ../bin/mpris_fetch ] && rm ../bin/mpris_fetch || true 
	[ -f ../bin/mpris_position ] && rm ../bin/mpris_position || true
	[ -f ../bin/wlan_monitor ] && rm ../bin/wlan_monitor || true  
	[ -f ../bin/wlan_scan ] && rm ../bin/wlan_scan || true  
	[ -f ../bin/workspace_focus ] && rm ../bin/workspace_focus || true  
	[ -f ../bin/workspace_list ] && rm ../bin/workspace_list || true  
