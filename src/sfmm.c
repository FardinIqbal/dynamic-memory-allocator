/**
 * ==============================================================================
 * FILE: sfmm.c
 *
 * This file contains the core logic of a dynamic memory allocator that
 * implements malloc, free, realloc, fragmentation tracking, quick lists, and
 * a segregated free-list structure. Peak utilization is also tracked via
 * sf_current_payload / sf_peak_payload.
 *
 * IMPORTANT:
 * - Do not include a main() function here; the submission would earn a zero.
 * - Keep all functionality intact; only comments and print logs were added for clarity.
 * ==============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "helper.h"

#include <asm-generic/errno-base.h>
#include <stddef.h>  // For size_t

/**
 * ============================================================================
 * Global Variables for Utilization Tracking
 * ----------------------------------------------------------------------------
 *  sf_current_payload : Tracks the total user payload of all currently
 *                       allocated (and not freed) blocks.
 *  sf_peak_payload    : The maximum value of sf_current_payload at any point
 *                       in the program's lifetime (peak usage).
 * ============================================================================
 */
size_t sf_current_payload = 0;
size_t sf_peak_payload = 0;

/**
 * =============================================================================
 * FUNCTION: sf_malloc
 * -----------------------------------------------------------------------------
 * Allocates a block of memory for at least `requested_size` bytes of user data.
 *
 * STEPS:
 *   1) Return NULL immediately if `requested_size == 0`.
 *   2) If the heap is not yet initialized (start == end), initialize it.
 *   3) Compute the required block size, including header & footer, aligned to 16.
 *   4) Search the free lists for a fitting block.
 *   5) If no block is found, extend the heap by one page and try again.
 *   6) If the free block is significantly larger than needed, split it.
 *   7) Remove the chosen block from its free list, mark it allocated.
 *   8) Return a pointer to the user payload area.
 *
 * NOTES:
 *   - On failure to extend the heap, sets sf_errno = ENOMEM.
 *   - The actual allocated block may be bigger than `requested_size` if alignment
 *     or splitting constraints require it.
 *
 * @param requested_size  The desired payload size in bytes.
 * @return A pointer to the payload of the allocated block, or NULL on failure.
 * =============================================================================
 */
void* sf_malloc(size_t requested_size)
{
    // Return NULL if the request is for zero bytes.
    if (requested_size == 0) return NULL;

    // Check if the heap has not yet been initialized.
    if (sf_mem_start() == sf_mem_end()) {
        initialize_heap_during_first_call_to_sf_malloc();
    }

    // Calculate total block size including header, footer, and alignment.
    size_t required_block_size = calculate_aligned_block_size(requested_size);

    // Attempt to find a suitable free block in the free lists.
    sf_block* chosen_block = find_first_free_block_that_fits(required_block_size);

    // If no block is found, extend the heap by one page and try again.
    while (chosen_block == NULL) {
        sf_block* new_block = extend_heap_by_one_page();
        if (new_block == NULL) {
            sf_errno = ENOMEM;
            return NULL;
        }
        chosen_block = find_first_free_block_that_fits(required_block_size);
    }

    // If the chosen free block is substantially bigger than needed, split it.
    split_free_block_if_necessary(chosen_block, required_block_size);

    // Remove from free list and mark the block as allocated.
    remove_block_from_free_list(chosen_block);
    mark_block_as_allocated(chosen_block, required_block_size, requested_size);

    // Return a pointer to the usable payload portion of the allocated block.
    return chosen_block->body.payload;
}

/**
 * =============================================================================
 * FUNCTION: sf_free
 * -----------------------------------------------------------------------------
 * Frees a previously allocated block, placing it into either:
 *   - A quick list (if the block is small enough), or
 *   - The segregated free list (if larger), with potential coalescing.
 *
 * STEPS:
 *   1) Validate the pointer.
 *   2) Subtract the block's payload from sf_current_payload for utilization.
 *   3) If small, push onto the appropriate quick list (mark still allocated).
 *   4) Otherwise, mark truly free, coalesce neighbors, and insert into free list.
 *
 * NOTES:
 *   - If the pointer is invalid, the function calls abort().
 *   - If a quick list is at capacity, flush it before pushing a new block.
 * =============================================================================
 */
