# nRF5340 Bluetooth Controller with Support for LE ISO Timesync

This fork of the Zephyr HCI UART example adds a custom HCI Command to allow for timesync between the Bluetooth Host 
and the Bluetooth Controller. When the HCI LE Read ISO Clock command is received, the Controller toggles a GPIO and 
returns its current Bluetooth LE ISO Clock in the Command Complete Event.

It has been tested on the nRF5340 Audio DK, but it should work with any nRF5340 dev kit.

## Requirements
- nRF Connect SDK v2.8 or newer

## HCI LE Read ISO Clock Command
- OGF: 0x3f, OCF: 0x200
- Parameters: Flags (1 Octet) unused
- Response: HCI Command Complete Event with status and 4 bytes timestamp in microseconds


## nRF5340 Development Kit

### HCI over USB CDC

```sh
west build -b nrf5340dk/nrf5340/cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=usb.overlay -DOVERLAY_CONFIG=overlay-usb.conf
```
_HCI over UART and timesync not tested._


## nRF5340 Audio DK

### HCI over USB CDC

```sh
west build --pristine -b nrf5340_audio_dk/nrf5340/cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=usb.overlay -DOVERLAY_CONFIG=overlay-usb.conf
```

### HCI over UART 0 connected to Virtual UART in J-Link Probe

```sh
west build --pristine -b nrf5340_audio_dk_nrf5340_cpuapp
```

### HCI over UART 1 connected to Virtual UART in J-Link Probe as well as Arduino Headers

Release build:
```sh
west build --pristine -b nrf5340_audio_dk/nrf5340/cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=uart1.overlay
```

Debug build:
```sh
west build --pristine -b nrf5340_audio_dk/nrf5340/cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=uart1.overlay -DOVERLAY_CONFIG=debug.conf
```

To use UART 1 via Arduino headers, the virtual UART of the J-Link probe needs to be disabled, e.g. with the JLink Configuration Tool.

### UART 1 + Time Sync Pinout

Signal direction as seen from the nRF5340.

| PIN      | Arduino |  nrf  | Direction |
|----------|---------|-------|-----------|
|  TX      |    D0   | P1.09 |    out    |
|  RX      |    D1   | P1.08 |    in     |
| RTS      |    D7   | P1.11 |    out    |
| CTS      |    D8   | P1.10 |    in     |
| Timesync |    D10  | P1.01 |    out    |
