#include "yalloc/yalloc.h"

#include <stdint.h>
#include <memory.h>
#include <assert.h>

#include "test_util.h"


// carefully crafted test sequence that covers all paths of the allocation function
static void test_alloc_coverage()
{
  uint32_t pool[MAX_POOL_SIZE / 4];

  assert(!yalloc_init(pool, 15)); // path that rounds size down to alignment
  yalloc_deinit(pool);
  assert(yalloc_init(pool, 8)); // pool too small
  assert(yalloc_init(pool, MAX_POOL_SIZE + 1)); // pool too big
  assert(!yalloc_init(pool, MAX_POOL_SIZE)); // maximum pool size

  assert(!checked_alloc(pool, 0)); // allocating zero bytes should return NULL
  checked_free(pool, NULL); // freeing NULL should be ignored

  {
    void * all = checked_alloc(pool, yalloc_count_free(pool)); // allocate everything
    assert(all);
    void * nope = checked_alloc(pool, 1); // try to allocate with an empty free-list (this is the interesting case)
    assert(!nope);
    checked_free(pool, all); // free everything
  }

  { // try to allocate more than available while the free-list is non-empty
    void * p = checked_alloc(pool, sizeof(pool));
    assert(!p);
  }

  void * a1 = checked_alloc(pool, 8);
  assert(a1);
  void * b = checked_alloc(pool, 16);
  assert(b);

  checked_free(pool, a1);

  { // occupy first block with exactly the same size as before
    void * a2 = checked_alloc(pool, 8);
    assert(a2 == a1);
    checked_free(pool, a2);
  }

  { // occupy first block with 1 byte less (test ceiling to alignment)
    void * a2 = checked_alloc(pool, 7);
    assert(a2 == a1);
    checked_free(pool, a2);
  }

  { // occupy first block with 4 bytes less, this leads to a padded allocation because 4 bytes are not enough for a free block (which is 8 bytes because it consistes of two list-nodes: one for address-order, one for free-list)
    void * a2 = checked_alloc(pool, 4);
    assert(a2 == a1);
    checked_free(pool, a2);
  }

  // allocation that can not be satisfied by the first element of the free list (so it will be iterated)
  void * c = checked_alloc(pool, 32);
  assert(c);

  checked_free(pool, c);

  // allocation that splits the first free block while that block has a pointer to a next free block
  void * c2 = checked_alloc(pool, 24);
  assert(c2 == c);

  yalloc_deinit(pool);
}

void test_free_coverage()
{
  uint32_t pool[32];
  yalloc_init(pool, sizeof(pool));

  { // free a block with a free block before it
    void * a = checked_alloc(pool, 16);
    void * b = checked_alloc(pool, yalloc_count_free(pool));
    checked_free(pool, a);

    // now b has free space before it (but not after it)
    checked_free(pool, b);
  }

  for (int withGap = 0; withGap < 2; ++withGap)
  {
    if (withGap)
    { // create a tiny free block at the beginning of the pool (so the free list has an additional element, which triggers additional code paths)
      void * gap = checked_alloc(pool, 8);
      checked_alloc(pool, 8);
      checked_free(pool, gap);
    }

    { // free block with free space after it
      void * a = checked_alloc(pool, 16);
      checked_free(pool, a);
    }

    { // free after a padded allocation
      void * a = checked_alloc(pool, 16);
      void * b = checked_alloc(pool, 16);
      checked_free(pool, a);
      void * a2 = checked_alloc(pool, 12); // reoccupy the space of a with a padded block
      assert(a2 == a);
      checked_free(pool, b); // free the block after the padded block
    }

    { // free a block with a free block before and after it
      void * a = checked_alloc(pool, 16);
      void * b = checked_alloc(pool, 16);
      checked_free(pool, a);

      // now b has free blocks on both sides
      checked_free(pool, b);
    }
  }

  yalloc_deinit(pool);
}

