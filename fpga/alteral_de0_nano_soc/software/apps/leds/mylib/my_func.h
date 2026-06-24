
//=============================================================================
// PERIPHERALS REGISTER DEFINITIONS
//=============================================================================

//----------------------------------------------------------
// PERIPHERAL #0 (LEDs / Keys / Switches)
//----------------------------------------------------------
#define  P1_LED_CTRL        (*(volatile unsigned int *) 0x10040000)    // LED output control [7:0]
#define  P1_TTY_DATA        (*(volatile unsigned int *) 0x10040004)    // TTY/printf data register
#define  P1_KEY_SW_VAL      (*(volatile unsigned int *) 0x10040020)    // Debounced key/sw values [5:0] (read-only)
#define  P1_KEY_SW_IRQ_EN   (*(volatile unsigned int *) 0x10040024)    // IRQ enable per input [5:0]
#define  P1_KEY_SW_IRQ_EDGE (*(volatile unsigned int *) 0x10040028)    // IRQ edge polarity [5:0] (1=rising, 0=falling)
#define  P1_KEY_SW_IRQ_BOTH (*(volatile unsigned int *) 0x1004002C)    // Any-edge select [5:0] (1=both edges, overrides EDGE)
#define  P1_KEY_SW_IRQ_VAL  (*(volatile unsigned int *) 0x10040030)    // IRQ status, write-1-to-clear [5:0]

// Backward compatibility aliases
#define  P1_OUT0       P1_LED_CTRL
#define  P1_OUT1       P1_TTY_DATA
#define  P1_IN0        P1_KEY_SW_VAL

//----------------------------------------------------------
// ACLINT MTIMER (machine timer), base 0x02004000 (single hart)
//----------------------------------------------------------
#define  MTIMECMP_LO   (*(volatile unsigned int *) 0x02004000)    // mtimecmp[0] low  word
#define  MTIMECMP_HI   (*(volatile unsigned int *) 0x02004004)    // mtimecmp[0] high word
#define  MTIME_LO      (*(volatile unsigned int *) 0x02004008)    // mtime low  word (read LO first: latches HI snapshot)
#define  MTIME_HI      (*(volatile unsigned int *) 0x0200400C)    // mtime high word

//----------------------------------------------------------
// PERIPHERAL #1 (Unused)
//----------------------------------------------------------
#define  P2_OUT0       (*(volatile unsigned int *) 0x10041000)
#define  P2_OUT1       (*(volatile unsigned int *) 0x10041004)
#define  P2_OUT2       (*(volatile unsigned int *) 0x10041008)
#define  P2_OUT3       (*(volatile unsigned int *) 0x1004100C)
#define  P2_OUT4       (*(volatile unsigned int *) 0x10041010)
#define  P2_OUT5       (*(volatile unsigned int *) 0x10041014)
#define  P2_OUT6       (*(volatile unsigned int *) 0x10041018)
#define  P2_OUT7       (*(volatile unsigned int *) 0x1004101C)

#define  P2_IN0        (*(volatile unsigned int *) 0x10041020)
#define  P2_IN1        (*(volatile unsigned int *) 0x10041024)
#define  P2_IN2        (*(volatile unsigned int *) 0x10041028)
#define  P2_IN3        (*(volatile unsigned int *) 0x1004102C)
#define  P2_IN4        (*(volatile unsigned int *) 0x10041030)
#define  P2_IN5        (*(volatile unsigned int *) 0x10041034)
#define  P2_IN6        (*(volatile unsigned int *) 0x10041038)
#define  P2_IN7        (*(volatile unsigned int *) 0x1004103C)


//=============================================================================
// PERIPHERAL MAPPING for FPGA
//=============================================================================
//
//  P1_LED_CTRL[7:0]    --> LED[7:0]
//  P1_KEY_SW_VAL[3:0]  <-- SW[3:0]  (debounced)
//  P1_KEY_SW_VAL[5:4]  <-- KEY[1:0] (debounced)
//


//=============================================================================
// FUNCTIONS
//=============================================================================

// Replace printf statements with custom one
#include "cprintf.h"
#define   printf  cprintf
