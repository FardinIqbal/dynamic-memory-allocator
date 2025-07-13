#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"
#include "helper.h"
#define TEST_TIMEOUT 15

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_block *bp = sf_free_list_heads[i].body.links.next;
        while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == ((bp->header ^ sf_magic()) & ~0xffffffff0000000f))
	        cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

/*
 * Assert the total number of quick list blocks of a specified size.
 * If size == 0, then assert the total number of all quick list blocks.
 */
void assert_quick_list_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_QUICK_LISTS; i++) {
	sf_block *bp = sf_quick_lists[i].first;
	while(bp != NULL) {
	    if(size == 0 || size == ((bp->header ^ sf_magic()) & ~0xffffffff0000000f))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of quick list blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of quick list blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

Test(sfmm_basecode_suite, malloc_an_int, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz = sizeof(int);
	int *x = sf_malloc(sz);

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(4016, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_basecode_suite, malloc_four_pages, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;

	// We want to allocate up to exactly four pages, so there has to be space
	// for the header and the link pointers.
	void *x = sf_malloc(16316);
	cr_assert_not_null(x, "x is NULL!");
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sfmm_basecode_suite, malloc_too_large, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	void *x = sf_malloc(151505);

	cr_assert_null(x, "x is not NULL!");
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(151504, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sfmm_basecode_suite, free_quick, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 32, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_quick_list_block_count(0, 1);
	assert_quick_list_block_count(48, 1);
	assert_free_block_count(0, 1);
	assert_free_block_count(3936, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_no_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 200, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 2);
	assert_free_block_count(224, 1);
	assert_free_block_count(3760, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_w = 8, sz_x = 200, sz_y = 300, sz_z = 4;
	/* void *w = */ sf_malloc(sz_w);
	void *x = sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);
	sf_free(x);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 2);
	assert_free_block_count(544, 1);
	assert_free_block_count(3440, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, freelist, .timeout = TEST_TIMEOUT) {
        size_t sz_u = 200, sz_v = 300, sz_w = 200, sz_x = 500, sz_y = 200, sz_z = 700;
	void *u = sf_malloc(sz_u);
	/* void *v = */ sf_malloc(sz_v);
	void *w = sf_malloc(sz_w);
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 4);
	assert_free_block_count(224, 3);
	assert_free_block_count(1808, 1);

	// First block in list should be the most recently freed block.
	int i = 3;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(bp, (char *)y - 8,
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, bp, (char *)y - 8);
}

Test(sfmm_basecode_suite, realloc_larger_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int), sz_y = 10, sz_x1 = sizeof(int) * 20;
	void *x = sf_malloc(sz_x);
	/* void *y = */ sf_malloc(sz_y);
	x = sf_realloc(x, sz_x1);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - 8);
	cr_assert((bp->header ^ sf_magic()) & 0x1, "Allocated bit is not set!");
	cr_assert(((bp->header ^ sf_magic()) & ~0xffffffff0000000f) == 96,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  (bp->header ^ sf_magic()) & ~0xffffffff0000000f, 96);

	assert_quick_list_block_count(0, 1);
	assert_quick_list_block_count(32, 1);
	assert_free_block_count(0, 1);
	assert_free_block_count(3888, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_splinter, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int) * 20, sz_y = sizeof(int) * 16;
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char *)x - 8);
	cr_assert((bp->header ^ sf_magic()) & 0x1, "Allocated bit is not set!");
	cr_assert(((bp->header ^ sf_magic()) & ~0xffffffff0000000f) == 96,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  (bp->header ^ sf_magic()) & ~0xffffffff0000000f, 96);

	// There should be only one free block.
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(3952, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_free_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(double) * 8, sz_y = sizeof(int);
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char *)x - 8);
	cr_assert((bp->header ^ sf_magic()) & 0x1, "Allocated bit is not set!");
	cr_assert(((bp->header ^ sf_magic()) & ~0xffffffff0000000f) == 32,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  (bp->header ^ sf_magic()) & ~0xffffffff0000000f, 32);

	// After realloc'ing x, we can return a block of size ADJUSTED_BLOCK_SIZE(sz_x) - ADJUSTED_BLOCK_SIZE(sz_y)
	// to the freelist.  This block will go into the main freelist and be coalesced.
	// Note that we don't put split blocks into the quick lists because their sizes are not sizes
	// that were requested by the client, so they are not very likely to satisfy a new request.
	assert_quick_list_block_count(0, 0);	
	assert_free_block_count(0, 1);
	assert_free_block_count(4016, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE OR MANGLE THESE COMMENTS
//############################################

/**
 * Test: fragmentation_no_allocations
 *
 * Verifies that `sf_fragmentation()` returns 0.0 when no blocks are allocated.
 *
 * This test assumes the heap has not been touched or all previous allocations
 * have been freed. Fragmentation is defined as:
 *
 *     total_payload / total_allocated_block_size
 *
 * When there are no allocated blocks, both numerator and denominator are 0,
 * and the function should return 0.0.
 */
Test(sfmm_student_suite, fragmentation_no_allocations, .timeout = TEST_TIMEOUT) {
	cr_assert_float_eq(sf_fragmentation(), 0.0, 1e-6,
		"Expected 0.0 fragmentation when no blocks are allocated.");
}


/**
 * Test: fragmentation_single_allocation
 *
 * Verifies `sf_fragmentation()` on a single small allocation.
 *
 * Allocation plan:
 *   x: malloc(20) → aligned to 48 bytes (due to header + footer + padding)
 *
 * Expected fragmentation:
 *   - Payload: 20 bytes (user-requested)
 *   - Block size: 48 bytes (actual total block size including header and footer)
 *   - Fragmentation = 20 / 48 = 0.416667
 */
Test(sfmm_student_suite, fragmentation_single_allocation, .timeout = TEST_TIMEOUT) {
	size_t requested = 20;
	void *x = sf_malloc(requested);
	cr_assert_not_null(x, "Allocation failed.");

	double payload = (double)requested;
	double block_size = 48.0; // Actual block size (header + footer + padding)
	double expected = payload / block_size;

	cr_assert_float_eq(sf_fragmentation(), expected, 1e-6,
		"Expected fragmentation %f but got %f", expected, sf_fragmentation());

	sf_free(x); // Cleanup
}


/**
 * Test: fragmentation_multiple_allocations
 *
 * Verifies `sf_fragmentation()` with multiple active allocations of varying sizes.
 *
 * Allocation plan:
 *   a: malloc(24)   → actual block size = 48 bytes
 *   b: malloc(100)  → actual block size = 128 bytes
 *   c: malloc(40)   → actual block size = 64 bytes
 *
 * Expected fragmentation:
 *   - Total payload: 24 + 100 + 40 = 164
 *   - Total block size: 48 + 128 + 64 = 240
 *   - Fragmentation = 164 / 240 ≈ 0.683333
 */
Test(sfmm_student_suite, fragmentation_multiple_allocations, .timeout = TEST_TIMEOUT) {
	void *a = sf_malloc(24);   // 48-byte block
	void *b = sf_malloc(100);  // 128-byte block
	void *c = sf_malloc(40);   // 64-byte block

	cr_assert(a && b && c, "Allocation failed.");

	double payload = 24 + 100 + 40;
	double block_size = 48 + 128 + 64;
	double expected = payload / block_size;

	cr_assert_float_eq(sf_fragmentation(), expected, 1e-6,
					   "Expected fragmentation %f but got %f", expected, sf_fragmentation());

	// Cleanup
	sf_free(a);
	sf_free(b);
	sf_free(c);
}


/**
 * Test: fragmentation_ignores_freed_large_middle_block
 *
 * This test verifies that `sf_fragmentation()` correctly ignores a large freed block
 * in the middle of the heap that is too big to be placed into any quick list.
 *
 * Allocation plan:
 *   a: malloc(24)    → aligned to 48 bytes
 *   b: malloc(2000)  → aligned to 2016 bytes (freed — goes to main free list)
 *   c: malloc(64)    → aligned to 80 bytes
 *
 * After freeing b, fragmentation should consider only a and c:
 *   Fragmentation = (24 + 64) / (48 + 80) = 88 / 128 ≈ 0.6875
 */
Test(sfmm_student_suite, fragmentation_ignores_freed_large_middle_block, .timeout = TEST_TIMEOUT) {
	void *a = sf_malloc(24);    // Allocated → block size = 48
	void *b = sf_malloc(2000);  // Allocated → block size = 2016
	void *c = sf_malloc(64);    // Allocated → block size = 80

	cr_assert_not_null(a, "Allocation for 'a' failed.");
	cr_assert_not_null(b, "Allocation for 'b' failed.");
	cr_assert_not_null(c, "Allocation for 'c' failed.");

	sf_free(b);  // Goes to main free list, still marked unallocated

	double payload = 24 + 64;
	double block_size = 48 + 80;
	double expected = payload / block_size;

	cr_assert_float_eq(sf_fragmentation(), expected, 1e-6,
		"Expected fragmentation %f but got %f", expected, sf_fragmentation());

	sf_free(a);
	sf_free(c);
}


/**
 * Test: fragmentation_all_freed
 *
 * Verifies that internal fragmentation is 0.0 after all allocated blocks
 * have been freed and are no longer counted as "allocated" by the allocator.
 *
 * Key Considerations:
 * - Internal fragmentation is defined as total_payload / total_allocated_block_size.
 * - Blocks in the quick list are still marked allocated and must NOT be used in this test.
 * - To avoid the quick list, we allocate a large block (e.g., 2000 bytes).
 *   This aligns to a block size well above the maximum quick list threshold (176 bytes).
 * - After freeing this large block, the allocator places it in the main free list
 *   and marks it unallocated, so `sf_fragmentation()` should return 0.0.
 */
Test(sfmm_student_suite, fragmentation_all_freed, .timeout = TEST_TIMEOUT) {
	void *x = sf_malloc(2000); // Allocated block will be too large for the quick list
	cr_assert_not_null(x, "Allocation failed.");

	sf_free(x); // Block goes into main free list (marked free)

	// Expect 0.0 fragmentation since no blocks are currently marked allocated
	cr_assert_float_eq(sf_fragmentation(), 0.0, 1e-6,
					   "Expected 0.0 fragmentation after all blocks are freed.");
}

/**
 * Test: utilization_no_allocations
 *
 * Verifies that `sf_utilization()` returns 0.0 when the heap is empty.
 */
Test(sfmm_student_suite, utilization_no_allocations, .timeout = TEST_TIMEOUT) {
	cr_assert_float_eq(sf_utilization(), 0.0, 1e-6,
		"Expected utilization 0.0 before any allocations.");
}

/**
 * Test: utilization_single_allocation
 *
 * Allocates a single small block and checks peak utilization.
 *
 * Expected:
 *   - Requested payload: 20
 *   - Heap size: 4096
 *   - Utilization = 20 / 4096 = 0.004883
 */
Test(sfmm_student_suite, utilization_single_allocation, .timeout = TEST_TIMEOUT) {
	void *x = sf_malloc(20);
	cr_assert_not_null(x, "Allocation failed.");

	double expected = 20.0 / 4096.0;
	cr_assert_float_eq(sf_utilization(), expected, 1e-6,
		"Expected utilization %.6f but got %.6f", expected, sf_utilization());

	sf_free(x);
}

/**
 * Test: utilization_multiple_allocations
 *
 * Allocates multiple blocks to track peak payload.
 *
 * Allocations:
 *   - a: 100 bytes
 *   - b: 200 bytes
 *   - c: 300 bytes
 * Total payload: 600
 * Heap size: 4096
 * Utilization = 600 / 4096 ≈ 0.146484
 */
Test(sfmm_student_suite, utilization_multiple_allocations, .timeout = TEST_TIMEOUT) {
	void *a = sf_malloc(100);
	void *b = sf_malloc(200);
	void *c = sf_malloc(300);

	cr_assert(a && b && c, "Allocations failed.");

	double expected = 600.0 / 4096.0;
	cr_assert_float_eq(sf_utilization(), expected, 1e-6,
		"Expected utilization %.6f but got %.6f", expected, sf_utilization());

	sf_free(a);
	sf_free(b);
	sf_free(c);
}

/**
 * Test: utilization_peak_does_not_shrink
 *
 * Allocates and frees memory to ensure `sf_utilization()` reflects **peak**, not current usage.
 *
 * Sequence:
 *   - a: 2000 bytes → peak becomes 2000
 *   - free a
 *   - utilization must still be 2000 / 4096
 */
Test(sfmm_student_suite, utilization_peak_does_not_shrink, .timeout = TEST_TIMEOUT) {
	void *a = sf_malloc(2000);
	cr_assert_not_null(a, "Allocation failed.");
	sf_free(a);

	double expected = 2000.0 / 4096.0;
	cr_assert_float_eq(sf_utilization(), expected, 1e-6,
		"Expected peak utilization %.6f after free, but got %.6f", expected, sf_utilization());
}

/**
 * Test: utilization_grows_with_heap
 *
 * Forces heap to grow by allocating more than one page.
 *
 * Allocation:
 *   - malloc(6000) → spans 2 pages (8192 bytes)
 *   - peak_payload = 6000
 *   - utilization = 6000 / 8192 = 0.732422
 */
Test(sfmm_student_suite, utilization_grows_with_heap, .timeout = TEST_TIMEOUT) {
	void *x = sf_malloc(6000);
	cr_assert_not_null(x, "Allocation failed.");

	double expected = 6000.0 / 8192.0;
	cr_assert_float_eq(sf_utilization(), expected, 1e-6,
		"Expected utilization %.6f but got %.6f", expected, sf_utilization());

	sf_free(x);
}
