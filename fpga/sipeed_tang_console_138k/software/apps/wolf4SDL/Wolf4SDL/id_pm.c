// ID_PM.C

#include "wl_def.h"
#include <unistd.h>

#ifdef WOLF_PAGE_CACHE
/*
=============================================================================

 Page cache: the VSWAP pages are read on demand into a fixed arena of
 PMPageSize slots (least recently used slot replaced). The arena comes from
 PM_CacheArena (the platform: spare VRAM on the target). A page pointer stays
 valid until the next PM_GetPage call that misses with every slot newer.
 The file is read through the file descriptor, not stdio: a miss is one seek
 and one read straight into the slot.

=============================================================================
*/

word ChunksInFile;
word PMSpriteStart;
word PMSoundStart;

bool PMSoundInfoPagePadded = false;

word *pageLengths;

static uint32_t *pageOffsets;
static int16_t  *pageSlot;          // slot of each page, -1 = not resident
static int16_t  *slotPage;          // page in each slot, -1 = free
static uint32_t *slotUse;           // last use, for the replacement
static uint32_t useClock;
static byte     *arena;
static int      slots;
static int      pagefile = -1;
uint32_t        PMMisses, PMHits, PMMissTicks;       // PMMissTicks: machine-timer ticks spent reading missed pages

uint32_t arv_mtime (void);
void     arv_gpu_wait (void);

#ifdef WOLF_GPU_SPRITES
/*
 * Sprites for the GPU's texture engine: a sprite page (compshape_t, the
 * column posts) is read into a scratch slot (the arena's last) and expanded
 * into its own slot as a 64 x 64 texture, column-major like the walls (texel
 * (column, row) at column * 64 + row); the texels outside the posts hold a
 * colour the sprite does not use. PM_GetPage leaves that colour and the
 * sprite's first and last columns in PMSpriteKey, PMSpriteLeft, PMSpriteRight.
 */
byte        PMSpriteKey;
word        PMSpriteLeft, PMSpriteRight;
static byte *slotKey;
static word *slotLeft, *slotRight;
static byte *scratch;

static int16_t PM_Short (const byte *p)
{
    return (int16_t) (p[0] | (p[1] << 8));
}

static void PM_ExpandSprite (const byte *src, byte *dst, int s)
{
    const compshape_t *sh = (const compshape_t *) src;
    uint32_t used[8] = { 0 };
    int      pass, i, key;

    for (pass = 0; pass < 2; pass++)
    {
        if (pass)
        {
            for (key = 255; key > 0 && (used[key >> 5] >> (key & 31)) & 1; key--)
                ;
            memset (dst, key, PMPageSize);
            slotKey[s] = (byte) key;
        }
        for (i = sh->leftpix; i <= sh->rightpix; i++)
        {
            const byte *cmd = src + sh->dataofs[i - sh->leftpix];
            int end, top, start;
            for (end = PM_Short (cmd) >> 1; end; cmd += 6, end = PM_Short (cmd) >> 1)
            {
                top = PM_Short (cmd + 2);
                for (start = PM_Short (cmd + 4) >> 1; start < end; start++)
                {
                    byte c = src[top + start];
                    if (pass)
                        dst[i * TEXTURESIZE + start] = c;
                    else
                        used[c >> 5] |= 1u << (c & 31);
                }
            }
        }
    }
    slotLeft[s] = sh->leftpix;
    slotRight[s] = sh->rightpix;
}
#endif

byte *PM_CacheArena (int *nslots);

