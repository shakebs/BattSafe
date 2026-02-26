/*
 * test_uart.c — Minimal UART test for THEJAS32
 *
 * Does NOTHING except send "Hello" over UART0 in an infinite loop.
 * Used to verify UART register addresses on the real board.
 */
#include <stdint.h>

/* THEJAS32 UART0 registers */
#define UART0_BASE 0x10000100
#define REG32(a) (*(volatile uint32_t *)(a))

#define UART0_THR REG32(UART0_BASE + 0x00)
#define UART0_LSR REG32(UART0_BASE + 0x14)
#define LSR_THRE (1 << 5)

static void uart_putc(char c) {
  while (!(UART0_LSR & LSR_THRE)) {
  }
  UART0_THR = (uint8_t)c;
}

static void uart_puts(const char *s) {
  while (*s) {
    if (*s == '\n')
      uart_putc('\r');
    uart_putc(*s++);
  }
}

static void delay(volatile uint32_t count) {
  while (count--) {
    __asm__ volatile("nop");
  }
}

int main(void) {
  /* Simple infinite loop: print "Hello" every ~1 second */
  uint32_t n = 0;
  while (1) {
    uart_puts("Hello from THEJAS32! #");

    /* Print the counter as decimal */
    char buf[12];
    int i = 0;
    uint32_t tmp = n;
    if (tmp == 0) {
      buf[i++] = '0';
    } else {
      char rev[12];
      int j = 0;
      while (tmp > 0) {
        rev[j++] = '0' + (tmp % 10);
        tmp /= 10;
      }
      while (j > 0)
        buf[i++] = rev[--j];
    }
    buf[i] = '\0';
    uart_puts(buf);
    uart_puts("\r\n");

    n++;
    delay(5000000); /* ~500ms at 100MHz */
  }
  return 0;
}
