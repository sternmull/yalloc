#include "yalloc/yalloc.h"
#include "yalloc/yalloc_internals.h"
#include "test_util.h"
#include <stdint.h>
#include <memory.h>
#include <assert.h>

typedef struct
{
  uint16_t size;
  uint16_t tStart;
  uint16_t tDuration;
} RawStep;

typedef struct
{
  void * p;
  uint32_t size;
  uint32_t tStart;
  uint32_t tEnd;
} Step;

size_t ceil4(size_t i)
{
  while (i % 4)
    ++i;

  return i;
}

void fuzzerFunc(const uint8_t * data, size_t size)
{
  /*
  This test function is given a bunch of random bytes. Here is how we convert that to a sequence of free/alloc calls:

  Interpret the random blob as a pool size (first 4 bytes) followed by a vector of triplets that represent allocations, their parameters are:

   - size of the allocation
   - time when to do the allocation
   - duration after which the allocation will be freed

  This is valid data for all possible inputs.
  Its structure should give the fuzzer a good chance to mutate/crossover the input vectors in a meaningful way to explore all the code paths.
  */

  { // initialization with unsupported pool size must fail
    uint32_t dummy[128];
    assert(yalloc_init(dummy, MAX_POOL_SIZE + 1));
    assert(yalloc_init(dummy, MAX_POOL_SIZE + 2));
    assert(yalloc_init(dummy, MAX_POOL_SIZE + 100));
  }

  // initialize a pool
  if (size < 4)
    return;

  uint32_t poolSize;
  memcpy(&poolSize, data, 4);
  data += 4;
  size -= 4;

  poolSize %= MAX_POOL_SIZE; // Map the 32bit input size to a valid pool size

  uint32_t pool[ceil4(poolSize) / 4];
  if (yalloc_init(pool, poolSize))
  {
    assert(poolSize < sizeof(Header) * 3);
    return;
  }

  { // trigger some trivial codepaths that can not be reached by the rest of the test
    assert(!yalloc_first_used(pool)); // get first used block when there is none

    yalloc_defrag_start(pool);
    assert(!yalloc_defrag_address(pool, NULL)); // request defragmented address of NULL
    yalloc_defrag_commit(pool);
  }

  uint32_t freeBytes = yalloc_count_free(pool); // counts the bytes that the pool claims to be free to allocate user data
  assert(freeBytes == (poolSize / 4) * 4 - 2 * sizeof(Header));

  int numAllocs = size / sizeof(RawStep);

  assert(numAllocs <= 0xFFFF); // uniqueness-test of checked_alloc/free will fail when there are more than 64k allocs for a pool! Tests only are guaranteed to run successfully when run with lesser allocations.

  if (!numAllocs)
  {
    yalloc_deinit(pool);
    return;
  }

  RawStep rawSteps[numAllocs];
  memcpy(rawSteps, data, numAllocs * sizeof(RawStep)); // copy the random bits into the "raw step vector" for which all possible values result in a meaningful input

  Step allocs[numAllocs];

  Step * starts[numAllocs + 1]; // allocations, sorted by allocation-time
  Step * ends[numAllocs + 1]; // allocations, sorted by deallocation-time

  // translate the random input to some test-data
  for (int i = 0; i < numAllocs; ++i)
  {
    starts[i] = ends[i] = &allocs[i]; // initialize starts/ends unsorted
    allocs[i].p = NULL;
    allocs[i].size = rawSteps[i].size;
    allocs[i].tStart = rawSteps[i].tStart;
    allocs[i].tEnd = rawSteps[i].tStart + rawSteps[i].tDuration;
  }

  // make starts/ends to NULL-terminated
  starts[numAllocs] = NULL;
  ends[numAllocs] = NULL;

  // sort starts/ends by their time-stamps
  for (int i = 1; i < numAllocs; ++i)
  {
    for (int j = i; j && starts[j-1]->tStart > starts[j]->tStart; --j)
    {
      Step * tmp = starts[j-1];
      starts[j-1] = starts[j];
      starts[j] = tmp;
    }

    for (int j = i; j && ends[j-1]->tEnd > ends[j]->tEnd; --j)
    {
      Step * tmp = ends[j-1];
      ends[j-1] = ends[j];
      ends[j] = tmp;
    }
  }

  int dummy = 666;
  void * freed = &dummy; // sentinel that freed pointers will set to (so i can detect early if i messed up the test and do double frees by accident)

  Step ** curStart = starts; // next allocation to perform
  Step ** curEnd = ends; // next deallocation to perform
  uint32_t t = 0; // current timestamp (jumps to the time of the next allocation/deallocation until all are done)
  for (;;)
  {
    // perform all allocations that are requested for the current timestamp
    while (*curStart && (*curStart)->tStart == t)
    {
      Step * x = *curStart;
      assert(!x->p);
      x->p = checked_alloc(pool, x->size);
      ++curStart;

      size_t newFreeBytes = yalloc_count_free(pool);
      if (x->p)
      { // alloc succeded
        assert(freeBytes >= x->size);
        freeBytes = newFreeBytes;
      }
      else
      {
        assert(newFreeBytes == freeBytes);

        yalloc_defrag_start(pool);

        for (int i = 0; i < numAllocs; ++i)
        {
          Step * x = &allocs[i];
          if (x->p && x->p != freed)
            x->p = yalloc_defrag_address(pool, x->p);
        }

        yalloc_defrag_commit(pool);
        freeBytes = yalloc_count_free(pool);
      }
    }

    // perform all deallocations that are requested for the current timestamp
    while (*curEnd && (*curEnd)->tEnd == t)
    {
      Step * x = *curEnd;
      assert(x->p != freed);
      checked_free(pool, x->p);

      size_t newFreeBytes = yalloc_count_free(pool);
      if (x->p)
      { // there was something to free
        assert(newFreeBytes >= freeBytes + x->size);
        freeBytes = newFreeBytes;
      }
      else
      {
        assert(newFreeBytes == freeBytes);
      }

      x->p = freed;
      ++curEnd;
    }

    // warp timestamp to the timestamp of the next event
    uint32_t newT;
    if (*curStart && *curEnd)
    {
      uint32_t a = (*curStart)->tStart;
      uint32_t b = (*curEnd)->tEnd;
      newT = a < b ? a : b;
    }
    else if (*curEnd)
      newT = (*curEnd)->tEnd;
    else
    {
      assert(!*curStart);
      break;
    }

    assert(newT > t);
    t = newT;
  }

  yalloc_deinit(pool);
}

