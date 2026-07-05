# RASPNET Test Project

This folder contains standalone firmware for a three-board test:

```text
test sender -> implementation under test -> test receiver
```

The sender emits deterministic frames. The receiver prints parsed frames over
UART as `FRAME ...` lines. Paste those lines into `verifier.html`.

## Wiring

Sender to DUT:

```text
sender PB4 -> DUT PD4 clock RX
sender PB5 -> DUT PD5 data RX
GND shared
```

DUT to receiver:

```text
DUT PB4 -> receiver PD4 clock RX
DUT PB5 -> receiver PD5 data RX
GND shared
```

## Build And Flash

```sh
make sender
make receiver
```

Override flashing settings if needed:

```sh
make sender PORT=/dev/spidev0.1 PROGRAMMER=gpio
make receiver PORT=/dev/ttyACM0 PROGRAMMER=arduino
```

Build without flashing:

```sh
make sender-build
make receiver-build
```

## Expected Test Behavior

The sender emits:

- normal relay frames
- a broadcast relay frame
- a `Send` payload addressed to `0x59`
- more relay pressure after the trigger
- one bad-CRC broadcast that should still be relayed
- one frame with source `0x59` that should not be relayed

The verifier defaults expect 5 relayed frames and 1 own frame from the DUT.
