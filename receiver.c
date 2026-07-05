/*
 * Interrupt-driven RASPNET test receiver.
 *
 * Wire this board after the implementation under test:
 *   DUT clock TX -> receiver PD4 clock RX / PCINT20
 *   DUT data TX  -> receiver PD5 data RX
 *   GND shared
 *
 * The pin-change ISR captures every clock edge and queues completed frames.
 * The main loop drains queued frames to UART, so UART output cannot block bit
 * reception during a frame.
 */

#ifndef F_CPU
#define F_CPU 12000000UL
#endif

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#ifndef BAUD
#define BAUD 115200UL
#endif

#ifndef RX_QUEUE_FRAMES
#define RX_QUEUE_FRAMES 16u
#endif

#ifndef RX_QUEUE_PAYLOAD_SIZE
#define RX_QUEUE_PAYLOAD_SIZE 32u
#endif

#if RX_QUEUE_FRAMES == 0
#error "RX_QUEUE_FRAMES must be greater than zero"
#endif

#if RX_QUEUE_PAYLOAD_SIZE == 0
#error "RX_QUEUE_PAYLOAD_SIZE must be greater than zero"
#endif

#define UART_UBRR ((F_CPU / 8UL / BAUD) - 1UL)

#define CLOCK_RX_PIN PD4
#define DATA_RX_PIN PD5
#define CLOCK_RX_PCINT PCINT20

#define PREAMBLE 0x7Eu
#define MAX_PAYLOAD_SIZE 248u

#define CLOCK_RX_VALUE() (((PIND & (1 << CLOCK_RX_PIN)) != 0) ? 1u : 0u)
#define DATA_RX_VALUE() (((PIND & (1 << DATA_RX_PIN)) != 0) ? 1u : 0u)

struct QueuedFrame {
  uint32_t received_crc;
  uint8_t size;
  uint8_t destination;
  uint8_t source;
  uint8_t payload_size;
  uint8_t stored_payload_size;
  uint8_t truncated;
  uint8_t payload[RX_QUEUE_PAYLOAD_SIZE];
};

static volatile struct QueuedFrame frame_queue[RX_QUEUE_FRAMES];
static volatile uint8_t queue_head = 0;
static volatile uint8_t queue_tail = 0;
static volatile uint8_t queue_count = 0;
static volatile uint16_t dropped_frames = 0;
static volatile uint16_t captured_frames = 0;
static volatile uint16_t invalid_frames = 0;

static volatile uint8_t previous_clock = 0;
static volatile uint8_t rolling = 0;
static volatile uint8_t receiving = 0;
static volatile uint8_t current_byte = 0;
static volatile uint8_t current_bit_count = 0;
static volatile uint8_t frame_byte_index = 0;
static volatile struct QueuedFrame building_frame;

