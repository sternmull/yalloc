#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "yalloc/yalloc.h"
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <memory.h>

/*
This functions are used by the test-programs as wrappers to yalloc_alloc/free. On allocation
a pseudorandom sequence of bytes is written to the allocated block. The first 2 bytes are a
base seed that comes from an incrementing global variable. The block size is added to get the
effective seed for the PRNG. This gives the following useful properties:

 - the allocator is likely to explode if it ever does accidently considers user-data as part of its internal structure
 - free can detect if the user data was unexpectedly modified (by checking the pseudorandom sequence that is derived from the first 16bit)
 - free can detect when the reported size of a block has changed (because the size is part of the effective seed)
 - As long as there are at most 0xffff allocations per test (and each test uses its own pool) then blocks will have unique first 16bit.
   This means we can detect if blocks where accidently duplicated by looking for other blocks with the same first 16 bit.

Another way would be to use the address of a block as seed. But that does not work well when compacting the pool (which,
of course, changes the address of the blocks).

WARNING: Because checked_free() iterates over ALL used blocks (to see if it finds duplicates) you can not mix
checked_alloc with raw yalloc_alloc() in a pool (because those "manually" allocated blocks may contain uninitialized
memory or have content that confuses the checking-logic!).
*/

static void * checked_alloc(void * pool, size_t size)
{
  static uint16_t allocSeed = 0xabcd;
  void * p = yalloc_alloc(pool, size);
  if (p)
  {
    size_t allocSize = yalloc_block_size(pool, p);
    assert(allocSize >= size);
    assert(allocSize % 4 == 0);

    memcpy(p, &allocSeed, 2);

    srand(allocSeed + allocSize); // add blocksize to the seed (so blocks with same seed but different size will have different content)
    ++allocSeed;

    for (size_t i = 2; i < allocSize; ++i)
      ((uint8_t*)p)[i] = (uint8_t)rand();
  }
  return p;
}

static void checked_free(void * pool, void * p)
{
  if (p)
  {
    size_t allocSize = yalloc_block_size(pool, p);
    assert(allocSize >= 4);
    uint16_t alloc_seed;
    memcpy(&alloc_seed, p, 2);
    srand(alloc_seed + allocSize);
    for (size_t i = 2; i < allocSize; ++i)
      assert(((uint8_t*)p)[i] == (uint8_t)rand());

    // Check if there are no duplicates in the pool (by checking the initial 16bit counter of all used blocks)
    int hits = 0;
    for (uint16_t * x = yalloc_first_used(pool); x; x = yalloc_next_used(pool, x))
    {
      if (*x == alloc_seed)
        ++hits;
    }

    assert(hits == 1);
  }
  yalloc_free(pool, p);
}

#endif // TEST_UTIL_H
