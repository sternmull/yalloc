#ifndef YALLOC_INTERNALS_H
#define YALLOC_INTERNALS_H

#include <stdint.h>

typedef struct
{
  uint16_t prev; // low bit set if free
  uint16_t next; // for used blocks: low bit set if unused header at the end
} Header;

// NOTE: We have 32bit aligned data and 16bit offsets where the lowest bit is used as flag. So we remove the low bit and shift by 1 to address 128k bytes with the 15bit significant offset bits.

#define NIL 0xFFFEu

// return Header-address for a prev/next
#define HDR_PTR(offset) ((Header*)((char*)pool + (((offset) & NIL)<<1)))

// return a prev/next for a Header-address
#define HDR_OFFSET(blockPtr) ((uint16_t)(((char*)blockPtr - (char*)pool) >> 1))

#ifndef YALLOC_INTERNAL_VALIDATE
# ifdef NDEBUG
#   define YALLOC_INTERNAL_VALIDATE 0
# else
#   define YALLOC_INTERNAL_VALIDATE 1
#endif
#endif


/*
internal_assert() is used in some places to check internal expections.
Activate this if you modify the code to detect problems as early as possible.
In other cases this should be deactivated.
*/
#if 0
#define internal_assert assert
#else
#define internal_assert(condition)((void) 0)
#endif

// detects offsets that point nowhere
static inline int isNil(uint16_t offset)
{
  return (offset | 1) == 0xFFFF;
}

static inline int isFree(Header * hdr)
{
  return hdr->prev & 1;
}

static inline int isPadded(Header * hdr)
{
  return hdr->next & 1;
}


#endif // YALLOC_INTERNALS_H