static const uint32_t crc_table[256] PROGMEM = {
  0x00000000u, 0x04C11DB7u, 0x09823B6Eu, 0x0D4326D9u, 0x130476DCu, 0x17C56B6Bu, 0x1A864DB2u, 0x1E475005u, 0x2608EDB8u, 0x22C9F00Fu, 0x2F8AD6D6u, 0x2B4BCB61u, 0x350C9B64u, 0x31CD86D3u, 0x3C8EA00Au, 0x384FBDBDu,
  0x4C11DB70u, 0x48D0C6C7u, 0x4593E01Eu, 0x4152FDA9u, 0x5F15ADACu, 0x5BD4B01Bu, 0x569796C2u, 0x52568B75u, 0x6A1936C8u, 0x6ED82B7Fu, 0x639B0DA6u, 0x675A1011u, 0x791D4014u, 0x7DDC5DA3u, 0x709F7B7Au, 0x745E66CDu,
  0x9823B6E0u, 0x9CE2AB57u, 0x91A18D8Eu, 0x95609039u, 0x8B27C03Cu, 0x8FE6DD8Bu, 0x82A5FB52u, 0x8664E6E5u, 0xBE2B5B58u, 0xBAEA46EFu, 0xB7A96036u, 0xB3687D81u, 0xAD2F2D84u, 0xA9EE3033u, 0xA4AD16EAu, 0xA06C0B5Du,
  0xD4326D90u, 0xD0F37027u, 0xDDB056FEu, 0xD9714B49u, 0xC7361B4Cu, 0xC3F706FBu, 0xCEB42022u, 0xCA753D95u, 0xF23A8028u, 0xF6FB9D9Fu, 0xFBB8BB46u, 0xFF79A6F1u, 0xE13EF6F4u, 0xE5FFEB43u, 0xE8BCCD9Au, 0xEC7DD02Du,
  0x34867077u, 0x30476DC0u, 0x3D044B19u, 0x39C556AEu, 0x278206ABu, 0x23431B1Cu, 0x2E003DC5u, 0x2AC12072u, 0x128E9DCFu, 0x164F8078u, 0x1B0CA6A1u, 0x1FCDBB16u, 0x018AEB13u, 0x054BF6A4u, 0x0808D07Du, 0x0CC9CDCAu,
  0x7897AB07u, 0x7C56B6B0u, 0x71159069u, 0x75D48DDEu, 0x6B93DDDBu, 0x6F52C06Cu, 0x6211E6B5u, 0x66D0FB02u, 0x5E9F46BFu, 0x5A5E5B08u, 0x571D7DD1u, 0x53DC6066u, 0x4D9B3063u, 0x495A2DD4u, 0x44190B0Du, 0x40D816BAu,
  0xACA5C697u, 0xA864DB20u, 0xA527FDF9u, 0xA1E6E04Eu, 0xBFA1B04Bu, 0xBB60ADFCu, 0xB6238B25u, 0xB2E29692u, 0x8AAD2B2Fu, 0x8E6C3698u, 0x832F1041u, 0x87EE0DF6u, 0x99A95DF3u, 0x9D684044u, 0x902B669Du, 0x94EA7B2Au,
  0xE0B41DE7u, 0xE4750050u, 0xE9362689u, 0xEDF73B3Eu, 0xF3B06B3Bu, 0xF771768Cu, 0xFA325055u, 0xFEF34DE2u, 0xC6BCF05Fu, 0xC27DEDE8u, 0xCF3ECB31u, 0xCBFFD686u, 0xD5B88683u, 0xD1799B34u, 0xDC3ABDEDu, 0xD8FBA05Au,
  0x690CE0EEu, 0x6DCDFD59u, 0x608EDB80u, 0x644FC637u, 0x7A089632u, 0x7EC98B85u, 0x738AAD5Cu, 0x774BB0EBu, 0x4F040D56u, 0x4BC510E1u, 0x46863638u, 0x42472B8Fu, 0x5C007B8Au, 0x58C1663Du, 0x558240E4u, 0x51435D53u,
  0x251D3B9Eu, 0x21DC2629u, 0x2C9F00F0u, 0x285E1D47u, 0x36194D42u, 0x32D850F5u, 0x3F9B762Cu, 0x3B5A6B9Bu, 0x0315D626u, 0x07D4CB91u, 0x0A97ED48u, 0x0E56F0FFu, 0x1011A0FAu, 0x14D0BD4Du, 0x19939B94u, 0x1D528623u,
  0xF12F560Eu, 0xF5EE4BB9u, 0xF8AD6D60u, 0xFC6C70D7u, 0xE22B20D2u, 0xE6EA3D65u, 0xEBA91BBCu, 0xEF68060Bu, 0xD727BBB6u, 0xD3E6A601u, 0xDEA580D8u, 0xDA649D6Fu, 0xC423CD6Au, 0xC0E2D0DDu, 0xCDA1F604u, 0xC960EBB3u,
  0xBD3E8D7Eu, 0xB9FF90C9u, 0xB4BCB610u, 0xB07DABA7u, 0xAE3AFBA2u, 0xAAFBE615u, 0xA7B8C0CCu, 0xA379DD7Bu, 0x9B3660C6u, 0x9FF77D71u, 0x92B45BA8u, 0x9675461Fu, 0x8832161Au, 0x8CF30BADu, 0x81B02D74u, 0x857130C3u,
  0x5D8A9099u, 0x594B8D2Eu, 0x5408ABF7u, 0x50C9B640u, 0x4E8EE645u, 0x4A4FFBF2u, 0x470CDD2Bu, 0x43CDC09Cu, 0x7B827D21u, 0x7F436096u, 0x7200464Fu, 0x76C15BF8u, 0x68860BFDu, 0x6C47164Au, 0x61043093u, 0x65C52D24u,
  0x119B4BE9u, 0x155A565Eu, 0x18197087u, 0x1CD86D30u, 0x029F3D35u, 0x065E2082u, 0x0B1D065Bu, 0x0FDC1BECu, 0x3793A651u, 0x3352BBE6u, 0x3E119D3Fu, 0x3AD08088u, 0x2497D08Du, 0x2056CD3Au, 0x2D15EBE3u, 0x29D4F654u,
  0xC5A92679u, 0xC1683BCEu, 0xCC2B1D17u, 0xC8EA00A0u, 0xD6AD50A5u, 0xD26C4D12u, 0xDF2F6BCBu, 0xDBEE767Cu, 0xE3A1CBC1u, 0xE760D676u, 0xEA23F0AFu, 0xEEE2ED18u, 0xF0A5BD1Du, 0xF464A0AAu, 0xF9278673u, 0xFDE69BC4u,
  0x89B8FD09u, 0x8D79E0BEu, 0x803AC667u, 0x84FBDBD0u, 0x9ABC8BD5u, 0x9E7D9662u, 0x933EB0BBu, 0x97FFAD0Cu, 0xAFB010B1u, 0xAB710D06u, 0xA6322BDFu, 0xA2F33668u, 0xBCB4666Du, 0xB8757BDAu, 0xB5365D03u, 0xB1F740B4u
};