void test_count_free()
{
  uint32_t pool[10];
  yalloc_init(pool, sizeof(pool));

  int n = yalloc_count_free(pool);
  assert(n == 32);

  {
    void * p = checked_alloc(pool, n);
    n = yalloc_count_free(pool);
    assert(n == 0);
    checked_free(pool, p);
  }

  void * a = checked_alloc(pool, 8);
  n = yalloc_count_free(pool);
  assert(n == 20);

  /*void * b =*/ checked_alloc(pool, 8);
  n = yalloc_count_free(pool);
  assert(n == 8);

  checked_free(pool, a);
  n = yalloc_count_free(pool);
  assert(n == 20);

  void * a2 = checked_alloc(pool, 4); // occupies the space of a, but will have padding
  assert(a2 == a);
  n = yalloc_count_free(pool);
  assert(n == 12);

  checked_alloc(pool, 8);
  n = yalloc_count_free(pool);
  assert(n == 0);

  yalloc_deinit(pool);
}

void test_used_block_iteration()
{
  uint32_t pool[10];
  yalloc_init(pool, sizeof(pool));

  assert(!yalloc_first_used(pool));

  void * a = checked_alloc(pool, 8);
  assert(yalloc_first_used(pool) == a);
  assert(!yalloc_next_used(pool, a));

  void * b = checked_alloc(pool, 8);
  assert(yalloc_first_used(pool) == a);
  assert(yalloc_next_used(pool, a) == b);
  assert(!yalloc_next_used(pool, b));

  checked_free(pool, a);
  assert(yalloc_first_used(pool) == b);
  assert(!yalloc_next_used(pool, b));

  yalloc_deinit(pool);
}

void test_defragmentation_coverage()
{
  uint32_t pool[MAX_POOL_SIZE / 4];
  yalloc_init(pool, sizeof(pool));

  { // defrag an empty pool
    assert(!yalloc_defrag_in_progress(pool));
    yalloc_defrag_start(pool);
    assert(yalloc_defrag_in_progress(pool));
    yalloc_defrag_commit(pool);
    assert(!yalloc_defrag_in_progress(pool));
  }

  { // defrag pool with one allocation that is already defragmented
    void * a = checked_alloc(pool, 16);
    yalloc_defrag_start(pool);
    assert(yalloc_defrag_address(pool, a) == a);
    yalloc_defrag_commit(pool);
    checked_free(pool, a);
  }

  { // defrag full pool with one allocation
    void * a = checked_alloc(pool, yalloc_count_free(pool));
    yalloc_defrag_start(pool);
    assert(yalloc_defrag_address(pool, a) == a);
    yalloc_defrag_commit(pool);
    checked_free(pool, a);
  }

  { // defrag full pool with one padded allocation
    void * a = checked_alloc(pool, yalloc_count_free(pool) - 4);
    yalloc_defrag_start(pool);
    assert(yalloc_defrag_address(pool, a) == a);
    yalloc_defrag_commit(pool);
    checked_free(pool, a);
  }

  { // defrag pool with two allocations, first one is padded
    void * a = checked_alloc(pool, 16);
    void * b = checked_alloc(pool, 24);
    checked_free(pool, a);
    void * newA = checked_alloc(pool, 12); // creates a padded allocation where a was
    assert(a == newA);
    yalloc_defrag_start(pool);
    assert(yalloc_defrag_address(pool, a) == a);
    void * newB = yalloc_defrag_address(pool, b);
    assert(newB == (char*)b - 4);
    yalloc_defrag_commit(pool);
    checked_free(pool, a);
    checked_free(pool, newB);
  }

  { // defrag pool with one allocation and a gap before it
    void * a = checked_alloc(pool, 16);
    void * b = checked_alloc(pool, 16);
    checked_free(pool, a);
    yalloc_defrag_start(pool);
    assert(yalloc_defrag_address(pool, NULL) == NULL);
    void * newB = yalloc_defrag_address(pool, b);
    assert(newB == a);
    yalloc_defrag_commit(pool);

    void * all = checked_alloc(pool, yalloc_count_free(pool));
    assert(all);
    checked_free(pool, all);

    checked_free(pool, newB);
  }

  { // defrag pool with two allocations and a gap between them
    void * a = checked_alloc(pool, 16);
    void * b = checked_alloc(pool, 24);
    void * c = checked_alloc(pool, 8);
    checked_free(pool, b);
    yalloc_defrag_start(pool);
    assert(yalloc_defrag_address(pool, NULL) == NULL);
    void * newA = yalloc_defrag_address(pool, a);
    assert(newA == a);
    void * newC = yalloc_defrag_address(pool, c);
    assert(newC == (char*)pool + 4 + 16 + 4);
    yalloc_defrag_commit(pool);

    void * all = checked_alloc(pool, yalloc_count_free(pool));
    assert(all);
    checked_free(pool, all);

    checked_free(pool, newA);
    checked_free(pool, newC);
  }

  { // defrag pool with two allocation and a gaps
    size_t initialFree = yalloc_count_free(pool);

    void * a = checked_alloc(pool, 20);
    void * b = checked_alloc(pool, 16);
    void * c = checked_alloc(pool, 8);
    void * d = checked_alloc(pool, 24);
    checked_free(pool, a);
    checked_free(pool, c);

    assert(yalloc_count_free(pool) == initialFree - (16 + 4) - (24 + 4));

    yalloc_defrag_start(pool);
    assert(yalloc_defrag_address(pool, NULL) == NULL);
    void * newB = yalloc_defrag_address(pool, b);
    assert(newB == a);
    void * newD = yalloc_defrag_address(pool, d);
    assert(newD == (char*)pool + 16 + 4 + 4);
    yalloc_defrag_commit(pool);

    void * all = checked_alloc(pool, yalloc_count_free(pool));
    assert(all);
    checked_free(pool, all);

    checked_free(pool, newB);
    checked_free(pool, newD);
  }

  yalloc_deinit(pool);
}

