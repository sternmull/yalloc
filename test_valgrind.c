#include "yalloc/yalloc.h"

#include <valgrind/memcheck.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#ifdef NVALGRIND
#error "This test must be compiled with NVALGRIND undefined"
#endif

// Returns 1 if none of the bytes the range can be read or written without triggering a valgrind error, otherwise return 0.
int isProtected(void * p, size_t n)
{
  for (size_t i = 0; i < n; ++i)
  {
    if (!VALGRIND_CHECK_MEM_IS_ADDRESSABLE((char*)p + i, 1)) // NOTE: Yes this is right. The macro returns 0 if true and the first non-addressable address otherwise.
      return 0;
  }
  return 1;
}

// Returns 1 if none of the bytes the range are defined, otherwise return 0.
int isUndefined(void * p, size_t n)
{
  for (size_t i = 0; i < n; ++i)
  {
    if (!VALGRIND_CHECK_MEM_IS_DEFINED((char*)p + i, 1)) // NOTE: Yes this is right. The macro returns 0 if true and the first non-addressable address otherwise.
      return 0;
  }
  return 1;
}

void * checked_alloc(void * pool, size_t n)
{
  void * p = yalloc_alloc(pool, n);
  if (p)
  {
    // every block has a header before and after it, both must be protected from user access
    assert(isProtected((char*)p - 4, 4));

    size_t size = yalloc_block_size(pool, p);
    assert(isProtected((char*)p + size, 4));

    // allocated blocks must start with undefined content
    assert(isUndefined(p, size));

    // make it defined
    memset(p, 0xAB, n);
  }

  return p;
}

void checked_free(void * pool, void * p)
{
  if (!p)
    return;

  // every block has a header before and after it, both must be protected from user access
  assert(isProtected((char*)p - 4, 4));

  size_t size = yalloc_block_size(pool, p);
  assert(isProtected((char*)p + size, 4));

  yalloc_free(pool, p);

  // the freed range must become protected
  assert(isProtected((char*)p - 4, size + 8));
}

/*
The purpose of this test is to check if valgrind protection does properly work: Do we trigger
valgrind errors when the application accidently accesses memory inside a pool that does not belong
to the user-part of allocated block?

This test is expected to produce valgrind errors (because even queries like VALGRIND_CHECK_MEM_IS_DEFINED do increase
the error count). So valgrind output is generally uninteresting.

It is left to the other tests to check if normal usage of the allocator does NOT trigger valgrind errors.

The test uses assert() to check for expected behavior.
*/
void test_valgrind_error_detection()
{
  assert(RUNNING_ON_VALGRIND); // this test must run on valgrind

  /*
  { // tests if isProtected() works as expected
    char buf[8];
    assert(!isProtected(buf, sizeof buf));
    VALGRIND_MAKE_MEM_NOACCESS(&buf[3], 1);
    assert(!isProtected(buf, sizeof buf));
    VALGRIND_MAKE_MEM_NOACCESS(buf, sizeof buf);
    assert(isProtected(buf, sizeof buf));
  }

  { // tests if isUndefiend() works as expected
    char buf[8];
    assert(isUndefined(buf, sizeof buf));
    buf[3] = 42; // NOTE: this might get optimized out, so this test only makes sense for unoptimized builds
    assert(!isUndefined(buf, sizeof buf));

    memset(buf, 0, sizeof buf);
    assert(!isProtected(buf, sizeof buf));
  }
  */

  uint32_t pool[32] = {0};

  yalloc_init(pool, sizeof pool);
  assert(isProtected(pool, sizeof pool)); // initially the whole pool should be protected

  {
    void * a = checked_alloc(pool, 17);
    checked_free(pool, a);

    // check if double-free results in a valgrind error (which causes the free to abort before messing up the pool, so we can continue testing with the pool)
    unsigned e0 = VALGRIND_COUNT_ERRORS;
    yalloc_free(pool, a);
    assert(VALGRIND_COUNT_ERRORS > e0);
  }

  {
    void * a = checked_alloc(pool, 24);
    void * b = checked_alloc(pool, 16);
    checked_free(pool, a);
    a = checked_alloc(pool, 20); // a padded allocation
    checked_free(pool, b);
    checked_free(pool, a);
  }

  {
    void * a = checked_alloc(pool, 24);
    void * b = checked_alloc(pool, 16);
    checked_free(pool, a);

    yalloc_defrag_start(pool);
    b = yalloc_defrag_address(pool, b);
    yalloc_defrag_commit(pool);

    assert(isProtected((char*)b + 16, 4));

    checked_free(pool, b);
  }
}

int main()
{
  test_valgrind_error_detection();
  return 0;
}
