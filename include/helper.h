//
// HELPER.H
//

#ifndef HELPER_H
#define HELPER_H

#include "sfmm.h"
#include <stddef.h>

//
// get_free_list_index_for_size(size_t total_block_size)
//
// Determines which segregated free list index should handle a block
// of size 'total_block_size'.
//
// Typically, smaller blocks map to lower indices and larger blocks
// map to higher indices. This allows for faster lookups/insertions
// for blocks in appropriate size classes.
//

/**
 * Computes the appropriate free list index for a given block size.
 *
 * @param total_block_size The total size of the block (header + payload [+ footer if free]).
 * @return Index of the segregated free list that should store this block.
 */
int get_free_list_index_for_size(size_t total_block_size);

//
// initialize_heap_during_first_call_to_sf_malloc()
//
// Sets up the heap structure on the very first call to sf_malloc().
// This may involve creating prologue/epilogue blocks, initializing
// free lists, and ensuring the allocator is ready to handle requests.
//

/**
 * Performs all initial heap setup during the first call to sf_malloc().
 */
void initialize_heap_during_first_call_to_sf_malloc();

//
// initialize_all_free_list_sentinels()
//
// Creates sentinel nodes for the segregated free lists. Sentinels
// are typically dummy nodes that simplify list operations by
// removing the need to handle special cases for head/tail.
//

/**
 * Initializes all sentinel nodes in the segregated free lists.
 */
void initialize_all_free_list_sentinels();

//
// initialize_all_quick_lists()
//
// Resets or initializes the quick lists so that they can be used
// to store small freed blocks for faster reallocation.
//

/**
 * Initializes all quick lists.
 */
void initialize_all_quick_lists();

//
// calculate_aligned_block_size(size_t requested_payload_size)
//
// Takes the user-requested payload size, adds space for the block
// header and footer, then aligns it to a multiple of 16 bytes.
// Ensures a minimum block size (often 32 bytes in this allocator).
//

/**
 * Aligns and calculates the total size needed for an allocation request.
 *
 * @param requested_payload_size The size requested by the user.
 * @return A properly aligned block size >= 32 bytes.
 */
size_t calculate_aligned_block_size(size_t requested_payload_size);

//
// find_first_free_block_that_fits(size_t required_total_block_size)
//
// Searches the segregated free lists for the first block large
// enough to satisfy 'required_total_block_size'. Returns NULL if
// no suitable block is found. May influence future heap extension.
//

/**
 * Searches the segregated free lists to find the first block that fits the requested size.
 *
 * @param required_total_block_size The total size (header + payload [+ footer]) needed.
 * @return A pointer to a suitable free block, or NULL if none are found.
 */
sf_block *find_first_free_block_that_fits(size_t required_total_block_size);

//
// insert_block_into_free_list(sf_block *free_block)
//
// Places a free block into the appropriate free list, usually based
// on its total size. This is crucial after coalescing or after
// splitting and returning leftover space.
//

/**
 * Inserts a free block into the appropriate free list based on its size.
 *
 * @param free_block Pointer to the free block being inserted.
 */
void insert_block_into_free_list(sf_block *free_block);

//
// remove_block_from_free_list(sf_block *block_to_remove)
//
// Removes the specified block from the free list it currently belongs to.
// Typically used right before allocating that block (or if it needs reclassification).
//

/**
 * Removes a block from the free list in which it resides.
 *
 * @param block_to_remove Pointer to the block to remove from its free list.
 */
void remove_block_from_free_list(sf_block *block_to_remove);

//
// split_free_block_if_necessary(sf_block *free_block, size_t needed_size)
//
// If a free block is significantly larger than what is required, it can be split
// into two blocks: one that exactly (or nearly) matches 'needed_size' and another
// that contains the leftover space. The leftover piece is then inserted back into
// the free lists.
//

