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

- 10 broadcast frames with random-looking payloads
- 2 broadcast frames with payload `Send`
- relay pressure before, between, and after the `Send` triggers

The verifier defaults expect 12 relayed broadcast frames and 2 own frames from
the DUT.

The sender waits until it receives `s` or `S` on UART, then sends the full test
sequence once. Send `s` again to repeat the run.
