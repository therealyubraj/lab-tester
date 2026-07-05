# RASPNET Test Project

This folder contains standalone firmware for a three-board test:

```text
test sender -> implementation under test -> test receiver
```

It also contains a transparent relayer firmware for validating the test setup:

```text
test sender -> perfect relayer -> test receiver
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

Perfect relayer wiring:

```text
previous PB4 -> relayer PD4 clock RX
previous PB5 -> relayer PD5 data RX
relayer PB4 -> next PD4 clock RX
relayer PB5 -> next PD5 data RX
GND shared
```

The relayer waits until it receives `s` or `S` on UART before forwarding pins.
Send `x` or `X` to stop forwarding and drive its outputs low.
While stopped, it prints `RELAYER idle` once per second. Unknown UART input
prints `RELAYER unknown`.

## Build And Flash

```sh
make sender
make receiver
make relayer
```

Override flashing settings if needed:

```sh
make sender PORT=/dev/spidev0.1 PROGRAMMER=gpio
make receiver PORT=/dev/ttyACM0 PROGRAMMER=arduino
```

Change the UART baud rate in one place:

```make
BAUD ?= 115200UL
```

or override it per command:

```sh
make sender BAUD=57600UL
make receiver BAUD=57600UL
```

Change the sender wire bit rate in one place:

```make
SEND_BPS ?= 1000UL
```

or override it per command:

```sh
make sender SEND_BPS=2000UL
```

Change the pause between sender frames in one place:

```make
INTER_FRAME_MS ?= 40
```

or override it per command:

```sh
make sender INTER_FRAME_MS=10
```

The Makefile includes the selected configuration in the build output path, so
changing `INTER_FRAME_MS` recompiles the sender automatically. If you are unsure
which binary is flashed, run:

```sh
make clean
make sender INTER_FRAME_MS=10
```

Change the sender frame source and destination in one place:

```make
TEST_SOURCE ?= 0x58u
TEST_DESTINATION ?= 0x00u
```

or override them per command:

```sh
make sender TEST_SOURCE=0x57u TEST_DESTINATION=0x00u
```

Build without flashing:

```sh
make sender-build
make receiver-build
make relayer-build
```

The receiver captures frames in a pin-change interrupt and drains them to UART
from the main loop. Its queue is configurable:

```make
RX_QUEUE_FRAMES ?= 16u
RX_QUEUE_PAYLOAD_SIZE ?= 32u
```

For larger payloads or longer bursts:

```sh
make receiver RX_QUEUE_FRAMES=20u RX_QUEUE_PAYLOAD_SIZE=64u
```

Receiver diagnostics:

```text
DROPPED N    completed frames lost because the queue was full
INVALID N    frames abandoned because the size field was invalid
FRAME ... printed=N captured=N queued=N
```

`printed` and `captured` are cumulative since boot. `queued` is the number of
completed frames still waiting in the receiver queue after this frame was
printed.

## Expected Test Behavior

The sender emits:

- 10 broadcast frames with random-looking payloads
- 2 broadcast frames with payload `Send`
- relay pressure before, between, and after the `Send` triggers

The verifier defaults expect 12 relayed broadcast frames and 2 own frames from
the DUT.

The sender waits until it receives `s` or `S` on UART, then sends the full test
sequence once. Send `s` again to repeat the run.

The CRC field is sent and received in AVR `uint32_t` memory order, matching the
main project's `read_bit_scattered` / `set_bit_scattered` behavior.
