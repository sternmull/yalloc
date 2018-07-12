# Summary

yalloc is a memory efficient allocator which is intended for embedded
applications that only have a low amount of RAM and want to maximize its
utilization. Properties of the allocator:

 - pools can be up to 128k
 - user data is 32bit aligned
 - 4 bytes overhead per allocation
 - supports defragmentation
 - uses a free list for first fit allocation strategy (most recently freed
   blocks are used first)
 - extensively tested (see section below)
 - MIT license

# Defragmentation

This feature was the initial motivation for this implementation. Especially
when dealing with highly memory constrained environments fragmenting memory
pools can be annoying. For this reason this implementation supports
defragmentation which moves all allocated blocks into a contiguous range at the
beginning of the pool, leaving a maximized free range at the end.

As there is no garbage collector or other runtime system involved that updates
the references, the application must do so. This is done in three steps:

 1. yalloc_defrag_start() is called. This calculates the new
    post-defragmentation-addresses for all allocations, but otherwise leaves
    the allocations untouched.

 2. yalloc_defrag_address() is called by the application for every pointer that
    points to an allocation. It returns the post-defragmentation-address for
    the allocation. The application must update all its relevant pointers this
    way. Care must be taken not not yet dereference that moved pointers. If the
    application works with hierarchical data then this can easily be done by
    updating the pointers button up (first the leafs then their parents).

 3. yalloc_defrag_commit() is called to finally perform the defragmentation.
    All allocated blocks are moved to their post-defragmentation-address and
    the application can continue using the pool the normal way.

It is up to the application when (and if) it performs defragmentation. One
strategy would be to delay it until an allocation failure. Another approach
would be to perform the defragmentation regularly when there is nothing else to
do.

# Configurable Defines

INTERNAL_VALIDATE

If this is not defined on the compiler commandline it will be defined as 0 if
NDEBUG is defined and otherwise as 1. If you want to disable internal
validation when NDEBUG is not defined then define INERNAL_VALIDATE as 0 on the
compiler commandline.

If it is nonzero the heap will be validated via a bunch of assert() calls at
the end of every function that modifies the heap. This has roughly O(N*N)
overhead where N is the number of allocated blocks in a heap. For applications
with enough live allocations this will get significant.

YALLOC_VALGRIND

If this is defined in yalloc.c and NVALGRIND is not defined then
valgrind/memcheck.h is included and the the allocator functions tell valgrind
about the pool, the allocations and makes the block headers inaccessible outside
of yalloc-functions. This allows valgrind to detect a lot of the accidents that
can happen when dealing dynamic memory. This also adds some overhead for every
yalloc-call because most of them will "unprotect" the internal structure on
entry and "protect" it again (marking it as inaccessible for valgrind) before
returning.

# Tests

The tests rely on internal validation of the pool (see INTERNAL_VALIDATE) to
check that no of the assumptions about the internal structure of the pool are
violated. They additionally check for correctness of observations that can be
made by using the public functions of the allocator (like checking if user data
stays unmodified). There are a few different scripts that run tests:

 - run_coverage.sh runs a bunch of testfunctions that are carefully crafted to
   cover all code paths. Coverage data is generated by clang and a summary is
   shown at the end of the test.

 - run_valgrind.sh tests if the valgrind integration is working as expected,
   runs the functions from the coverage test and some randomly generated
   testcases under valgrind.

 - run_libfuzzer.sh uses libfuzzer from clang to generate interesting testcases
   and runs them in multiple jobs in parallel for 10 seconds. It also generates
   coverage data at the end (it always got 100% coverage in my testruns).

All tests exit with 0 and print "All fine!" at the end if there where no
errors. Coverage deficits are not counted as error, so you have to look at the
summary (they should show 100% coverage!).


# Implementation Details

The Headers and the user data are 32bit aligned. Headers have two 16bit fields
where the high 15 bits represent offsets (relative to the pools address) to the
previous/next block. The macros HDR_PTR() and HDR_OFFSET() are used to
translate an offset to an address and back. The 32bit alignment is exploited to
allow pools of up to 128k with that 15 significant bits.

A pool is always occupied by non-overlapping blocks that link to their
previous/next block in address order via the prev/next field of Header.

Free blocks are always joined: No two free blocks will ever be neighbors.

Free blocks have an additional header of the same structure. This additional
header is used to build a list of free blocks (independent of their address
order).

yalloc_free() will insert the freed block to the front of the free list.
yalloc_alloc() searches that list front to back and takes the first block that
is big enough to satisfy the allocation.

There is always a Header at the front and at the end of the pool. The Header at
the end is degenerate: It is marked as "used" but has no next block (which is
usually used to determine the size of a block).

The prev-field of the very first block in the pool has special meaning: It
points to the first free block in the pool. Or, if the pool is currently
defragmenting (after yalloc_defrag_start() and before yalloc_defrag_commit()),
points to the last header of the pool. This state can be recognized by checking
if it points to an empty block (normal pool state) or a used block
(defragmentation in progress). This logic can be seen in
yalloc_defrag_in_progress().

The lowest bit of next/prev have special meaning:

 - low bit of prev is set for free blocks

 - low bit of next is set for blocks with 32bit padding after the user data.
   This is needed when a block is allocated from a free block that leaves only
   4 free bytes after the user data... which is not enough to insert a
   free-header (which is needs 8 bytes). The padding will be reclaimed when
   that block is freed or when the pool is defragmented. The predicate
   isPadded() can be used to test if a block is padded. Free blocks are never
   padded.

The predicate isNil() can be used to test if an offset points nowhere (it tests
if all 15 high bits of an offset are 1). The constant NIL has all but the
lowest bit set. It is used to set offsets to point to nowhere, and in some
places it is used to mask out the actual address bits of an offset. This should
be kept in mind when modifying the code and updating prev/next: Think carefully
if you have to preserve the low bit when updating an offset!

Defragmentation is done in two phases: First the user calls
yalloc_defrag_start(). This will put the pool in a special state where no
alloc/free-calls are allowed. In this state the prev-fields of the used blocks
have a special meaning: They store the offset that the block will have after
defragmentation finished. This information is used by yalloc_defrag_address()
which can be called by the application to query the new addresses for its
allocations. After the application has updated all its pointers it must call
yalloc_defrag_commit() which moves all used blocks in contiguous space at the
beginning of the pool, leaving one maximized free block at the end.
