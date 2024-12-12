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

The first  Virtual UART (UART1, ...) is Zephyr UART 1
The second Virtual UART (UART2, ...) is Zephyr UART 0

### Pinout

Signal direction as seen from the nRF5340.

| PIN      | Arduino | MCU   | Direction |
|----------|---------|-------|-----------|
|  TX      |    D0   | P1.00 |    out    |
|  RX      |    D1   | P1.01 |    in     |
| RTS      |    D7   | P1.11 |    out    |
| CTS      |    D8   | P1.10 |    in     |
| Time Sync|    D10  | P1.06 |    out    |



### HCI over USB CDC

```sh
west build --pristine -b nrf5340dk/nrf5340/cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=usb.overlay -DOVERLAY_CONFIG=overlay-usb.conf
```


### HCI over UART 0 connected to second Virtual UART in J-Link Probe 
```sh
west build --pristine -b nrf5340dk/nrf5340/cpuapp
```

- HCI over second Virtual port / UART 0
- Boot banner on Arduino Header UART (P1.01) / UART 1 


### HCI over UART 1 connected to first Virtual UART in J-Link Probe as well as Arduino Headers
```sh
west build --pristine -b nrf5340dk/nrf5340/cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=uart1.overlay
```

- No Boot Banner on Arduino Header UART



## nRF5340 Audio DK

The first UART (UART1, P1.04, P1.05) is only available via test points (TP60, TP61, ...)
The second UART (UART2, P1.08, P1.09) is connected to the Arduino headers.

### Pinout

Signal direction as seen from the nRF5340.

| PIN      | Arduino | MCU   | Direction |
|----------|---------|-------|-----------|
|  TX      |    D0   | P1.09 |    out    |
|  RX      |    D1   | P1.08 |    in     |
| RTS      |    D7   | P1.11 |    out    |
| CTS      |    D8   | P1.10 |    in     |
| Time Sync|    D10  | P1.01 |    out    |


### HCI over USB CDC

```sh
west build --pristine -b nrf5340_audio_dk/nrf5340/cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=usb.overlay -DOVERLAY_CONFIG=overlay-usb.conf
```

### HCI over UART 0 connected to Virtual UART in J-Link Probe

```sh
west build --pristine -b nrf5340_audio_dk/nrf5340/cpuapp
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


