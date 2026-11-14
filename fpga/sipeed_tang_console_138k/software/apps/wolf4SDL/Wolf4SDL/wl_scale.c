// WL_SCALE.C

#include "wl_def.h"

#ifdef USE_SHADING
#include "wl_shade.h"
#endif


/*
=============================================================================

                              SPRITE DRAWING ROUTINES

=============================================================================
*/


/*
===================
=
= ScaleLine
=
= Reconstruct a sprite and draw it
=
= each vertical line of the shape has a pointer to segment data:
= 	end of segment pixel*2 (0 terminates line)
= 	top of virtual line with segment in proper place
=	start of segment pixel*2
=	<repeat>
=
===================
*/

void ScaleLine (int16_t x, int16_t toppix, fixed fracstep, byte *linesrc, byte *linecmds, byte *curshades)
{
    byte    *src,*dest;
    byte    col;
    int16_t start,end,top;
    int16_t startpix,endpix;
    fixed   frac;

    for (end = ReadShort(linecmds) >> 1; end; end = ReadShort(linecmds) >> 1)
    {
        top = ReadShort(linecmds + 2);
        start = ReadShort(linecmds + 4) >> 1;

        frac = start * fracstep;

        endpix = (frac >> FRACBITS) + toppix;

        for (src = &linesrc[top + start]; start != end; start++, src++)
        {
            startpix = endpix;

            if (startpix >= viewheight)
                break;                          // off the bottom of the view area

            frac += fracstep;
            endpix = (frac >> FRACBITS) + toppix;

            if (endpix < 0)
                continue;                       // not into the view area

            if (startpix < 0)
                startpix = 0;                   // clip upper boundary

            if (endpix > viewheight)
                endpix = viewheight;            // clip lower boundary

#ifdef USE_SHADING
            if (curshades)
                col = curshades[*src];
            else
#endif
                col = *src;

            dest = vbuf + ylookup[startpix] + x;

            while (startpix < endpix)
            {
                *dest = col;
                dest += bufferPitch;
                startpix++;
            }
        }

        linecmds += 6;                          // next segment list
    }
}


#ifdef WOLF_GPU_SPRITES
/*
=============================================================================

 Sprites drawn by the GPU's texture engine: the page cache holds each sprite
 expanded into a 64 x 64 texture, column-major, the texels outside its posts
 equal to PMSpriteKey (id_pm.c). A screen column of a sprite is one textured
 span over the sprite's 2 * scale pixels, the key's texels skipped, queued
 behind the walls. Below 3 pixels of half height (the step does not fit the
 GPU's 5.11 format) the CPU draws the column, once the GPU is done.

=============================================================================
*/

extern byte PMSpriteKey;
extern word PMSpriteLeft, PMSpriteRight;
void arv_gpu_sprite_post (byte *dst, unsigned n, const byte *src, unsigned half, unsigned skip, byte key);
void arv_gpu_wait (void);

static void SpriteColumn (int x, int toppix, int scale, const byte *col, byte key)
{
    int top = toppix, bot = toppix + 2 * scale, y;

    if (top < 0)
        top = 0;
    if (bot > viewheight)
        bot = viewheight;
    if (bot <= top)
        return;
    if (scale >= 3)
    {
        arv_gpu_sprite_post (vbuf + ylookup[top] + x, bot - top, col, scale, top - toppix, key);
        return;
    }
    for (y = top; y < bot; y++)
    {
        byte c = col[((y - toppix) * TEXTURESIZE) / (2 * scale)];
        if (c != key)
            vbuf[ylookup[y] + x] = c;
    }
}

