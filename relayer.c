/*
 * Transparent RASPNET test relayer.
 *
 * Wire this board between two other boards:
 *   previous clock TX -> relayer PD4 clock RX
 *   previous data TX  -> relayer PD5 data RX
 *   relayer PB4 clock TX -> next clock RX
 *   relayer PB5 data TX  -> next data RX
 *   GND shared
 *
 * This firmware does not parse frames. It mirrors the incoming data and clock
 * pins as quickly as possible, preserving payloads, CRC fields, and malformed
 * frames.
 */

#ifndef F_CPU
#define F_CPU 12000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <util/delay.h>

#ifndef BAUD
#define BAUD 115200UL
#endif

#define UART_UBRR ((F_CPU / 8UL / BAUD) - 1UL)

#define CLOCK_TX_PIN PB4
#define DATA_TX_PIN PB5
#define CLOCK_RX_PIN PD4
#define DATA_RX_PIN PD5
#define CLOCK_RX_PCINT PCINT20

#define CLOCK_RX_VALUE() ((PIND & (1 << CLOCK_RX_PIN)) != 0)
#define DATA_RX_VALUE() ((PIND & (1 << DATA_RX_PIN)) != 0)

static volatile uint8_t enabled = 0;
static volatile uint8_t previous_clock = 0;

static void uart_init(void)
{
  UBRR0H = (uint8_t)(UART_UBRR >> 8);
  UBRR0L = (uint8_t)UART_UBRR;
  UCSR0A |= (1 << U2X0);
  UCSR0B |= (1 << RXEN0) | (1 << TXEN0);
  UCSR0C |= (1 << UCSZ01) | (1 << UCSZ00);
}

static void uart_putc(char c)
{
  while ((UCSR0A & (1 << UDRE0)) == 0) {}
  UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
  while (*s != '\0') {
    uart_putc(*s++);
  }
}

static uint8_t uart_has_byte(void)
{
  return (UCSR0A & (1 << RXC0)) != 0 ? 1u : 0u;
}

static uint8_t uart_getc(void)
{
  return UDR0;
}

static void set_outputs_low(void)
{
  PORTB &= ~((1 << CLOCK_TX_PIN) | (1 << DATA_TX_PIN));
}

static void relay_edge(void)
{
  if (DATA_RX_VALUE()) {
    PORTB |= (1 << DATA_TX_PIN);
  } else {
    PORTB &= ~(1 << DATA_TX_PIN);
  }

  if (CLOCK_RX_VALUE()) {
    PORTB |= (1 << CLOCK_TX_PIN);
  } else {
    PORTB &= ~(1 << CLOCK_TX_PIN);
  }
}

ISR(PCINT2_vect)
{
  uint8_t clock_value = CLOCK_RX_VALUE();

  if (clock_value == previous_clock) {
    return;
  }
  previous_clock = clock_value;

  if (enabled != 0u) {
    relay_edge();
  }
}

static void init_relay_interrupt(void)
{
  previous_clock = CLOCK_RX_VALUE();
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << CLOCK_RX_PCINT);
}

int main(void)
{
  DDRB |= (1 << CLOCK_TX_PIN) | (1 << DATA_TX_PIN);
  DDRD &= ~((1 << CLOCK_RX_PIN) | (1 << DATA_RX_PIN));
  PORTD &= ~((1 << CLOCK_RX_PIN) | (1 << DATA_RX_PIN));

  set_outputs_low();

  uart_init();
  init_relay_interrupt();
  sei();
  uart_puts("RELAYER idle\r\n");

  while (1) {
    if (uart_has_byte()) {
      uint8_t command = uart_getc();

      if (command == 's' || command == 'S') {
        cli();
        enabled = 1;
        previous_clock = CLOCK_RX_VALUE();
        sei();
        uart_puts("RELAYER start\r\n");
      } else if (command == 'x' || command == 'X') {
        cli();
        enabled = 0;
        set_outputs_low();
        sei();
        uart_puts("RELAYER stop\r\n");
      } else {
        uart_puts("RELAYER unknown\r\n");
      }
    }

    if (enabled == 0u) {
      uart_puts("RELAYER idle\r\n");
      _delay_ms(1000);
    }
  }
}