static void uart_init(void)
{
  UBRR0H = (uint8_t)(UART_UBRR >> 8);
  UBRR0L = (uint8_t)UART_UBRR;
  UCSR0A |= (1 << U2X0);
  UCSR0B |= (1 << TXEN0);
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

static void uart_hex_nibble(uint8_t value)
{
  value &= 0x0Fu;
  if (value < 10u) {
    uart_putc((char)('0' + value));
  } else {
    uart_putc((char)('A' + (value - 10u)));
  }
}

static void uart_hex8(uint8_t value)
{
  uart_hex_nibble(value >> 4);
  uart_hex_nibble(value);
}

static void uart_uint16(uint16_t value)
{
  char digits[5];
  uint8_t count = 0;

  if (value == 0u) {
    uart_putc('0');
    return;
  }

  while (value > 0u && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10u));
    value /= 10u;
  }

  while (count > 0u) {
    uart_putc(digits[--count]);
  }
}

static uint32_t crc_update_byte(uint32_t crc, uint8_t byte)
{
  uint8_t table_index = (uint8_t)(((crc >> 24) ^ byte) & 0xFFu);
  return (crc << 8) ^ pgm_read_dword_near(&crc_table[table_index]);
}

static uint32_t crc_finalize(uint32_t crc)
{
  return ((crc & 0x000000FFu) << 24) |
         ((crc & 0x0000FF00u) << 8) |
         ((crc & 0x00FF0000u) >> 8) |
         ((crc & 0xFF000000u) >> 24);
}

static uint32_t frame_crc(const struct QueuedFrame *frame)
{
  uint32_t crc = 0;

  crc = crc_update_byte(crc, frame->destination);
  crc = crc_update_byte(crc, frame->source);

  if (frame->truncated != 0u) {
    return 0xFFFFFFFFu;
  }

  for (uint8_t i = 0; i < frame->payload_size; i++) {
    crc = crc_update_byte(crc, frame->payload[i]);
  }

  return crc_finalize(crc);
}

static void reset_rx_state(void)
{
  receiving = 0;
  current_byte = 0;
  current_bit_count = 0;
  frame_byte_index = 0;
  rolling = 0;
}

static void queue_completed_frame(void)
{
  if (queue_count >= RX_QUEUE_FRAMES) {
    dropped_frames++;
    return;
  }

  frame_queue[queue_head] = building_frame;
  queue_head++;
  if (queue_head >= RX_QUEUE_FRAMES) {
    queue_head = 0;
  }
  queue_count++;
  captured_frames++;
}

static void process_frame_byte(uint8_t byte)
{
  uint8_t payload_index;

  if (frame_byte_index == 0u) {
    building_frame.received_crc = (uint32_t)byte;
  } else if (frame_byte_index == 1u) {
    building_frame.received_crc |= ((uint32_t)byte << 8);
  } else if (frame_byte_index == 2u) {
    building_frame.received_crc |= ((uint32_t)byte << 16);
  } else if (frame_byte_index == 3u) {
    building_frame.received_crc |= ((uint32_t)byte << 24);
  } else if (frame_byte_index == 4u) {
    building_frame.size = byte;
    if (byte < 2u || (uint8_t)(byte - 2u) > MAX_PAYLOAD_SIZE) {
      invalid_frames++;
      reset_rx_state();
      return;
    }
    building_frame.payload_size = (uint8_t)(byte - 2u);
    building_frame.stored_payload_size = 0;
    building_frame.truncated = 0;
  } else if (frame_byte_index == 5u) {
    building_frame.destination = byte;
  } else if (frame_byte_index == 6u) {
    building_frame.source = byte;
    if (building_frame.payload_size == 0u) {
      queue_completed_frame();
      reset_rx_state();
      return;
    }
  } else {
    payload_index = (uint8_t)(frame_byte_index - 7u);
    if (payload_index < RX_QUEUE_PAYLOAD_SIZE) {
      building_frame.payload[payload_index] = byte;
      building_frame.stored_payload_size++;
    } else {
      building_frame.truncated = 1;
    }

    if ((uint8_t)(payload_index + 1u) >= building_frame.payload_size) {
      queue_completed_frame();
      reset_rx_state();
      return;
    }
  }

  frame_byte_index++;
}