static void DrawSprite (int xcenter, int shapenum, int16_t scale, int height)
{
    int     i, left, right;
    byte    *tex, key;
    int16_t toppix, x1, x2, actx;
    fixed   frac, fracstep;

    tex = PM_GetSpritePage (shapenum);
    key = PMSpriteKey;
    left = PMSpriteLeft;
    right = PMSpriteRight;
    if (scale < 3)
        arv_gpu_wait ();                // the CPU draws this one: after the GPU's queue

    fracstep = scale*((1<<FRACBITS)/(TEXTURESIZE/2));     // FixedDiv by TEXTURESIZE/2: exact
    frac = left * fracstep;
    actx = xcenter - scale;
    toppix = centery - scale;
    x2 = (frac >> FRACBITS) + actx;

    for (i = left; i <= right; i++)
    {
        x1 = x2;
        if (x1 >= viewwidth)
            break;                      // off the right side of the view area
        frac += fracstep;
        x2 = (frac >> FRACBITS) + actx;
        if (x2 < 0)
            continue;                   // not into the view area
        if (x1 < 0)
            x1 = 0;
        if (x2 > viewwidth)
            x2 = viewwidth;
        for (; x1 < x2; x1++)
            if (height < 0 || wallheight[x1] < height)     // height < 0: no wall test (the weapon)
                SpriteColumn (x1, toppix, scale, tex + i * TEXTURESIZE, key);
    }
}

void ScaleShape (int xcenter, int shapenum, int height, uint32_t flags)
{
    int16_t scale = height >> 3;        // low three bits are fractional
    (void) flags;
    if (scale)
        DrawSprite (xcenter, shapenum, scale, height);
}

void SimpleScaleShape (int xcenter, int shapenum, int height)
{
    int16_t scale = height >> 1;
    if (scale)
        DrawSprite (xcenter, shapenum, scale, -1);
}

#else

/*
===================
=
= ScaleShape
=
= Draws a compiled shape at [scale] pixels high
=
===================
*/

void ScaleShape (int xcenter, int shapenum, int height, uint32_t flags)
{
    int         i;
    compshape_t *shape;
    byte        *linesrc,*linecmds;
    byte        *curshades = NULL;
    int16_t     scale,toppix;
    int16_t     x1,x2,actx;
    fixed       frac,fracstep;

    scale = height >> 3;        // low three bits are fractional

    if (!scale)
        return;                 // too close or far away

    linesrc = PM_GetSpritePage(shapenum);
    shape = (compshape_t *)linesrc;

#ifdef USE_SHADING
    if (flags & FL_FULLBRIGHT)
        curshades = shadetable[0];
    else
        curshades = shadetable[GetShade(height)];
#endif

    fracstep = scale*((1<<FRACBITS)/(TEXTURESIZE/2));     // FixedDiv by TEXTURESIZE/2: exact
    frac = shape->leftpix * fracstep;

    actx = xcenter - scale;
    toppix = centery - scale;

    x2 = (frac >> FRACBITS) + actx;

    for (i = shape->leftpix; i <= shape->rightpix; i++)
    {
        //
        // calculate edges of the shape
        //
        x1 = x2;

        if (x1 >= viewwidth)
            break;                // off the right side of the view area

        frac += fracstep;
        x2 = (frac >> FRACBITS) + actx;

        if (x2 < 0)
            continue;             // not into the view area

        if (x1 < 0)
            x1 = 0;               // clip left boundary

        if (x2 > viewwidth)
            x2 = viewwidth;       // clip right boundary

        while (x1 < x2)
        {
            if (wallheight[x1] < height)
            {
                linecmds = &linesrc[shape->dataofs[i - shape->leftpix]];

                ScaleLine (x1,toppix,fracstep,linesrc,linecmds,curshades);
            }

            x1++;
        }
    }
}


/*
===================
=
= SimpleScaleShape
=
= NO CLIPPING, height in pixels
=
= Draws a compiled shape at [scale] pixels high
=
===================
*/