void PM_Startup (void)
{
    int      i;
    int32_t  filesize;
    char     fname[13];

    snprintf (fname,sizeof(fname),"vswap.%s",extension);

    pagefile = open(fname,O_RDONLY | O_BINARY);

    if (pagefile < 0)
        CA_CannotOpen(fname);

    read (pagefile,&ChunksInFile,sizeof(ChunksInFile));
    read (pagefile,&PMSpriteStart,sizeof(PMSpriteStart));
    read (pagefile,&PMSoundStart,sizeof(PMSoundStart));

    pageOffsets = SafeMalloc((ChunksInFile + 1) * sizeof(*pageOffsets));
    read (pagefile,pageOffsets,ChunksInFile * sizeof(*pageOffsets));
    pageLengths = SafeMalloc(ChunksInFile * sizeof(*pageLengths));
    read (pagefile,pageLengths,ChunksInFile * sizeof(*pageLengths));

    filesize = lseek(pagefile,0,SEEK_END);
    pageOffsets[ChunksInFile] = filesize;

    for (i = 0; i < ChunksInFile; i++)
    {
        if (!pageOffsets[i])
            continue;           // sparse page
        if (pageOffsets[i] < pageOffsets[0] || pageOffsets[i] >= (size_t)filesize)
            Quit ("PM_Startup: Illegal page offset for page %i: %u (filesize: %u)",i,pageOffsets[i],filesize);
        if (PM_GetPageSize(i) > PMPageSize)
            Quit ("PM_Startup: page %i is larger than a cache slot",i);
    }

    arena = PM_CacheArena(&slots);
#ifdef WOLF_GPU_SPRITES
    slots--;                                        // the last slot: the scratch of the sprites
    scratch = arena + (size_t) slots * PMPageSize;
    slotKey = SafeMalloc(slots * sizeof(*slotKey));
    slotLeft = SafeMalloc(slots * sizeof(*slotLeft));
    slotRight = SafeMalloc(slots * sizeof(*slotRight));
#endif
    pageSlot = SafeMalloc(ChunksInFile * sizeof(*pageSlot));
    slotPage = SafeMalloc(slots * sizeof(*slotPage));
    slotUse = SafeMalloc(slots * sizeof(*slotUse));
    for (i = 0; i < ChunksInFile; i++)
        pageSlot[i] = -1;
    for (i = 0; i < slots; i++)
    {
        slotPage[i] = -1;
        slotUse[i] = 0;
    }
}

void PM_Shutdown (void)
{
    if (pagefile >= 0)
        close (pagefile);
    pagefile = -1;
    free (pageOffsets);
    free (pageLengths);
    free (pageSlot);
    free (slotPage);
    free (slotUse);
    pageOffsets = NULL;
    pageLengths = NULL;
}

uint32_t PM_GetPageSize (int page)
{
    if (page < 0 || page >= ChunksInFile)
        Quit ("PM_GetPageSize: Invalid page request: %i",page);
    if (!pageOffsets[page])
        return 0;               // sparse page
    if (!pageOffsets[page + 1])
        return pageLengths[page];
    return pageOffsets[page + 1] - pageOffsets[page];
}

byte *PM_GetPage (int page)
{
    int      s, i;
    uint32_t size;

    if (page < 0 || page >= ChunksInFile)
        Quit ("PM_GetPage: Invalid page request: %i",page);

    s = pageSlot[page];
    if (s < 0)
    {
        PMMisses++;
        s = 0;
        for (i = 1; i < slots; i++)
            if (slotUse[i] < slotUse[s])
                s = i;
        if (slotPage[s] >= 0)
            pageSlot[slotPage[s]] = -1;
        slotPage[s] = page;
        pageSlot[page] = s;
        size = PM_GetPageSize(page);
        if (size)
        {
            uint32_t t0 = arv_mtime();
#ifdef WOLF_GPU_WALLS
            arv_gpu_wait ();            // the GPU may still read the slot's previous page
#endif
            lseek (pagefile,pageOffsets[page],SEEK_SET);
#ifdef WOLF_GPU_SPRITES
            if (page >= PMSpriteStart && page < PMSoundStart)
            {
                read (pagefile,scratch,size);
                PM_ExpandSprite (scratch,arena + (size_t) s * PMPageSize,s);
            }
            else
#endif
            read (pagefile,arena + (size_t) s * PMPageSize,size);
            PMMissTicks += arv_mtime() - t0;
        }
    }
    else
        PMHits++;
    slotUse[s] = ++useClock;
#ifdef WOLF_GPU_SPRITES
    PMSpriteKey = slotKey[s];
    PMSpriteLeft = slotLeft[s];
    PMSpriteRight = slotRight[s];
#endif
    return arena + (size_t) s * PMPageSize;
}

// Consecutive pages read straight from the file (the digitised sounds)
void PM_ReadPages (int page, byte *dst, uint32_t size)
{
    lseek (pagefile,pageOffsets[page],SEEK_SET);
    read (pagefile,dst,size);
}

byte *PM_GetPageEnd (void)
{
    return NULL;
}

#else

word ChunksInFile;
word PMSpriteStart;
word PMSoundStart;

bool PMSoundInfoPagePadded = false;

word *pageLengths;

byte *PMPageData;
byte **PMPages;


/*
==================
=
= PM_Startup
=
==================
*/

