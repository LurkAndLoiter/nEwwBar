# nEwwBar
### The Noob Bar: Nearly Exactly What We Became Accustomed to Recongize.

![20250521_02h03m01s_grim](https://github.com/user-attachments/assets/431da1e3-1c6f-4e1b-b79f-5ed7208ef8ca)

A low resource eww bar that intends to implement all the basics one might need from a tray bar. why doesn't my tray bar have all the functionality pavucontrol does? &lt;- This is our goal. what do you open often? why? let us know. We want to implement it.

>[!WARNING]
>You need to open-many as the panels are all revealers hidden under the bar.
>You MUST declare your monitor ID I'd encourage the name such as DP-1 HDMI-A-1
>Below is default index 0 monitor
>You can declare this as a list: monitor='["<primary>",1,0,"DP-1"]'
>
>```bash
>eww open-many audioPanel micPanel bluetoothPanel wifiPanel powerPanel bar --arg monitor=0
>```

> [!NOTE]
> Panels can be locked open by right-clicking on the revealer button.
>
> Revealer buttons have default actions assigned to left-click.
>
> Panels can become out of position on forced eww updates where sub-panels draw on top of the bar.
>
> In this scenario, you can reload them by scrolling up on the bar to trigger barMin.
>
> Then scroll down at the top of the screen (5px) to make barMin return into the bar. This is a good method to reload anything in the event of errors.

### Intall

- run `make` in cloned directory.
  - compiles C from src/  into bin/
  - generates svg assets with scripts/svgBuilder.sh

Dependencies ():
- Arch _pacman widget_
- Hyprland _workspaces widget_
- playerctl _audio widget_
- wireplumber _audio, mic widget_
- bluez _bluetooth widget_
- NetworkManager _wifi widget_
- libraries used in C (probably already installed)
  - pulseaudio
  - glib2
  - json-glib
  - dbus
  - playerctl

If you run into an issue with compiling or setup please write an issue report.
This is very much currently untested and a "works for me state." I really do
want it to be seemless for people so let me know if you encounter any issues.

![Image](https://github.com/user-attachments/assets/238131be-f35b-4808-82c1-7255507410d6)

It's very much a rough in at the moment. The roughly "complete" widgets are App Launcher, pacman, audio/media, and bluetooth.

I'm looking for people to go through integration hell(find the issues my setup doesn't) and collaborate to make something better. The intended audience isn't power users in the end this is meant to help new linux users have an easy intuitive bar. If you have ideas I'd love to hear about them.

Everything is event driven if you wanted to grab [some code](https://github.com/LurkAndLoiter/nEwwBar/tree/main/src) to use. There are some [very outdated shell versions](https://github.com/LurkAndLoiter/nEwwBar/tree/ShellBase/scripts/Archives) for those interested.


>[!WARNING]
>You need to be on git release as the yuck utilizes image property fill-svg not available on the 0.6.0 release.

## App Launcher
### Simply displays all apps and will launch them onclick. Currently supports user/system/flatpak desktop entries.
Have ideas for this side panel? let me know because I'm a bit at a loss with it. It's tacky and I hate it.

![20250516_22h09m23s_grim](https://github.com/user-attachments/assets/c6b3b35f-5e08-48d1-9d83-0af6d0a80983)

## Audio/Media
### Same functionality you can expect from pavucontrol.
- Control output's audio
- control player's audio
- switch players output device
- mute/unmute  players and outputs
- volume control players and outputs
- set default audio device.

![output](https://github.com/user-attachments/assets/0c1d66d5-6f8c-4193-bce2-4a577e20f7aa)

## Bluetooth widget
### Simple click to scan; click to add/connect/disconnect/remove.
![output](https://github.com/user-attachments/assets/4f4294fc-75ef-4a43-9a80-e5a5443a5bb5)

## Pacman widget
### Pretty output of pacman logs. this log can be limited to day,week,month,year, or not at all.

![20250516_21h28m07s_grim](https://github.com/user-attachments/assets/ee73c9b7-8c00-43d3-bf24-992263f3e8e4)

>[!NOTE]
>I really haven't worked on these two yet basic functionality is there but certainly less than desired.

## Wifi widget
Non actionable and just displays information at the moment. you can toggle rfkill with the bar button and initiate a rescan in the panel.

![20250516_22h00m53s_grim](https://github.com/user-attachments/assets/43764b0b-915c-4f72-bf0f-58726adfef75)

## Mic widget
 shows mute/volume/suspended state of mics and allows you to swap defaults

![20250516_22h01m16s_grim](https://github.com/user-attachments/assets/9a2784b4-b448-4fd3-972a-97b590b13af6)
