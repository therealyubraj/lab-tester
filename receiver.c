/*
 * Deterministic RASPNET test receiver.
 *
 * Wire this board after the implementation under test:
 *   DUT clock TX -> receiver PD4 clock RX
 *   DUT data TX  -> receiver PD5 data RX
 *   GND shared
 */

#ifndef F_CPU
#define F_CPU 12000000UL
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#define BAUD 115200UL
#define UART_UBRR ((F_CPU / 8UL / BAUD) - 1UL)

#define CLOCK_RX_PIN PD4
#define DATA_RX_PIN PD5

#define PREAMBLE 0x7Eu
#define MAX_PAYLOAD_SIZE 248u

#define CLOCK_RX_VALUE() (((PIND & (1 << CLOCK_RX_PIN)) != 0) ? 1u : 0u)
#define DATA_RX_VALUE() (((PIND & (1 << DATA_RX_PIN)) != 0) ? 1u : 0u)

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

static void uart_hex32(uint32_t value)
{
  uart_hex8((uint8_t)(value >> 24));
  uart_hex8((uint8_t)(value >> 16));
  uart_hex8((uint8_t)(value >> 8));
  uart_hex8((uint8_t)value);
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

static uint8_t wait_edge(uint8_t *previous_clock)
{
  uint8_t current_clock;

  do {
    current_clock = CLOCK_RX_VALUE();
  } while (current_clock == *previous_clock);

  *previous_clock = current_clock;
  return DATA_RX_VALUE();
}

static uint8_t read_byte(uint8_t *previous_clock)
{
  uint8_t value = 0;

  for (uint8_t i = 0; i < 8u; i++) {
    value = (uint8_t)((value << 1) | wait_edge(previous_clock));
  }

  return value;
}

static void print_frame(uint32_t received_crc,
                        uint32_t calculated_crc,
                        uint8_t size,
                        uint8_t destination,
                        uint8_t source,
                        const uint8_t *payload)
{
  uint8_t payload_size = (uint8_t)(size - 2u);

  uart_puts("FRAME crc=");
  uart_hex32(received_crc);
  uart_puts(" calc=");
  uart_hex32(calculated_crc);
  uart_puts(" ok=");
  uart_putc(received_crc == calculated_crc ? '1' : '0');
  uart_puts(" size=");
  uart_hex8(size);
  uart_puts(" dst=");
  uart_hex8(destination);
  uart_puts(" src=");
  uart_hex8(source);
  uart_puts(" payload=");

  for (uint8_t i = 0; i < payload_size; i++) {
    uart_hex8(payload[i]);
  }

  uart_puts(" ascii=\"");
  for (uint8_t i = 0; i < payload_size; i++) {
    uint8_t c = payload[i];
    uart_putc((c >= 32u && c <= 126u && c != '"') ? (char)c : '.');
  }
  uart_puts("\"\r\n");
}

int main(void)
{
  uint8_t previous_clock;
  uint8_t rolling = 0;
  uint8_t payload[MAX_PAYLOAD_SIZE];

  DDRD &= ~((1 << CLOCK_RX_PIN) | (1 << DATA_RX_PIN));
  PORTD &= ~((1 << CLOCK_RX_PIN) | (1 << DATA_RX_PIN));
  uart_init();
  uart_puts("RECEIVER start\r\n");

  previous_clock = CLOCK_RX_VALUE();

  while (1) {
    rolling = (uint8_t)((rolling << 1) | wait_edge(&previous_clock));

    if (rolling == PREAMBLE) {
      uint32_t received_crc = 0;
      uint32_t calculated_crc = 0;
      uint8_t size;
      uint8_t destination;
      uint8_t source;

      received_crc = (uint32_t)read_byte(&previous_clock);
      received_crc |= ((uint32_t)read_byte(&previous_clock) << 8);
      received_crc |= ((uint32_t)read_byte(&previous_clock) << 16);
      received_crc |= ((uint32_t)read_byte(&previous_clock) << 24);

      size = read_byte(&previous_clock);
      destination = read_byte(&previous_clock);
      source = read_byte(&previous_clock);

      if (size < 2u || (uint8_t)(size - 2u) > MAX_PAYLOAD_SIZE) {
        uart_puts("ERROR invalid-size\r\n");
        rolling = 0;
        continue;
      }

      calculated_crc = crc_update_byte(calculated_crc, destination);
      calculated_crc = crc_update_byte(calculated_crc, source);

      for (uint8_t i = 0; i < (uint8_t)(size - 2u); i++) {
        payload[i] = read_byte(&previous_clock);
        calculated_crc = crc_update_byte(calculated_crc, payload[i]);
      }

      calculated_crc = crc_finalize(calculated_crc);
      print_frame(received_crc, calculated_crc, size, destination, source, payload);
      rolling = 0;
    }
  }
}
