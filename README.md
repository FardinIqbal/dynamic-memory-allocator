# Dynamic Memory Allocator

A secure, high-performance memory allocator for 64-bit systems written in C.
Implements `malloc`, `free`, and `realloc` from scratch with support for:

* Segregated free lists (size classes)
* Quick lists for caching small freed blocks
* Immediate and deferred coalescing
* 16-byte alignment
* Header/footer obfuscation for heap integrity checks
* Internal fragmentation and heap utilization metrics

---

## Overview

This project replicates the core functionality of a production-grade dynamic memory allocator, similar in spirit to `dlmalloc` or `jemalloc`, but implemented from first principles in C. It manages a virtual heap using fixed-size page requests and supports allocation requests of varying sizes with minimal fragmentation and strong memory correctness guarantees.

The allocator maintains full metadata, including payload size tracking and corruption detection using obfuscated headers and footers.

---

## Features

| Feature                    | Description                                                                 |
| -------------------------- | --------------------------------------------------------------------------- |
| Segregated Free Lists      | 12 size classes to efficiently locate blocks for allocation (power-of-two)  |
| Quick Lists                | Fast LIFO cache for recently freed small blocks                             |
| Coalescing                 | Merges adjacent free blocks to reduce fragmentation                         |
| Block Splitting            | Splits blocks when allocation size is smaller than free block               |
| 16-byte Alignment          | Guarantees payload addresses are aligned to 16 bytes                        |
| Obfuscated Headers/Footers | Detects heap corruption and invalid frees using XOR with a `MAGIC` constant |
| Epilogue & Prologue Guards | Simplifies edge-case handling at heap boundaries                            |
| Instrumentation            | Tracks internal fragmentation and peak heap utilization                     |

---

## Technical Design

### Memory Layout

\| Padding | Prologue Block | User Blocks | Free Blocks | Epilogue Header |

* Blocks contain a 64-bit header and 64-bit footer.
* Free blocks include `next` and `prev` pointers (circular doubly linked).
* Allocated blocks store the original payload size in the upper 32 bits of the header.

### Block Format

Each memory block includes:

* Header (8 bytes) — payload size, block size, flags
* Payload — client-usable memory
* Optional: Quick list or free list links (16 bytes)
* Footer (8 bytes) — identical to header, used for coalescing

### Header Bit Format

32 bits payload size | 28 bits block size | 2 unused | in\_qklst (1) | allocated (1)

All headers and footers are obfuscated with a runtime-generated MAGIC value using XOR to detect heap corruption.

---

## Allocation Strategy

1. Quick List Check
   Reuse block if available for matching size.

2. Free List Search
   Segregated first-fit policy begins at the appropriate size class.

3. Heap Growth
   If no free block is available, `sf_mem_grow()` adds a 4096-byte page.

4. Splitting
   Blocks are split when large enough, avoiding splinters smaller than 32 bytes.

---

## Freeing Strategy

* Small blocks are added to quick lists.
* If the quick list is full, it is flushed and all blocks are coalesced and inserted into the free list.
* Large blocks are immediately coalesced with adjacent free blocks.
* Coalesced blocks are added to free lists in LIFO order.

---

## Usage

**Build**
`make`

**Run Demo**
`./bin/sfmm`

**Run Tests**
`./bin/sfmm_tests`
`./bin/sfmm_tests --verbose --filter suite_name/test_name`

---

## Statistics

**sf\_fragmentation()**
Returns the ratio: `total_payload / total_allocated_block_size`

**sf\_utilization()**
Returns the peak ratio: `max_payload_seen / current_heap_size`

---

## Testing

* Uses Criterion test framework.
* Includes instructor-provided tests and 5+ custom tests.
* Covers alignment, splitting, coalescing, quick list overflow, invalid frees, and memory correctness.

---

## 📁 File Structure

```

.
├── include/
│   ├── sfmm.h          # Core data structures and allocator function prototypes
│   └── debug.h         # Debugging and logging utilities
├── src/
│   ├── sfmm.c          # Main allocator implementation (malloc, free, realloc)
│   └── main.c          # Sample usage / demo program
├── tests/
│   └── sfmm\_tests.c    # Unit tests written using Criterion
├── lib/
│   └── sfutil.o        # Provided utility object file (heap growth, start/end)
├── Makefile            # Build script for allocator and tests

```


## Restrictions

* No `malloc`, `calloc`, `realloc`, or `free` may be used.
* Memory must only grow via `sf_mem_grow()` in 4KB page increments.
* Header/footer obfuscation and proper alignment are mandatory.

---

## Concepts Demonstrated

* Manual heap management
* Bit masking and block metadata
* Secure memory practices (corruption detection)
* Pointer arithmetic
* Memory alignment
* Doubly linked list management
* Systems-level testing and instrumentation

---

## Author

Fardin Iqbal
Computer Science, Stony Brook University
GitHub: [https://github.com/yourusername](https://github.com/FardinIqbal)
LinkedIn: [https://www.linkedin.com/in/yourusername](https://www.linkedin.com/in/fardin-iqbal/)

---

## License

This project is intended for educational and demonstration purposes only. All rights reserved by the author.
