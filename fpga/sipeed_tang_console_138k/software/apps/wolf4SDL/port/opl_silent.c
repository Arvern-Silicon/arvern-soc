/* Silent stand-in for the MAME OPL2 emulator (mame/fmopl.c): the AdLib music
 * and sound effects are replaced by pre-rendered audio on the target */
#include <string.h>
#include "mame/fmopl.h"

int  YM3812Init(int num, int clock, int rate) { (void) num; (void) clock; (void) rate; return 0; }
void YM3812Shutdown(void) {}
void YM3812ResetChip(int which) { (void) which; }
int  YM3812Write(int which, int a, int v) { (void) which; (void) a; (void) v; return 0; }
void YM3812UpdateOne(int which, INT16 *buffer, int length) { (void) which; memset(buffer, 0, (size_t) length * sizeof(*buffer)); }
