# build for USB CDC
```sh
west build -b nrf5340dk_nrf5340_cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=usb.overlay -DOVERLAY_CONFIG=overlay-usb.conf
```

# build for build in serial adapter of the nrf5340_audio_dk a.k. uart0
```sh
west build -b nrf5340_audio_dk_nrf5340_cpuapp
```

# build for uart1 a.k. arduino_serial
```sh
west build -b nrf5340_audio_dk_nrf5340_cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=uart1.overlay
```

# build for debug and uart1
```sh
west build -b nrf5340_audio_dk_nrf5340_cpuapp -- -DEXTRA_DTC_OVERLAY_FILE=uart1.overlay -DOVERLAY_CONFIG=debug.conf
```
# pinout for nrf5340_audio_dk for uart1

| PIN | Arduino |  nrf  |
|:---:|:-------:|:-----:|
|  TX |    D0   | P1.09 |
|  RX |    D1   | P1.08 |
| RTS |    D7   | P1.11 |
| CTS |    D8   | P1.10 |