void PM_Startup (void)
{
    int      i;
    int      padding;
    byte     *page;
    uint32_t *pageOffsets;
    uint32_t pagesize;
    int32_t  filesize,datasize;
    FILE     *file;
    char     fname[13];

    snprintf (fname,sizeof(fname),"vswap.%s",extension);

    file = fopen(fname,"rb");

    if (!file)
        CA_CannotOpen(fname);

    //
    // read in header variables
    //
    fread (&ChunksInFile,sizeof(ChunksInFile),1,file);
    fread (&PMSpriteStart,sizeof(PMSpriteStart),1,file);
    fread (&PMSoundStart,sizeof(PMSoundStart),1,file);

    //
    // read in the chunk offsets
    //
    pageOffsets = SafeMalloc((ChunksInFile + 1) * sizeof(*pageOffsets));

    fread (pageOffsets,sizeof(*pageOffsets),ChunksInFile,file);

    //
    // read in the chunk lengths
    //
    pageLengths = SafeMalloc(ChunksInFile * sizeof(*pageLengths));

    fread (pageLengths,sizeof(*pageLengths),ChunksInFile,file);

    fseek (file,0,SEEK_END);
    filesize = ftell(file);
    datasize = filesize - pageOffsets[0];

    if (datasize < 0)
        Quit ("PM_Startup: The page file \"%s\" is too large!",fname);

    pageOffsets[ChunksInFile] = filesize;

    //
    // check that all chunk offsets are valid
    //
    for (i = 0; i < ChunksInFile; i++)
    {
        if (!pageOffsets[i])
            continue;           // sparse page

        if (pageOffsets[i] < pageOffsets[0] || pageOffsets[i] >= (size_t)filesize)
            Quit ("PM_Startup: Illegal page offset for page %i: %u (filesize: %u)",i,pageOffsets[i],filesize);
    }

    //
    // calculate total amount of padding needed for sprites and sound info page
    //
    padding = 0;

    for (i = PMSpriteStart; i < PMSoundStart; i++)
    {
        if (!pageOffsets[i])
            continue;           // sparse page

        if (((pageOffsets[i] - pageOffsets[0]) + padding) & 1)
            padding++;
    }

    if (((pageOffsets[ChunksInFile - 1] - pageOffsets[0]) + padding) & 1)
        padding++;

    //
    // allocate enough memory to hold the whole page file
    //
    PMPageData = SafeMalloc(datasize + padding);

    //
    // [ChunksInFile + 1] pointers to page starts
    // the last pointer points one byte after the last page
    //
    PMPages = SafeMalloc((ChunksInFile + 1) * sizeof(*PMPages));

    //
    // load pages and initialize PMPages pointers
    //
    page = &PMPageData[0];

    for (i = 0; i < ChunksInFile; i++)
    {
        if ((i >= PMSpriteStart && i < PMSoundStart) || i == ChunksInFile - 1)
        {
            //
            // pad with zeros to make it 2-byte aligned
            //
            if ((page - &PMPageData[0]) & 1)
            {
                *page++ = 0;

                if (i == ChunksInFile - 1)
                    PMSoundInfoPagePadded = true;
            }
        }

        PMPages[i] = page;

        if (!pageOffsets[i])
            continue;               // sparse page

        //
        // use specified page length when next page is sparse
        // otherwise, calculate size from the offset difference between this and the next page
        //
        if (!pageOffsets[i + 1])
            pagesize = pageLengths[i];
        else
            pagesize = pageOffsets[i + 1] - pageOffsets[i];

        fseek (file,pageOffsets[i],SEEK_SET);
        fread (page,sizeof(*page),pagesize,file);

        page += pagesize;
    }

    //
    // last page points after page buffer
    //
    PMPages[ChunksInFile] = page;

    free (pageOffsets);

    fclose (file);
}


/*
==================
=
= PM_Shutdown
=
==================
*/

void PM_Shutdown (void)
{
    free (pageLengths);
    free (PMPages);
    free (PMPageData);

    pageLengths = NULL;
    PMPages = NULL;
    PMPageData = NULL;
}


/*
==================
=
= PM_GetPageSize
=
==================
*/

uint32_t PM_GetPageSize (int page)
{
    if (page < 0 || page >= ChunksInFile)
        Quit ("PM_GetPageSize: Invalid page request: %i",page);

    return (uint32_t)(PMPages[page + 1] - PMPages[page]);
}


/*
==================
=
= PM_GetPage
=
= Returns the address of the page
=
==================
*/

byte *PM_GetPage (int page)
{
    if (page < 0 || page >= ChunksInFile)
        Quit ("PM_GetPage: Invalid page request: %i",page);

    return PMPages[page];
}


/*
==================
=
= PM_GetPageEnd
=
= Returns the address of the last page
=
==================
*/

byte *PM_GetPageEnd (void)
{
    return PMPages[ChunksInFile];
}

#endif // WOLF_PAGE_CACHE
