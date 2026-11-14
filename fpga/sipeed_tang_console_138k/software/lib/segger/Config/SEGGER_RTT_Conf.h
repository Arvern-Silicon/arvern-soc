/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************
*                                                                    *
*        SEGGER RTT * Real Time Transfer for embedded targets        *
*                  https://github.com/SEGGERMicro/RTT                *
*                                                                    *
**********************************************************************

---------------------------END-OF-HEADER------------------------------
Purpose : User configuration file for RTT.
          For available configuration,
          refer to SEGGER_RTT_ConfDefaults.h.

----------------------------------------------------------------------
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H


/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
//
// aRVern local configuration.
//
// This is the ONLY file under lib/segger/ that carries local edits: upstream
// ships it deliberately empty for exactly this purpose, and every tunable has a
// default in RTT/SEGGER_RTT_ConfDefaults.h. Keeping settings here means the rest
// of the vendored tree stays byte-identical to
// https://github.com/SEGGERMicro/RTT and can be refreshed by re-downloading.
// Re-apply this block after any such refresh.
//
// One up and one down channel: channel 0 is the terminal both ways. The default
// of three of each only costs RAM we have no use for on a 32 KiB part.
//
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS     (1)
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS   (1)

#define BUFFER_SIZE_UP                    (512)    // target -> host (printf)
#define BUFFER_SIZE_DOWN                  (32)     // host -> target (commands)

//
// SKIP, never BLOCK: nothing drains the up-buffer unless a debugger is attached,
// and this firmware is an interrupt-driven LED demo. Blocking would freeze the
// light show whenever no host is listening -- which is most of the time.
//
#define SEGGER_RTT_MODE_DEFAULT           SEGGER_RTT_MODE_NO_BLOCK_SKIP

#endif
/*************************** End of file ****************************/
