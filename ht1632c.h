#ifndef HT1632C_H
#define HT1632C_H

//#include <inttypes.h>
#include <SPI.h>


#define HT1632_CMD_8NMOS 0x20	/* CMD= 0010-ABxx-x commons options */
#define HT1632_CMD_16NMOS 0x24	/* CMD= 0010-ABxx-x commons options */
#define HT1632_CMD_8PMOS 0x28	/* CMD= 0010-ABxx-x commons options */
#define HT1632_CMD_16PMOS 0x2C	/* CMD= 0010-ABxx-x commons options */

#define HT1632_ID_CMD        4  /* ID = 100 - Commands */
#define HT1632_ID_RD         6  /* ID = 110 - Read RAM */
#define HT1632_ID_WR         5  /* ID = 101 - Write RAM */

#define HT1632_CMD_SYSDIS 0x00  /* CMD= 0000-0000-x Turn off oscil */
#define HT1632_CMD_SYSON  0x01  /* CMD= 0000-0001-x Enable system oscil */
#define HT1632_CMD_LEDOFF 0x02  /* CMD= 0000-0010-x LED duty cycle gen off */
#define HT1632_CMD_LEDON  0x03  /* CMD= 0000-0011-x LEDs ON */
#define HT1632_CMD_BLOFF  0x08  /* CMD= 0000-1000-x Blink ON */
#define HT1632_CMD_BLON   0x09  /* CMD= 0000-1001-x Blink Off */
#define HT1632_CMD_SLVMD  0x10  /* CMD= 0001-00xx-x Slave Mode */
#define HT1632_CMD_MSTMD  0x14  /* CMD= 0001-01xx-x Master Mode */
#define HT1632_CMD_RCCLK  0x18  /* CMD= 0001-10xx-x Use on-chip clock */
#define HT1632_CMD_EXTCLK 0x1C  /* CMD= 0001-11xx-x Use external clock */
#define HT1632_CMD_PWM    0xA0  /* CMD= 101x-PPPP-x PWM duty cycle */

#define HT1632_ID_LEN     3  /* IDs are 3 bits */
#define HT1632_CMD_LEN    8  /* CMDs are 8 bits */
#define HT1632_DATA_LEN   8  /* Data are 4*2 bits */
#define HT1632_ADDR_LEN   7  /* Address are 7 bits */

// panel parameters
#define PANEL_HEADER_BITS (HT1632_ID_LEN + HT1632_ADDR_LEN)

#define toBit(v) ((v) ? 1 : 0)

//
// public functions
//

/// Initializes library and display.
/// Commons mode is either 8/16 NMOS/PMOS (see #define)
int ht1632c_init(const uint8_t commonsMode);

/// Shuts down library.
int ht1632c_close();

/// Sets display brightness.
void ht1632c_pwm(const uint8_t value);

/// Sends frame buffer to display; required to bring any drawing operations to the display.
void ht1632c_sendframe();

/// Clears the whole frame. Also reset clipping area.
void ht1632c_clear();

/// Puts a single value at HT1632 addr, at COM bit index
/// Note: only sets bit in frame buffer. a call to ht1632_sendframe() is required to update the display.
void ht1632c_update_framebuffer(const int addr, const uint8_t bitIndex, const uint8_t bitValue);

uint8_t ht1632c_get_framebuffer(const int addr, const uint8_t bitIndex);

#endif