void SimpleScaleShape (int xcenter, int shapenum, int height)
{
    int         i;
    compshape_t *shape;
    byte        *linesrc,*linecmds;
    int16_t     scale,toppix;
    int16_t     x1,x2,actx;
    fixed       frac,fracstep;

    scale = height >> 1;

    linesrc = PM_GetSpritePage(shapenum);
    shape = (compshape_t *)linesrc;

    fracstep = scale*((1<<FRACBITS)/(TEXTURESIZE/2));     // FixedDiv by TEXTURESIZE/2: exact
    frac = shape->leftpix * fracstep;

    actx = xcenter - scale;
    toppix = centery - scale;

    x2 = (frac >> FRACBITS) + actx;

    for (i = shape->leftpix; i <= shape->rightpix; i++)
    {
        //
        // calculate edges of the shape
        //
        x1 = x2;

        frac += fracstep;
        x2 = (frac >> FRACBITS) + actx;

        while (x1 < x2)
        {
            linecmds = &linesrc[shape->dataofs[i - shape->leftpix]];

            ScaleLine (x1,toppix,fracstep,linesrc,linecmds,NULL);

            x1++;
        }
    }
}

#endif // WOLF_GPU_SPRITES

#ifdef USE_DIR3DSPR

/*
===================
=
= Scale3DShape
=
= Draws a compiled 3D shape at [scale1] pixels high
=
===================
*/

void Scale3DShape (visobj_t *sprite, int x1, int x2, fixed ny1, fixed ny2, fixed nx1, fixed nx2)
{
    int         i;
    compshape_t *shape;
    byte        *linesrc,*linecmds;
    byte        *curshades;
    int16_t     scale1,toppix;
    int16_t     dx,len,slinex;
    int16_t     xpos[TEXTURESIZE + 1];
    fixed       height,dheight,height1,height2;
    fixed       fracstep;
    fixed       dxx,dzz;
    fixed       dxa,dza;

    linesrc = PM_GetSpritePage(sprite->shapenum);
    shape = (compshape_t *)linesrc;

    len = shape->rightpix - shape->leftpix + 1;

    if (!len)
        return;

    dxx = (ny2 - ny1) << 8;
    dzz = (nx2 - nx1) << 8;

    ny1 += dxx >> 9;
    nx1 += dzz >> 9;

    dxa = -(dxx >> 1);
    dza = -(dzz >> 1);
    dxx >>= TEXTURESHIFT;
    dzz >>= TEXTURESHIFT;
    dxa += shape->leftpix * dxx;
    dza += shape->leftpix * dzz;

    xpos[0] = (ny1 + (dxa >> 8)) * scale / (nx1 + (dza >> 8)) + centerx;
    height1 = heightnumerator / ((nx1 + (dza >> 8)) >> 8);
    height = (((fixed)height1) << 12) + 2048;

    for (i = 1; i <= len; i++)
    {
        dxa += dxx;
        dza += dzz;
        xpos[i] = (ny1 + (dxa >> 8)) * scale / (nx1 + (dza >> 8)) + centerx;

        if (xpos[i - 1] > viewwidth)
            break;
    }

    len = i - 1;
    dx = xpos[len] - xpos[0];

    if (!dx)
        return;

    height2 = heightnumerator / ((nx1 + (dza >> 8)) >> 8);
    dheight = (((fixed)height2 - (fixed)height1) << 12) / (fixed)dx;

    if (x2 > viewwidth)
        x2 = viewwidth;

    for (i = 0; i < len; i++)
    {
        for (slinex = xpos[i]; slinex < xpos[i + 1] && slinex < x2; slinex++)
        {
            height += dheight;

            if (slinex < 0)
                continue;

            scale1 = height >> 15;

            if (!scale1)
                continue;

            if (wallheight[slinex] < (height >> 12))
            {
#ifdef USE_SHADING
                if (sprite->flags & FL_FULLBRIGHT)
                    curshades = shadetable[0];
                else
                    curshades = shadetable[GetShade(scale1 << 3)];
#endif
                fracstep = scale1*((1<<FRACBITS)/(TEXTURESIZE/2));    // FixedDiv by TEXTURESIZE/2: exact
                toppix = centery - scale1;

                linecmds = &linesrc[shape->dataofs[i]];

                ScaleLine (slinex,toppix,fracstep,linesrc,linecmds,curshades);
            }
        }
    }
}