#ifdef USE_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
  fuzzerFunc(data, size);
  return 0;
}
#else

#include <stdio.h>

/*
This program supports two modes:

  -n N

  Will read N testcases from /dev/urandom

  <file1> <file2> ...

  Will run every file as testcase
*/
int main(int argc, char * argv[])
{
  for (int iarg = 1; iarg < argc; ++iarg)
  {
    if (!strcmp(argv[iarg], "-n"))
    { // read up to a given number of testcases from /dev/urandom
      if (argc != 3)
        return -1;

      int n = atoi(argv[2]);
      FILE * f = fopen("/dev/urandom", "rb");
      if (!f)
        return -1;

      uint8_t buf[64];
      for (int i = 0; i < n; ++i)
      {
        printf("\riteration %i of %i", i, n);
        fflush(stdout);

        uint16_t size;
        do
        { // yes, theoretically this may loop for ever... but in practice it won't, believe me!
          if (fread(&size, 2, 1, f) != 1)
            return -1;

          size %= sizeof(buf);
        } while (!size);

        if (fread(buf, size, 1, f) != 1)
          return -1;

        fuzzerFunc(buf, size);
      }

      printf("\rDid all %i iterations.\n", n);
      fflush(stdout);

      return 0;
    }

    if (strcmp(argv[iarg], "-files"))
    { // read all the files that where passed by commandline
      ++iarg;
      for (; iarg < argc; ++iarg)
      {
        char const * fn = argv[iarg];
        printf("running for input file: %s\n", fn);
        FILE * f = fopen(fn, "rb");
        if (!f)
          return -1;

        if (fseek(f, SEEK_END, 0))
          return -1;

        long size = ftell(f);
        if (size == EOF)
          return -1;

        if (fseek(f, SEEK_SET, 0))
          return -1;

        uint8_t * buf = (uint8_t*)malloc(size);
        if (!buf)
          return -1;

        if (fread(buf, size, 1, f) != 1)
          return -1;

        fclose(f);

        fuzzerFunc(buf, size);
        free(buf);
      }
    }
  }

  return 0;
}
#endif