ISR(PCINT2_vect)
{
  uint8_t clock_value = CLOCK_RX_VALUE();
  uint8_t data_value;

  if (clock_value == previous_clock) {
    return;
  }
  previous_clock = clock_value;

  data_value = DATA_RX_VALUE();

  if (receiving == 0u) {
    rolling = (uint8_t)((rolling << 1) | data_value);
    if (rolling == PREAMBLE) {
      receiving = 1;
      current_byte = 0;
      current_bit_count = 0;
      frame_byte_index = 0;
      building_frame.received_crc = 0;
      building_frame.size = 0;
      building_frame.destination = 0;
      building_frame.source = 0;
      building_frame.payload_size = 0;
      building_frame.stored_payload_size = 0;
      building_frame.truncated = 0;
    }
    return;
  }

  current_byte = (uint8_t)((current_byte << 1) | data_value);
  current_bit_count++;

  if (current_bit_count >= 8u) {
    process_frame_byte(current_byte);
    current_byte = 0;
    current_bit_count = 0;
  }
}

static uint8_t dequeue_frame(struct QueuedFrame *frame)
{
  uint8_t tail;

  cli();
  if (queue_count == 0u) {
    sei();
    return 0;
  }
  tail = queue_tail;
  sei();

  frame->received_crc = frame_queue[tail].received_crc;
  frame->size = frame_queue[tail].size;
  frame->destination = frame_queue[tail].destination;
  frame->source = frame_queue[tail].source;
  frame->payload_size = frame_queue[tail].payload_size;
  frame->stored_payload_size = frame_queue[tail].stored_payload_size;
  frame->truncated = frame_queue[tail].truncated;

  for (uint8_t i = 0; i < frame->stored_payload_size; i++) {
    frame->payload[i] = frame_queue[tail].payload[i];
  }

  cli();
  if (queue_count > 0u && queue_tail == tail) {
    queue_tail++;
    if (queue_tail >= RX_QUEUE_FRAMES) {
      queue_tail = 0;
    }
    queue_count--;
  }
  sei();

  return 1;
}

static uint16_t take_counter(volatile uint16_t *counter)
{
  uint16_t value;

  cli();
  value = *counter;
  *counter = 0;
  sei();

  return value;
}

static void print_frame(const struct QueuedFrame *frame, uint16_t printed_count)
{
  uint32_t calculated_crc = frame_crc(frame);

  uart_puts("FRAME ok=");
  uart_putc(frame->received_crc == calculated_crc ? '1' : '0');
  uart_puts(" dst=");
  uart_hex8(frame->destination);
  uart_puts(" src=");
  uart_hex8(frame->source);
  uart_puts(" ascii=\"");

  for (uint8_t i = 0; i < frame->stored_payload_size; i++) {
    uint8_t c = frame->payload[i];
    uart_putc((c >= 32u && c <= 126u && c != '"') ? (char)c : '.');
  }

  uart_puts("\"");
  if (frame->truncated != 0u) {
    uart_puts(" truncated=1");
  }
  uart_puts(" printed=");
  uart_uint16(printed_count);
  uart_puts("\r\n");
}

static void init_receiver_interrupt(void)
{
  previous_clock = CLOCK_RX_VALUE();
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << CLOCK_RX_PCINT);
}

int main(void)
{
  struct QueuedFrame frame;
  uint16_t printed_frames = 0;

  DDRD &= ~((1 << CLOCK_RX_PIN) | (1 << DATA_RX_PIN));
  PORTD &= ~((1 << CLOCK_RX_PIN) | (1 << DATA_RX_PIN));

  uart_init();
  init_receiver_interrupt();
  sei();

  uart_puts("RECEIVER interrupt-ready\r\n");

  while (1) {
    uint16_t dropped = take_counter(&dropped_frames);
    uint16_t invalid = take_counter(&invalid_frames);
    uint16_t captured = take_counter(&captured_frames);

    if (captured > 0u) {
      uart_puts("CAPTURED ");
      uart_uint16(captured);
      uart_puts("\r\n");
    }

    if (dropped > 0u) {
      uart_puts("DROPPED ");
      uart_uint16(dropped);
      uart_puts("\r\n");
    }

    if (invalid > 0u) {
      uart_puts("INVALID ");
      uart_uint16(invalid);
      uart_puts("\r\n");
    }

    if (dequeue_frame(&frame) != 0u) {
      printed_frames++;
      print_frame(&frame, printed_frames);
    }
  }
}