void sf_free(void* pp) {
    // If the pointer is NULL, do nothing.
    if (pp == NULL)
        return;

    // Convert user pointer to the start of the block (which includes the header).
    sf_block* block = (sf_block*)((char*)pp - 8);

    // Decode header to retrieve payload/block size and validate.
    uint64_t encoded_header = block->header;
    uint64_t decoded_header = encoded_header ^ MAGIC;
    size_t payload_size = decoded_header >> 32;
    size_t block_size = (decoded_header & 0xFFFFFFFF) & ~0xF;

    // Validate the block to ensure correctness.
    if (block_size < 32 || block_size % 16 != 0)
        abort();
    if (!(decoded_header & THIS_BLOCK_ALLOCATED))
        abort();
    if (decoded_header & IN_QUICK_LIST)
        abort();
    if ((void*)block < sf_mem_start() || (void*)block >= sf_mem_end())
        abort();

    // Reduce current payload usage by the block's payload size.
    sf_current_payload -= payload_size;

    // If block fits in quick list range, place it there.
    size_t max_quick_size = 32 + 16 * (NUM_QUICK_LISTS - 1);
    if (block_size <= max_quick_size) {
        int ql_index = (block_size - 32) / 16;
        if (ql_index < 0 || ql_index >= NUM_QUICK_LISTS)
            abort();

        // If this quick list is at capacity, flush its blocks.
        if (sf_quick_lists[ql_index].length >= QUICK_LIST_MAX)
            flush_quick_list(ql_index);

        // Build a new header marking it as allocated & in quick list.
        uint64_t new_header = ((uint64_t)payload_size << 32) |
                              (block_size | THIS_BLOCK_ALLOCATED | IN_QUICK_LIST);
        block->header = new_header ^ MAGIC;

        // Write footer to match.
        sf_footer* footer = (sf_footer*)((char*)block + block_size - 8);
        *footer = block->header; // already encoded

        // Insert at the head of the quick list.
        block->body.links.next = sf_quick_lists[ql_index].first;
        sf_quick_lists[ql_index].first = block;
        sf_quick_lists[ql_index].length++;

        return;
    }

    // Otherwise, mark the block as truly free, coalesce, and insert into free list.
    uint64_t new_header = ((uint64_t)payload_size << 32) | block_size;
    block->header = new_header ^ MAGIC;

    sf_footer* footer = (sf_footer*)((char*)block + block_size - 8);
    if ((void*)footer >= sf_mem_start() && (void*)footer < sf_mem_end())
        *footer = block->header; // already encoded
    else
        abort();

    sf_block* coalesced = coalesce_adjacent_free_blocks(block);
    if (coalesced == NULL)
        abort();

    insert_block_into_free_list(coalesced);
}

/**
 * =============================================================================
 * FUNCTION: sf_realloc
 * -----------------------------------------------------------------------------
 * Resizes an allocated block's payload to a new size `rsize`.
 *
 * STEPS:
 *   1) If ptr is NULL, this is effectively malloc(rsize).
 *   2) If rsize == 0, free the block and return NULL.
 *   3) Validate the pointer's range and state.
 *   4) If new_size equals old_size, just update the payload in the header.
 *   5) If shrinking, attempt splitting; update utilization accordingly.
 *   6) If growing, allocate new block, copy data, and free old block.
 *
 * NOTES:
 *   - On invalid pointer, sets sf_errno=EINVAL, returns NULL.
 *   - On allocation failure for growing, sets sf_errno=ENOMEM, returns NULL.
 *   - Updates sf_current_payload and sf_peak_payload as needed.
 * =============================================================================
 */
