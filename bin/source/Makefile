CC=gcc

all: audio_in audio_out bluetooth_active bluetooth_devices date_simple mpris_combined mpris_fetch mpris_position wlan_monitor wlan_scan workspace_focus workspace_list 

audio_in: audio_in.c 
	$(CC) -o ../audio_in audio_in.c `pkg-config --libs libpulse`

audio_out: audio_out.c
	$(CC) -o ../audio_out audio_out.c `pkg-config --libs libpulse`

bluetooth_active: bluetooth_active.c
	$(CC) -o ../bluetooth_active bluetooth_active.c `pkg-config --cflags --libs dbus-1`

bluetooth_devices: bluetooth_devices.c
	$(CC) -o ../bluetooth_devices bluetooth_devices.c `pkg-config --cflags --libs dbus-1`

date_simple: date_simple.c 
	$(CC) -o ../date_simple date_simple.c

mpris_combined: mpris_combined.c 
	$(CC) -o ../mpris_combined mpris_combined.c `pkg-config --cflags dbus-1 --libs dbus-1 libpulse`
mpris_fetch: mpris_fetch.c
	$(CC) -o ../mpris_fetch mpris_fetch.c `pkg-config --cflags json-glib-1.0 --libs glib-2.0 playerctl json-glib-1.0 libpulse libpulse-mainloop-glib` 

mpris_position: mpris_position.c
	$(CC) -o ../mpris_position mpris_position.c `pkg-config --cflags json-glib-1.0 --libs playerctl` 

wlan_monitor: wlan_monitor.c 
	# dependency for compiling 'gio' packaged in 'glib2'
	$(CC) -o ../wlan_monitor wlan_monitor.c `pkg-config --cflags --libs gio-2.0`

wlan_scan: wlan_scan.c 
	$(CC) -o ../wlan_scan wlan_scan.c `pkg-config --cflags --libs dbus-1`

workspace_focus: workspace_focus.c 
	# dependency for compilation 'json-c'
	$(CC) -o ../workspace_focus workspace_focus.c `pkg-config --libs json-c`

workspace_list: workspace_list.c
	#dependency for compilation 'json-c'
	$(CC) -o ../workspace_list workspace_list.c `pkg-config --libs json-c`

clean:
	[ -f ../audio_in ] && rm ../audio_in || true
	[ -f ../audio_out ] && rm ../audio_out || true
	[ -f ../bluetooth_active ] && rm ../bluetooth_active || true
	[ -f ../bluetooth_devices ] && rm ../bluetooth_devices || true
	[ -f ../date_simple ] && rm ../date_simple || true  
	[ -f ../mpris_combined ] && rm ../mpris_combined || true  
	[ -f ../mpris_fetch ] && rm ../mpris_fetch || true 
	[ -f ../mpris_position ] && rm ../mpris_position || true
	[ -f ../wlan_monitor ] && rm ../wlan_monitor || true  
	[ -f ../wlan_scan ] && rm ../wlan_scan || true  
	[ -f ../workspace_focus ] && rm ../workspace_focus || true  
	[ -f ../workspace_list ] && rm ../workspace_list || true  