/**
 * Splits a free block if it is significantly larger than the needed size.
 *
 * @param free_block Pointer to the block to split.
 * @param needed_size The block size required for allocation.
 */
void split_free_block_if_necessary(sf_block *free_block, size_t needed_size);

//
// coalesce_adjacent_free_blocks(sf_block *target_block)
//
// Attempts to merge 'target_block' with any neighboring free blocks
// in memory, forming a single larger free block. This helps reduce
// fragmentation and increases the chance of satisfying larger requests.
//

/**
 * Coalesces adjacent free blocks to reduce fragmentation.
 *
 * @param target_block The block to coalesce with its neighbors.
 * @return The resulting (possibly larger) free block after coalescing.
 */
sf_block *coalesce_adjacent_free_blocks(sf_block *target_block);

//
// push_block_onto_quick_list(sf_block *small_block)
//
// When a small block (<= certain size threshold) is freed, it can be
// placed on a quick list for faster re-allocation. This function
// pushes the newly freed block onto the front of that list.
//

/**
 * Pushes a recently freed small block onto the corresponding quick list.
 *
 * @param small_block Pointer to the small block being added to the quick list.
 */
void push_block_onto_quick_list(sf_block *small_block);

//
// flush_quick_list_entirely(int quick_list_idx)
//
// Empties out a specific quick list. This is usually done when the
// list reaches a maximum capacity. Freed blocks from the quick
// list are coalesced if possible and placed into the main free lists.
//

/**
 * Flushes all blocks from a specified quick list into the main free lists.
 *
 * @param quick_list_idx The index of the quick list to flush.
 */
void flush_quick_list_entirely(int quick_list_idx);

//
// extend_heap_by_one_page()
//
// Increases the heap space by one page (often 4096 bytes) if no suitable
// free block is found. Typically sets sf_errno = ENOMEM if the system
// cannot provide more memory. Returns a pointer to the newly added
// memory region on success, NULL otherwise.
//

/**
 * Extends the heap by one page (4096 bytes) when no suitable free block is found.
 *
 * @return A pointer to the newly added memory region, or NULL on failure (sf_errno set to ENOMEM).
 */
void *extend_heap_by_one_page();

//
// mark_block_as_allocated(sf_block *allocated_block, size_t final_size, size_t requested_payload_size)
//
// Encodes all necessary information (payload size, block size, flags)
// into the block header/footer, marking it as allocated. This function
// also updates global usage stats.
//

/**
 * Marks a chosen block as allocated to satisfy an sf_malloc or sf_realloc request.
 *
 * @param allocated_block Pointer to the block being allocated.
 * @param final_size The total block size used (aligned size).
 * @param requested_payload_size The payload size the user asked for (before padding).
 */
void mark_block_as_allocated(sf_block *allocated_block, size_t final_size, size_t requested_payload_size);

//
// flush_quick_list(int ql_index) [STATIC]
//
// Internal helper function to flush a specific quick list. Typically invoked
// when that list reaches its capacity. Moves all blocks from the quick list
// to the main free list after coalescing. Marked 'static' to limit its scope
// to within a single compilation unit, ensuring it isn't used externally.
//

/**
 * Flushes a quick list when it reaches capacity.
 * Moves all blocks from the quick list to the main free list after coalescing.
 *
 * @param ql_index The index of the quick list to flush.
 */
static void flush_quick_list(int ql_index);

//
// GLOBAL TRACKING VARIABLES
//
// These two variables track the amount of memory "in use" (current payload)
// and the highest amount of memory "in use" at any point (peak payload).
// This data is used for utilization metrics such as sf_utilization().
//

/**
 * GLOBAL TRACKING VARIABLES (used for sf_utilization).
 *
 * These must be defined in one source file (e.g., sfmm.c), and declared here for use across the project.
 */
extern size_t sf_current_payload; // Tracks current total allocated payload
extern size_t sf_peak_payload;    // Tracks the peak (max) allocated payload

#endif // HELPER_H