void* sf_realloc(void* pp, size_t rsize)
{
    // If pointer is NULL, act like malloc().
    if (pp == NULL)
        return sf_malloc(rsize);

    // If requested size is 0, free the block and return NULL.
    if (rsize == 0)
    {
        sf_free(pp);
        return NULL;
    }

    // Validate pointer range.
    if ((char*)pp < (char*)sf_mem_start() + 40 || (char*)pp >= (char*)sf_mem_end())
    {
        sf_errno = EINVAL;
        return NULL;
    }

    // Decode current block header to check if it's valid and allocated.
    sf_block* block = (sf_block*)((char*)pp - 8);
    uint64_t encoded_header = block->header;
    uint64_t decoded_header = encoded_header ^ MAGIC;

    if (!(decoded_header & THIS_BLOCK_ALLOCATED) || (decoded_header & IN_QUICK_LIST))
    {
        sf_errno = EINVAL;
        return NULL;
    }

    // Extract old block size/payload.
    size_t old_size = decoded_header & 0xFFFFFFFF & ~0xF;
    size_t old_payload = decoded_header >> 32;

    // Calculate new aligned block size.
    size_t new_size = calculate_aligned_block_size(rsize);

    // If the new size matches the old block size, only update the user payload field.
    if (new_size == old_size)
    {
        uint64_t updated_header = ((uint64_t)rsize << 32) | (old_size | THIS_BLOCK_ALLOCATED);
        block->header = updated_header ^ MAGIC;

        // Update utilization tracking.
        sf_current_payload = sf_current_payload - old_payload + rsize;
        if (sf_current_payload > sf_peak_payload)
            sf_peak_payload = sf_current_payload;

        // Return the same pointer without moving the block.
        return pp;
    }

    // If shrinking, split off the excess if large enough; otherwise it remains a splinter.
    if (new_size < old_size)
    {
        size_t leftover_size = old_size - new_size;

        // Adjust utilization.
        sf_current_payload = sf_current_payload - old_payload + rsize;
        if (sf_current_payload > sf_peak_payload)
            sf_peak_payload = sf_current_payload;

        // Split if leftover can form a valid free block.
        if (leftover_size >= 32)
        {
            // Update header & footer for the newly resized block
            uint64_t resized_header = ((uint64_t)rsize << 32) | (new_size | THIS_BLOCK_ALLOCATED);
            block->header = resized_header ^ MAGIC;

            sf_footer* resized_footer = (sf_footer*)((char*)block + new_size - 8);
            *resized_footer = block->header; // already encoded

            // Create leftover block as free
            sf_block* leftover_block = (sf_block*)((char*)block + new_size);
            uint64_t leftover_header = ((uint64_t)0 << 32) | leftover_size;
            leftover_block->header = leftover_header ^ MAGIC;

            sf_footer* leftover_footer = (sf_footer*)((char*)leftover_block + leftover_size - 8);
            *leftover_footer = leftover_block->header; // encoded

            // Coalesce leftover with neighbors, then insert in free list
            sf_block* coalesced = coalesce_adjacent_free_blocks(leftover_block);
            insert_block_into_free_list(coalesced);
        }
        else
        {
            // Otherwise, treat it as a splinter (no split).
            uint64_t resized_header = ((uint64_t)rsize << 32) | (old_size | THIS_BLOCK_ALLOCATED);
            block->header = resized_header ^ MAGIC;
        }

        return pp;
    }

    // If growing, allocate a new block of the requested size, copy old data, then free old block.
    void* new_pp = sf_malloc(rsize);
    if (new_pp == NULL)
    {
        sf_errno = ENOMEM;
        return NULL;
    }

    memcpy(new_pp, pp, old_payload);
    sf_free(pp);

    return new_pp;
}

/**
 * =============================================================================
 * FUNCTION: sf_fragmentation
 * -----------------------------------------------------------------------------
 * Computes the current internal fragmentation ratio: total_payload / total_allocated_block_size.
 *
 * STEPS:
 *   1) Walk the heap from prologue to epilogue, summing allocated blocks' payload & sizes.
 *   2) Also consider blocks in quick lists as allocated.
 *   3) Return 0.0 if nothing is allocated.
 *
 * NOTES:
 *   - total_allocated_block_size includes header+footer for each allocated block.
 *   - total_payload is the sum of user-requested sizes stored in the upper 32 bits.
 * =============================================================================
 */