/*
========================
=
= Transform3DShape
=
========================
*/

void Transform3DShape (visobj_t *sprite)
{
    #define SIZEADD 1024

    fixed nx1,nx2,ny1,ny2;
    int   viewx1,viewx2;
    fixed diradd;
    fixed gy1,gy2,gx,gyt1,gyt2,gxt;
    fixed gx1,gx2,gy,gxt1,gxt2,gyt;

    //
    // the following values for "diradd" aren't optimized yet
    // if you have problems with sprites being visible through wall edges
    // where they shouldn't, you can try to adjust these values and SIZEADD
    //
    switch (sprite->flags & FL_DIR_POS_MASK)
    {
        case FL_DIR_POS_FW: diradd = 0x7ff0 + 0x8000; break;
        case FL_DIR_POS_BW: diradd = -0x7ff0 + 0x8000; break;
        case FL_DIR_POS_MID: diradd = 0x8000; break;

        default:
            Quit ("Bad 3D sprite dir at %dx%d (shapenum = %d)",sprite->tilex,sprite->tiley,sprite->shapenum);
    }

    if (sprite->flags & FL_DIR_VERT_FLAG)
    {
        //
        // translate point to view centered coordinates
        //
        gy1 = (((fixed)sprite->tiley) << TILESHIFT) + 0x8000 - viewy - 0x8000L - SIZEADD;
        gy2 = gy1 + 0x10000L + (2 * SIZEADD);
        gx = (((fixed)sprite->tilex) << TILESHIFT) + diradd - viewx;

        //
        // calculate nx
        //
        gxt = FixedMul(gx,viewcos);
        gyt1 = FixedMul(gy1,viewsin);
        gyt2 = FixedMul(gy2,viewsin);
        nx1 = gxt - gyt1;
        nx2 = gxt - gyt2;

        //
        // calculate ny
        //
        gxt = FixedMul(gx,viewsin);
        gyt1 = FixedMul(gy1,viewcos);
        gyt2 = FixedMul(gy2,viewcos);
        ny1 = gyt1 + gxt;
        ny2 = gyt2 + gxt;
    }
    else
    {

        //
        // translate point to view centered coordinates
        //
        gx1 = (((fixed)sprite->tilex) << TILESHIFT) + 0x8000 - viewx - 0x8000L - SIZEADD;
        gx2 = gx1 + 0x10000L + (2 * SIZEADD);
        gy = (((fixed)sprite->tiley) << TILESHIFT) + diradd - viewy;

        //
        // calculate nx
        //
        gxt1 = FixedMul(gx1,viewcos);
        gxt2 = FixedMul(gx2,viewcos);
        gyt = FixedMul(gy,viewsin);
        nx1 = gxt1 - gyt;
        nx2 = gxt2 - gyt;

        //
        // calculate ny
        //
        gxt1 = FixedMul(gx1,viewsin);
        gxt2 = FixedMul(gx2,viewsin);
        gyt = FixedMul(gy,viewcos);
        ny1 = gyt + gxt1;
        ny2 = gyt + gxt2;
    }

    if (nx1 < 0 || nx2 < 0)
        return;              // TODO: Clip on viewplane

    //
    // calculate perspective ratio
    //
    if (nx1 >= 0 && nx1 <= 1792) nx1 = 1792;
    if (nx1 < 0 && nx1 >= -1792) nx1 = -1792;
    if (nx2 >= 0 && nx2 <= 1792) nx2 = 1792;
    if (nx2 < 0 && nx2 >= -1792) nx2 = -1792;

    viewx1 = (int)(centerx + ny1 * scale / nx1);
    viewx2 = (int)(centerx + ny2 * scale / nx2);

    if (viewx2 < viewx1)
        Scale3DShape (sprite,viewx2,viewx1,ny2,ny1,nx2,nx1);
    else
        Scale3DShape (sprite,viewx1,viewx2,ny1,ny2,nx1,nx2);
}

#endif