// This test misses a bunch of special cases. I still leave it heare... a test more never hurts.
// The better test is test_defragmentation_coverage() which covers all cases of the defragmentation procedure.
void test_defragmentation()
{
  uint32_t pool[MAX_POOL_SIZE / 4];
  yalloc_init(pool, sizeof(pool));


  // defragment a pool that don't need defragementation
  for (int n = 0; n < 5; ++n)
  { // do so with n allocations
    void * ptrs[n];

    for (int i = 0; i < n; ++i)
      ptrs[i] = checked_alloc(pool, i * 8);

    yalloc_defrag_start(pool);

    for (int i = 0; i < n; ++i)
      assert(yalloc_defrag_address(pool, ptrs[i]) == ptrs[i]);

    yalloc_defrag_commit(pool);

    for (int i = 0; i < n; ++i)
      checked_free(pool, ptrs[i]);
  }

  // defragment a pool that does need defragementation
  for (int freeOdd = 0; freeOdd < 2; ++freeOdd)
  { // at first round free the first, third, ... allocation, at the next free the second, fourth, ...
    for (int n = 0; n < 5; ++n)
    { // do a pass with n allocations
      void * ptrs[n];
      int sizes[n];

      // do the allocations
      for (int i = 0; i < n; ++i)
      {
        sizes[i] = i * 8;
        ptrs[i] = checked_alloc(pool, sizes[i]);
      }

      // free every second allcation (so we get holes in the pool)
      for (int i = freeOdd; i < n; i += 2)
      {
        checked_free(pool, ptrs[i]);
        ptrs[i] = NULL;
      }

      yalloc_defrag_start(pool);

      // check if the new addresses are reported correctly (should be pool + sum of allocations before them + their own header)
      for (int i = 0; i < n; ++i)
      { // for each allocation
        char * expectedNewAddr;

        if (!ptrs[i])
          expectedNewAddr = NULL;
        else
        {
          expectedNewAddr = (char*)pool;

          for (int j = 0; j < i; ++j)
          {
            if (ptrs[j])
              expectedNewAddr += yalloc_block_size(pool, ptrs[j]) + 4;
          }

          expectedNewAddr += 4; // header of the current allocation
        }
        void * newAddr = yalloc_defrag_address(pool, ptrs[i]);
        assert(newAddr == expectedNewAddr);
        ptrs[i] = newAddr;
      }

      yalloc_defrag_commit(pool);

      for (int i = 0; i < n; ++i)
      {
        checked_free(pool, ptrs[i]);
      }
    }
  }

  yalloc_deinit(pool);
}

int main()
{
  test_used_block_iteration();
  test_count_free();
  test_alloc_coverage();
  test_free_coverage();
  test_defragmentation_coverage();
  test_defragmentation();

  return 0;
}