double sf_fragmentation()
{
    size_t total_payload = 0;
    size_t total_allocated_block_size = 0;

    void* heap_start = sf_mem_start();
    void* heap_end = sf_mem_end();

    // Start scanning from after the prologue (32 + 8 for prologue + footer).
    sf_block* current = (sf_block*)((char*)heap_start + 40);

    // Walk through the entire heap looking for allocated blocks.
    while ((void*)current + 8 < heap_end)
    {
        uint64_t raw_header = current->header;
        uint64_t header = raw_header ^ MAGIC;

        uint64_t payload = header >> 32;
        uint32_t lower = (uint32_t)(header & 0xFFFFFFFF);
        size_t block_size = lower & ~0xF;
        uint64_t flags = lower & 0xF;

        if (block_size < 32 || block_size % 16 != 0)
            break; // Malformed block or end of valid region

        // Sum payload and total allocated block size if block is allocated.
        if (flags & THIS_BLOCK_ALLOCATED)
        {
            total_payload += payload;
            total_allocated_block_size += block_size;
        }

        // Move to the next block in memory.
        current = (sf_block*)((char*)current + block_size);
    }

    // Also account for blocks in the quick lists as allocated (since they are not free).
    for (int i = 0; i < NUM_QUICK_LISTS; i++)
    {
        sf_block* q = sf_quick_lists[i].first;
        while (q != NULL)
        {
            uint64_t raw_header = q->header;
            uint64_t header = raw_header ^ MAGIC;

            uint64_t payload = header >> 32;
            uint32_t lower = (uint32_t)(header & 0xFFFFFFFF);
            size_t block_size = lower & ~0xF;

            total_payload += payload;
            total_allocated_block_size += block_size;

            q = q->body.links.next;
        }
    }

    // If nothing is allocated, fragmentation is 0.
    if (total_allocated_block_size == 0)
        return 0.0;

    return (double)total_payload / (double)total_allocated_block_size;
}

/**
 * =============================================================================
 * FUNCTION: sf_utilization
 * -----------------------------------------------------------------------------
 * Returns the peak memory utilization, i.e. (peak_payload / total_heap_size).
 *
 * STEPS:
 *   1) If heap not initialized, return 0.0.
 *   2) Compute the total heap size from start to end.
 *   3) Return peak_payload / heap_size. If heap_size=0, return 0.0.
 * =============================================================================
 */
double sf_utilization()
{
    void *heap_start = sf_mem_start();
    void *heap_end = sf_mem_end();

    // If the heap has not been initialized, utilization is 0.
    if (heap_start == heap_end) {
        return 0.0;
    }

    // Calculate total heap size.
    size_t heap_size = (size_t)((char *)heap_end - (char *)heap_start);
    if (heap_size == 0) {
        return 0.0;
    }

    // Return ratio of peak payload to total heap size.
    return (double)sf_peak_payload / (double)heap_size;
}

/* ========================================================================
 * HELPER FUNCTIONS
 * ========================================================================
 * The following are definitions of helper routines declared in "helper.h"
 * including quick-list flush, coalescing, splitting, free list indexing,
 * and heap initialization.
 * ======================================================================*/

/**
 * Computes which segregated free list index to use based on block size.
 *
 * @param total_block_size The total size of the block (including header, footer).
 * @return Index into sf_free_list_heads[].
 */
int get_free_list_index_for_size(size_t total_block_size)
{
    if (total_block_size <= 32) {
        return 0;
    }

    size_t current_class_size = 32;
    int current_index = 0;

    // Double the class size until we find the appropriate slot or reach the last list.
    while (current_class_size < total_block_size && current_index < NUM_FREE_LISTS - 1)
    {
        current_class_size <<= 1;
        current_index++;
    }

    return current_index;
}

