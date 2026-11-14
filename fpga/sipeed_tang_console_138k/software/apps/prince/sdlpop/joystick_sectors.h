/*
 * Which sector an analog stick is in, as integer arithmetic.
 *
 * Separate from seg000.c only so that the host tests can drive exactly the code
 * the firmware runs: tools/tests/joystick_check.c compares every function here
 * against the atan2() form it replaced. Keeping them in seg000.c would mean the
 * test compared a copy against a copy.
 *
 * ---- the derivation ----
 *
 * get_joystick_state() used to compute atan2(y, x) and then compare it against
 * six fixed angles. The angle was never wanted for itself: for a comparison
 * against a fixed ray, only the side matters. The stick is inside the cone of
 * half-angle t about an axis when its off-axis component is smaller than its
 * on-axis one times tan(t), and squaring both sides of that makes it integer
 * arithmetic with no trigonometry and no rounding:
 *
 *   |angle| < 60          x > 0 && y*y < 3*x*x       tan^2 60 is exactly 3
 *   |angle| > 120         x < 0 && y*y < 3*x*x
 *   -150 < angle < -30    y < 0 && x*x < 3*y*y
 *     35 < angle < 145    y > 0 && x*x < y*y * tan^2 55
 *
 * The three 60-degree rays come out exact rather than approximated, because
 * tan^2(60) is 3. tan^2(55) is irrational and gets the rational below.
 *
 * Note that `angle > 0` is NOT the same as y > 0: atan2 returns +pi along -x, so
 * y == 0 with x < 0 is a positive angle, and that case is reachable here since it
 * is neither up nor down. `angle < 0` really is just y < 0, because integer y of
 * 0 gives +0.0 and so an angle of 0 or +pi, never -pi.
 *
 * int64_t throughout: 3*x*x overflows a signed 32-bit int at full deflection,
 * which is the same overflow the dead-zone test in get_joystick_state() carries
 * an upstream fix for.
 */
#ifndef JOYSTICK_SECTORS_H
#define JOYSTICK_SECTORS_H

#include <stdint.h>

/*
 * tan^2(55 degrees) as a rational: 138577/67943 = 2.039606729170 against
 * 2.039606729161. It is 8.6e-12 high, which puts the band of x*x it could decide
 * wrongly at y*y * 8.6e-12 <= 0.009 wide - narrower than the gap between
 * consecutive integers, so no integer input can fall inside it. A coarser
 * approximation does not have that property: 7261/3560 is 1.2e-8 high and gets
 * two integer pairs wrong, both within 1e-7 of a degree of the boundary.
 */
#define JOY_TAN2_55_NUM 138577
#define JOY_TAN2_55_DEN  67943

/* 120-degree cone about +x: atan2(y,x) in (-60, 60) */
static inline int joy_sector_right(int x, int y) {
	return x > 0 && (int64_t)y * y < 3 * (int64_t)x * x;
}

/* 120-degree cone about -x: |atan2(y,x)| > 120 */
static inline int joy_sector_left(int x, int y) {
	return x < 0 && (int64_t)y * y < 3 * (int64_t)x * x;
}

/* 120-degree cone about -y: atan2(y,x) in (-150, -30) */
static inline int joy_sector_up(int x, int y) {
	return y < 0 && (int64_t)x * x < 3 * (int64_t)y * y;
}

/* 110-degree cone about +y: atan2(y,x) in (35, 145) */
static inline int joy_sector_down(int x, int y) {
	return y > 0 && (int64_t)x * x * JOY_TAN2_55_DEN
	              < (int64_t)y * y * JOY_TAN2_55_NUM;
}

/* atan2(y,x) < 0 */
static inline int joy_facing_up(int x, int y) {
	(void)x;
	return y < 0;
}

/* atan2(y,x) > 0 - see the note above about +pi along -x */
static inline int joy_facing_down(int x, int y) {
	return y > 0 || (y == 0 && x < 0);
}

#endif // JOYSTICK_SECTORS_H
