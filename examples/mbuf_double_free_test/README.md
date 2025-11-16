# DPDK Mbuf Double Free Detection

## Overview

This example demonstrates the DPDK mbuf double free detection feature. Double free bugs occur when the same memory buffer (mbuf) is freed multiple times, which can lead to:

- Memory corruption
- Crashes and segmentation faults
- Security vulnerabilities
- Unpredictable behavior

The `RTE_LIBRTE_MBUF_DEBUG` feature provides runtime detection of these bugs by tracking the freed state of mbufs using a magic number marker.

## How It Works

The double free detection mechanism works by:

1. **Marking freed mbufs**: When an mbuf's reference count reaches 0 and it's returned to the pool, a magic number (`0xDEADBEEFFEEDFACE`) is stored in the mbuf's `udata64` field.

2. **Checking before free**: Before freeing an mbuf, the code checks:
   - If the magic number is present (indicating the mbuf was already freed)
   - If the reference count is already 0 (indicating it's already in the pool)

3. **Panic on detection**: If a double free is detected, the code calls `rte_panic()` with a detailed error message showing:
   - The mbuf pointer
   - The pool name
   - A description of the error

4. **Clearing on allocation**: When an mbuf is allocated from the pool, the magic number is cleared to prevent false positives.

## Enabling Double Free Detection

### Method 1: Build-time Configuration

Edit `config/common_base` and set:

```bash
CONFIG_RTE_LIBRTE_MBUF_DEBUG=y
```

Then rebuild DPDK:

```bash
make config T=x86_64-native-linuxapp-gcc
make
```

### Method 2: Command-line Override

```bash
make config T=x86_64-native-linuxapp-gcc
sed -ri 's,(CONFIG_RTE_LIBRTE_MBUF_DEBUG=).*,\1y,' build/.config
make
```

## Building the Example

```bash
export RTE_SDK=/path/to/dpdk
export RTE_TARGET=x86_64-native-linuxapp-gcc

cd examples/mbuf_double_free_test
make
```

## Running the Example

### Normal Test (No Double Free)

This runs tests for normal mbuf allocation, freeing, and reference counting:

```bash
./build/mbuf_double_free_test -c 0x1 -n 4
```

Expected output:
```
Created mbuf pool 'mbuf_pool' with 1024 mbufs

========================================
Testing MBUF Double Free Detection
========================================

Test 1: Normal allocation and free
-----------------------------------
✓ Allocated mbuf 0x7f... (refcnt=1)
✓ Freed mbuf 0x7f... successfully

Test 2: Mbuf with data
----------------------
✓ Allocated mbuf 0x7f... with 64 bytes of data
✓ Freed mbuf 0x7f... successfully

Test 3: Reference counting
--------------------------
✓ Allocated mbuf 0x7f... (refcnt=1)
✓ Incremented refcnt to 2
✓ Called free (refcnt now should be 1, mbuf still in use)
✓ Allocated new mbuf 0x7f... (refcnt=1)
✓ Freed new mbuf successfully

========================================
All tests completed successfully!
========================================

Double free detection is ENABLED
```

### Double Free Test (Triggers Detection)

This intentionally triggers a double free to demonstrate the detection:

```bash
./build/mbuf_double_free_test -c 0x1 -n 4 -- --test-double-free
```

Expected output (will panic):
```
WARNING: Double free test enabled!
This will intentionally trigger a panic to demonstrate detection.

Created mbuf pool 'mbuf_pool' with 1024 mbufs

========================================
Testing MBUF Double Free Detection
========================================

[... normal tests ...]

Test 4: Double Free Detection (THIS WILL PANIC!)
--------------------------------------------------
Allocating an mbuf and freeing it twice...
✓ Allocated mbuf 0x7f... (refcnt=1)
Freeing mbuf for the first time...
✓ First free completed

*** Attempting double free (should trigger panic) ***
PANIC in __rte_mbuf_raw_free():
Double free detected for mbuf 0x7f... from pool mbuf_pool
This mbuf was already freed and is being freed again!
```

## Performance Considerations

The double free detection adds a small overhead:

- **Memory**: Uses 8 bytes (`udata64`) per mbuf when freed
- **CPU**: Two checks per free operation (magic number check + refcnt check)
- **Conditional compilation**: Only active when `RTE_LIBRTE_MBUF_DEBUG` is enabled

### Overhead Breakdown

- Magic number check: ~1 CPU cycle (comparison + unlikely branch)
- Magic number write: ~1 CPU cycle (write to memory already in cache)
- Total overhead: ~2-3 CPU cycles per free operation

This is negligible compared to the cost of:
- Reference count operations (atomic operations)
- Mempool operations (returning to pool)
- Cache line management

### When to Enable

- **Development**: Always enable during development to catch bugs early
- **Testing**: Enable for QA and testing environments
- **Production**: Consider enabling if:
  - You suspect memory corruption issues
  - You're debugging crashes related to mbufs
  - Performance impact is acceptable for your use case
  - Disable for performance-critical production deployments

## Integration with Your Application

To use double free detection in your DPDK application:

1. **Enable at build time**: Set `CONFIG_RTE_LIBRTE_MBUF_DEBUG=y` in your DPDK config

2. **No code changes needed**: The detection is automatic and built into the mbuf library

3. **Handle panics**: Be prepared for `rte_panic()` calls if double frees are detected

4. **Optional compile-time checks**:
   ```c
   #ifdef RTE_LIBRTE_MBUF_DEBUG
   printf("Double free detection is enabled\n");
   #else
   printf("Double free detection is disabled\n");
   #endif
   ```

## Common Double Free Scenarios Detected

### Scenario 1: Direct Double Free
```c
struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
rte_pktmbuf_free(m);  // First free - OK
rte_pktmbuf_free(m);  // Second free - DETECTED!
```

### Scenario 2: Use After Free
```c
struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
rte_pktmbuf_free(m);
// ... later ...
rte_pktmbuf_free(m);  // DETECTED!
```

### Scenario 3: Incorrect Reference Counting
```c
struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
// Reference count is 1
rte_pktmbuf_free(m);  // Refcnt becomes 0, mbuf freed
rte_pktmbuf_free(m);  // DETECTED! Refcnt was already 0
```

### Scenario 4: Freed Mbuf in Segmented Chain
```c
struct rte_mbuf *m1 = rte_pktmbuf_alloc(pool);
struct rte_mbuf *m2 = rte_pktmbuf_alloc(pool);
m1->next = m2;
m1->nb_segs = 2;

rte_pktmbuf_free(m1);  // Frees both m1 and m2
rte_pktmbuf_free(m2);  // DETECTED! m2 was already freed
```

## Troubleshooting

### "Double free was NOT detected" Message

If you see this message, double free detection is disabled. Rebuild DPDK with:
```bash
CONFIG_RTE_LIBRTE_MBUF_DEBUG=y
```

### False Positives

False positives should not occur because:
- The magic marker is cleared on allocation
- The check happens before the mbuf is freed
- The magic number is highly unlikely to occur naturally (`0xDEADBEEFFEEDFACE`)

### Application Uses `udata64` Field

If your application uses the `udata64` field for its own purposes, you have two options:

1. **Disable double free detection** in production (only enable for testing)
2. **Use a different field** in your application (e.g., `userdata` pointer)

The detection only uses `udata64` when the mbuf is in the freed state (in the pool), so it won't conflict with normal usage.

## Implementation Details

The implementation is located in:

- **Configuration**: `config/common_base` - `CONFIG_RTE_LIBRTE_MBUF_DEBUG`
- **Core logic**: `lib/librte_mbuf/rte_mbuf.h`
  - Magic number definition
  - Detection in `__rte_mbuf_raw_free()` (line ~1188)
  - Detection in `__rte_pktmbuf_prefree_seg()` (line ~1606)
  - Clearing in `rte_mbuf_raw_alloc()` (line ~1167)

## License

This example is licensed under the BSD license, the same as DPDK.

## See Also

- DPDK Programmer's Guide: Mbuf Library
- DPDK API Reference: rte_mbuf.h
- DPDK Mempool Library Documentation