/**
 * Initializes the heap on the very first malloc call by creating:
 *   - One page (4KB)
 *   - Prologue block (32 bytes, allocated)
 *   - One free block
 *   - Epilogue block (8 bytes, allocated)
 *   - Resets free/quick lists
 */
void initialize_heap_during_first_call_to_sf_malloc()
{
    void* heap_start = sf_mem_grow();
    if (heap_start == NULL) {
        sf_errno = ENOMEM;
        return;
    }

    // Set all free lists to empty sentinel nodes.
    initialize_all_free_list_sentinels();
    // Reset all quick lists to empty.
    initialize_all_quick_lists();

    // Create a prologue block at the start of the heap.
    sf_block* prologue_block = (sf_block*)((char*)heap_start + 8);
    size_t prologue_header_info = 32 | THIS_BLOCK_ALLOCATED;
    prologue_block->header = prologue_header_info ^ MAGIC;

    // Create the initial free block after the prologue.
    size_t initial_free_block_size = PAGE_SZ - 32 - 8;
    sf_block* initial_free_block = (sf_block*)((char*)prologue_block + 32);

    // Encode & store header, then matching footer
    size_t free_block_header_info = initial_free_block_size;
    initial_free_block->header = free_block_header_info ^ MAGIC;

    sf_footer* initial_free_footer = (sf_footer*)((char*)initial_free_block + initial_free_block_size - 8);
    *initial_free_footer = initial_free_block->header;

    // Create an epilogue block at the end of the page.
    sf_block* epilogue_block = (sf_block*)((char*)sf_mem_end() - 8);
    size_t epilogue_header_info = 8 | THIS_BLOCK_ALLOCATED;
    epilogue_block->header = epilogue_header_info ^ MAGIC;

    // Insert the newly created large free block into the free list.
    insert_block_into_free_list(initial_free_block);
}

/**
 * Sets each list's sentinel node to point to itself (empty).
 */
void initialize_all_free_list_sentinels()
{
    for (int list_index = 0; list_index < NUM_FREE_LISTS; list_index++)
    {
        sf_block* sentinel_node = &sf_free_list_heads[list_index];
        sentinel_node->body.links.next = sentinel_node;
        sentinel_node->body.links.prev = sentinel_node;
        sentinel_node->header = 0; // no encoding needed for sentinel
    }
}

/**
 * Each quick list is empty: length=0, first=NULL.
 */
void initialize_all_quick_lists()
{
    for (int i = 0; i < NUM_QUICK_LISTS; i++)
    {
        sf_quick_lists[i].length = 0;
        sf_quick_lists[i].first = NULL;
    }
}

/**
 * Returns the total block size required for a user payload, ensuring:
 *   - 8-byte header + 8-byte footer
 *   - Aligned to 16 bytes
 *   - At least 32 bytes total
 */
size_t calculate_aligned_block_size(size_t requested_payload_size)
{
    // Compute size including header, footer, and alignment.
    size_t size_with_header_and_footer = requested_payload_size + sizeof(sf_header) + sizeof(sf_footer);
    size_t size_aligned_to_16 = (size_with_header_and_footer + 15) & ~0xF;

    // Ensure minimum block size is 32.
    if (size_aligned_to_16 < 32)
        size_aligned_to_16 = 32;

    return size_aligned_to_16;
}

/**
 * Scans segregated free lists for a first-fit block >= required_total_block_size.
 * If none is found, returns NULL.
 */
sf_block* find_first_free_block_that_fits(size_t required_total_block_size)
{
    // Determine which list index is appropriate to start searching.
    int starting_list_index = get_free_list_index_for_size(required_total_block_size);

    // Search from that list through larger lists using first-fit strategy.
    for (int current_index = starting_list_index; current_index < NUM_FREE_LISTS; current_index++)
    {
        sf_block* sentinel_node = &sf_free_list_heads[current_index];
        sf_block* current_block = sentinel_node->body.links.next;

        while (current_block != sentinel_node)
        {
            // decode
            size_t current_block_size = (current_block->header ^ MAGIC) & ~0xF;
            if (current_block_size >= required_total_block_size)
            {
                return current_block;
            }
            current_block = current_block->body.links.next;
        }
    }
    return NULL;
}

