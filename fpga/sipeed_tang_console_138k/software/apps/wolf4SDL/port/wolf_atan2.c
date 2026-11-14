/* Integer replacement for the actors' angle computation
 *     angle = atan2(dy, dx); if (angle < 0) angle += 2 pi; iangle = angle / 2 pi * ANGLES
 * (wl_act2.c): the whole number of degrees, 0-359, without floating point */
#include <stdint.h>

/* tan(k degrees) in Q24, k = 1 .. 89 */
static const uint32_t tan_q24[89] = {
#include "tan_q24.inc"
};

/* floor(atan(ay / ax) in degrees) for ax > 0, ay >= 0: 0 .. 89 */
static int base_degrees(uint32_t ay, uint32_t ax)
{
    uint64_t num = (uint64_t) ay << 24;
    int lo = 0, hi = 89;                       /* answer in [lo, hi] */
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if ((uint64_t) tan_q24[mid - 1] * ax <= num) lo = mid; else hi = mid - 1;
    }
    return lo;
}

int wolf_atan2_deg(int32_t dy, int32_t dx)
{
    uint32_t ax = dx < 0 ? 0u - (uint32_t) dx : (uint32_t) dx;
    uint32_t ay = dy < 0 ? 0u - (uint32_t) dy : (uint32_t) dy;
    int      d, exact;

    if (ax == 0 && ay == 0) return 0;
    if (ax == 0) { d = 90; exact = 1; }
    else { d = base_degrees(ay, ax); exact = (ay == 0) || (ay == ax); }
    /* exact: the angle in the quadrant is a whole number of degrees (0, 45, 90) */
    if (dx >= 0 && dy >= 0) return d % 360;                       /* 0 .. 90    */
    if (dx < 0 && dy >= 0)  return exact ? 180 - d : 179 - d;     /* 90 .. 180  */
    /* dy < 0: the original adds 2 pi in single precision, which lands just
     * below a whole degree (225 -> 224, 315 -> 314); reproduced for the demos */
    if (dx < 0)             return exact ? 179 + d : 180 + d;     /* 180 .. 270 */
    return (ax == 0) ? 270 : 359 - d;                             /* 270 .. 360 */
}