/**
 * Insert a free block into the appropriate free list, setting up header/footer,
 * then performing LIFO insertion at the head.
 */
void insert_block_into_free_list(sf_block* free_block)
{
    // Decode the block's header to discover the size.
    size_t header_unmasked = free_block->header ^ MAGIC;
    size_t block_size = header_unmasked & ~0xF;

    // Re-encode the header as a free block (no flags set).
    size_t new_header = block_size ^ MAGIC;
    free_block->header = new_header;

    // Write matching footer (same encoded value).
    sf_footer* footer = (sf_footer*)((char*)free_block + block_size - 8);
    *footer = free_block->header;

    // Identify which free list this block belongs in.
    int index = get_free_list_index_for_size(block_size);
    sf_block* sentinel = &sf_free_list_heads[index];

    // LIFO insertion at the head of the chosen free list.
    free_block->body.links.next = sentinel->body.links.next;
    free_block->body.links.prev = sentinel;
    sentinel->body.links.next->body.links.prev = free_block;
    sentinel->body.links.next = free_block;
}

/**
 * If a free block is big enough for the requested block + leftover >= 32,
 * we split the block:
 *   - Carve out a new free block from leftover
 *   - Insert leftover block
 *   - Mark the original portion as allocated with the correct payload in the upper bits
 */
void split_free_block_if_necessary(sf_block* free_block, size_t needed_size)
{
    // Decode size of the free block.
    size_t free_block_header_unmasked = free_block->header ^ MAGIC;
    size_t free_block_total_size = free_block_header_unmasked & ~0xF;

    // Calculate leftover if we carve out needed_size from this free block.
    size_t remaining_block_size = free_block_total_size - needed_size;

    // If leftover is too small (< 32 bytes), do not split.
    if (remaining_block_size < 32)
        return;

    // Create a new free block from leftover space.
    sf_block* new_free_block = (sf_block*)((char*)free_block + needed_size);
    uint64_t new_free_header = ((uint64_t)0 << 32) | remaining_block_size;
    new_free_block->header = new_free_header ^ MAGIC;

    // Write footer for the new free block.
    sf_footer* new_free_footer = (sf_footer*)((char*)new_free_block + remaining_block_size - 8);
    *new_free_footer = new_free_block->header; // encoded

    // Insert the new free block into the free list.
    insert_block_into_free_list(new_free_block);

    // Mark the original portion as allocated, storing payload in top 32 bits
    size_t payload_size = needed_size - 16;
    uint64_t alloc_header = ((uint64_t)payload_size << 32) | (needed_size | THIS_BLOCK_ALLOCATED);
    free_block->header = alloc_header ^ MAGIC;

    // Write the footer for the allocated portion
    sf_footer* alloc_footer = (sf_footer*)((char*)free_block + needed_size - 8);
    *alloc_footer = free_block->header; // encoded
}

/**
 * Unlinks a free block from its circular list. If block wasn't in a list or pointers
 * are invalid, we call abort(). This is needed before allocating or coalescing.
 */
void remove_block_from_free_list(sf_block* block_to_remove)
{
    if (block_to_remove == NULL)
        abort();

    sf_block* previous_block = block_to_remove->body.links.prev;
    sf_block* next_block = block_to_remove->body.links.next;

    // If either pointer is invalid, abort.
    if (previous_block == NULL || next_block == NULL)
        abort();

    // Update neighbors to skip this block, effectively removing from list
    if (previous_block != block_to_remove)
    {
        previous_block->body.links.next = next_block;
    }
    if (next_block != block_to_remove)
    {
        next_block->body.links.prev = previous_block;
    }
}

/**
 * Allocates a block (with user payload in top 32 bits) by setting the
 * THIS_BLOCK_ALLOCATED bit and preserving IN_QUICK_LIST if present.
 * Also adjusts current payload usage and updates peak if needed.
 */
void mark_block_as_allocated(sf_block* allocated_block, size_t final_size, size_t requested_payload_size)
{
    // Decode header to preserve IN_QUICK_LIST if set
    uint64_t unmasked_header = allocated_block->header ^ MAGIC;
    uint32_t lower = (uint32_t)(unmasked_header & 0xFFFFFFFF);
    uint32_t flags = lower & 0xF; // preserve flag bits (including IN_QUICK_LIST)

    // Rebuild & encode
    uint64_t new_header = ((uint64_t)requested_payload_size << 32) |
                          (final_size | (flags & IN_QUICK_LIST) | THIS_BLOCK_ALLOCATED);
    allocated_block->header = new_header ^ MAGIC;

    // Write a matching footer (allocated design requires footers)
    sf_footer* footer = (sf_footer*)((char*)allocated_block + final_size - 8);
    *footer = allocated_block->header;

    // Clear IN_QUICK_LIST in next block if it exists
    sf_block* next_block = (sf_block*)((char*)allocated_block + final_size);
    if ((void*)next_block < sf_mem_end()) {
        uint64_t next_unmasked = next_block->header ^ MAGIC;
        next_unmasked &= ~IN_QUICK_LIST;
        next_block->header = next_unmasked ^ MAGIC;
    }

    // Update usage stats
    sf_current_payload += requested_payload_size;
    if (sf_current_payload > sf_peak_payload)
        sf_peak_payload = sf_current_payload;
}

/**
 * Extends the heap by one page (4KB). If possible, coalesce it with the previous
 * free block. Then fix the epilogue and insert the final free block.
 *
 * @return Pointer to the new (or merged) free block, or NULL if extension fails.
 */
void* extend_heap_by_one_page()
{
    void* new_page_start = sf_mem_grow();
    if (new_page_start == NULL)
    {
        sf_errno = ENOMEM;
        return NULL;
    }

    // The new free block starts at the beginning of the newly added page minus 8,
    // effectively overlapping the old epilogue.
    sf_block* new_free_block = (sf_block*)((char*)new_page_start - 8);
    size_t new_block_size = PAGE_SZ;

    // Check if the block just before this new page is free, then merge if possible.
    sf_block* prev_block = (sf_block*)((char*)new_free_block - 8);
    size_t prev_block_size = 0;

    size_t prev_header_unmasked = prev_block->header ^ MAGIC;
    if (!(prev_header_unmasked & THIS_BLOCK_ALLOCATED))
    {
        // If the previous block is free, remove it & combine sizes
        prev_block_size = prev_header_unmasked & ~0xF;
        prev_block = (sf_block*)((char*)new_free_block - prev_block_size);
        new_block_size += prev_block_size;
        remove_block_from_free_list(prev_block);
    }
    else
    {
        // Otherwise, no merge
        prev_block = NULL;
    }

    // final_free_block references the entire free region
    sf_block* final_free_block = (prev_block != NULL) ? prev_block : new_free_block;
    final_free_block->header = (new_block_size) ^ MAGIC;

    // Write footer for that large new free block
    sf_footer* footer = (sf_footer*)((char*)final_free_block + new_block_size - 8);
    *footer = final_free_block->header; // encoded

    // Create a new epilogue at the end of the extended heap.
    sf_block* new_epilogue = (sf_block*)((char*)sf_mem_end() - 8);
    size_t epilogue_val = (8 | THIS_BLOCK_ALLOCATED);
    new_epilogue->header = epilogue_val ^ MAGIC;

    // Insert the merged free block into the free list
    insert_block_into_free_list(final_free_block);
    return final_free_block;
}

/**
 * Coalesces adjacent free blocks around target_free_block if possible.
 * This merges free blocks in memory order to create a larger free block.
 *
 * @param target_free_block The block around which to coalesce.
 * @return The final coalesced block after merging with neighbors.
 */
sf_block* coalesce_adjacent_free_blocks(sf_block* target_free_block)
{
    if (target_free_block == NULL)
        abort();

    // Decode header to get original size
    uint64_t decoded_header = target_free_block->header ^ MAGIC;
    uint32_t lower_bits = (uint32_t)(decoded_header & 0xFFFFFFFF);
    size_t original_size = lower_bits & ~0xF;

    if (original_size < 32 || original_size % 16 != 0)
        abort();

    sf_block* base_block = target_free_block;
    size_t total_size = original_size;

    // Attempt to coalesce with a free predecessor block
    if ((char *)base_block > (char *)sf_mem_start() + 32) {
        sf_footer* prev_footer = (sf_footer *)((char*)base_block - 8);
        if ((void*)prev_footer >= sf_mem_start()) {
            uint64_t prev_footer_val = *prev_footer ^ MAGIC;
            uint32_t prev_lower = (uint32_t)(prev_footer_val & 0xFFFFFFFF);
            size_t prev_size = prev_lower & ~0xF;

            if (prev_size >= 32 && prev_size % 16 == 0 && !(prev_lower & THIS_BLOCK_ALLOCATED)) {
                sf_block* prev_block = (sf_block*)((char*)base_block - prev_size);
                remove_block_from_free_list(prev_block);
                base_block = prev_block;
                total_size += prev_size;
            }
        }
    }

    // Attempt to coalesce with a free successor block
    sf_block* next_block = (sf_block*)((char*)base_block + total_size);
    if ((void*)next_block + 8 <= sf_mem_end()) {
        uint64_t next_decoded_header = next_block->header ^ MAGIC;
        uint32_t next_lower = (uint32_t)(next_decoded_header & 0xFFFFFFFF);
        size_t next_size = next_lower & ~0xF;

        if (next_size >= 32 && next_size % 16 == 0 && !(next_lower & THIS_BLOCK_ALLOCATED)) {
            remove_block_from_free_list(next_block);
            total_size += next_size;
        }
    }

    // Encode new header and write it
    uint64_t new_header = ((uint64_t)0 << 32) | total_size;
    base_block->header = new_header ^ MAGIC;

    // Write matching footer
    sf_footer* footer_loc = (sf_footer*)((char*)base_block + total_size - 8);
    if ((void*)footer_loc < sf_mem_end()) {
        *footer_loc = base_block->header; // encoded
    } else {
        abort();
    }

    return base_block;
}

/**
 * If a quick list is at capacity, flushes all blocks from it into the main free list.
 * Each block is first unmarked from ALLOC & QUICK bits, then coalesced and inserted.
 */
static void flush_quick_list(int ql_index)
{
    while (sf_quick_lists[ql_index].length > 0) {
        sf_block* block = sf_quick_lists[ql_index].first;
        sf_quick_lists[ql_index].first = block->body.links.next;
        sf_quick_lists[ql_index].length--;

        // Decode existing header
        size_t encoded_header = block->header;
        size_t decoded_header = encoded_header ^ MAGIC;

        // Convert it to a free block header (clear ALLOC & QUICK bits)
        size_t block_size = decoded_header & ~0xF;
        size_t free_header = block_size ^ MAGIC;
        block->header = free_header;

        // Write the matching footer
        sf_footer* footer = (sf_footer*)((char*)block + block_size - 8);
        if ((void*)footer >= sf_mem_start() && (void*)footer < sf_mem_end())
            *footer = block->header; // already encoded
        else
            abort();

        // Coalesce adjacent free blocks, insert into free list
        block = coalesce_adjacent_free_blocks(block);
        insert_block_into_free_list(block);
    }
}

/**
 * Retrieves the payload size (user-requested) from the top 32 bits of a block's header.
 */
size_t get_payload_size(const sf_block* block)
{
    uint64_t header = block->header ^ MAGIC;
    return header >> 32;
}
