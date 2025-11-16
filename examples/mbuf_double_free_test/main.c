/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2016 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 32

/*
 * Example application demonstrating DPDK mbuf double free detection.
 *
 * This application shows how the RTE_LIBRTE_MBUF_DEBUG feature
 * can detect when an mbuf is freed twice, which is a common bug that can
 * lead to memory corruption and crashes.
 *
 * To enable double free detection, compile DPDK with:
 *   CONFIG_RTE_LIBRTE_MBUF_DEBUG=y
 *
 * The test will:
 * 1. Allocate mbufs from a pool
 * 2. Demonstrate correct free operation
 * 3. Optionally trigger a double free to show detection (if TEST_DOUBLE_FREE is set)
 */

/* Set to 1 to trigger double free detection (will cause panic) */
static int test_double_free = 0;

static void
test_mbuf_operations(struct rte_mempool *mbuf_pool)
{
	struct rte_mbuf *m1, *m2, *m3;

	printf("\n========================================\n");
	printf("Testing MBUF Double Free Detection\n");
	printf("========================================\n\n");

	/* Test 1: Normal allocation and free */
	printf("Test 1: Normal allocation and free\n");
	printf("-----------------------------------\n");

	m1 = rte_pktmbuf_alloc(mbuf_pool);
	if (m1 == NULL) {
		printf("ERROR: Failed to allocate mbuf\n");
		return;
	}
	printf("✓ Allocated mbuf %p (refcnt=%u)\n", m1, rte_mbuf_refcnt_read(m1));

	rte_pktmbuf_free(m1);
	printf("✓ Freed mbuf %p successfully\n", m1);
	printf("\n");

	/* Test 2: Allocation, use, and free with data */
	printf("Test 2: Mbuf with data\n");
	printf("----------------------\n");

	m2 = rte_pktmbuf_alloc(mbuf_pool);
	if (m2 == NULL) {
		printf("ERROR: Failed to allocate mbuf\n");
		return;
	}

	/* Add some data to the mbuf */
	char *data = rte_pktmbuf_append(m2, 64);
	if (data) {
		memset(data, 0xAB, 64);
		printf("✓ Allocated mbuf %p with 64 bytes of data\n", m2);
	}

	rte_pktmbuf_free(m2);
	printf("✓ Freed mbuf %p successfully\n", m2);
	printf("\n");

	/* Test 3: Reference counting */
	printf("Test 3: Reference counting\n");
	printf("--------------------------\n");

	m3 = rte_pktmbuf_alloc(mbuf_pool);
	if (m3 == NULL) {
		printf("ERROR: Failed to allocate mbuf\n");
		return;
	}
	printf("✓ Allocated mbuf %p (refcnt=%u)\n", m3, rte_mbuf_refcnt_read(m3));

	/* Increment reference count */
	rte_mbuf_refcnt_update(m3, 1);
	printf("✓ Incremented refcnt to %u\n", rte_mbuf_refcnt_read(m3));

	/* First free decrements refcnt but doesn't free mbuf */
	rte_pktmbuf_free(m3);
	printf("✓ Called free (refcnt now should be 1, mbuf still in use)\n");

	/* Allocate again to verify pool is working */
	struct rte_mbuf *m4 = rte_pktmbuf_alloc(mbuf_pool);
	if (m4 == NULL) {
		printf("ERROR: Failed to allocate mbuf\n");
		return;
	}
	printf("✓ Allocated new mbuf %p (refcnt=%u)\n", m4, rte_mbuf_refcnt_read(m4));
	rte_pktmbuf_free(m4);
	printf("✓ Freed new mbuf successfully\n");
	printf("\n");

	/* Test 4: Double free detection (if enabled) */
	if (test_double_free) {
		printf("Test 4: Double Free Detection (THIS WILL PANIC!)\n");
		printf("--------------------------------------------------\n");
		printf("Allocating an mbuf and freeing it twice...\n");

		struct rte_mbuf *m_double = rte_pktmbuf_alloc(mbuf_pool);
		if (m_double == NULL) {
			printf("ERROR: Failed to allocate mbuf\n");
			return;
		}
		printf("✓ Allocated mbuf %p (refcnt=%u)\n",
		       m_double, rte_mbuf_refcnt_read(m_double));

		/* First free - this is OK */
		printf("Freeing mbuf for the first time...\n");
		rte_pktmbuf_free(m_double);
		printf("✓ First free completed\n");

		/* Second free - THIS SHOULD BE CAUGHT! */
		printf("\n*** Attempting double free (should trigger panic) ***\n");
		rte_pktmbuf_free(m_double);

		/* We should never reach here if detection is enabled */
		printf("ERROR: Double free was NOT detected! Check that "
		       "CONFIG_RTE_LIBRTE_MBUF_DEBUG=y\n");
	}

	printf("========================================\n");
	printf("All tests completed successfully!\n");
	printf("========================================\n\n");

#ifdef RTE_LIBRTE_MBUF_DEBUG
	printf("Double free detection is ENABLED\n");
#else
	printf("Double free detection is DISABLED\n");
	printf("To enable, rebuild DPDK with CONFIG_RTE_LIBRTE_MBUF_DEBUG=y\n");
#endif
}

int
main(int argc, char **argv)
{
	struct rte_mempool *mbuf_pool;
	int ret;

	/* Initialize EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_panic("Cannot init EAL\n");

	/* Check if user wants to test double free */
	if (argc > 1 && strcmp(argv[argc-1], "--test-double-free") == 0) {
		test_double_free = 1;
		printf("\n");
		printf("WARNING: Double free test enabled!\n");
		printf("This will intentionally trigger a panic to demonstrate detection.\n");
		printf("\n");
	}

	/* Create mbuf pool */
	mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
		rte_socket_id());

	if (mbuf_pool == NULL)
		rte_panic("Cannot create mbuf pool\n");

	printf("Created mbuf pool '%s' with %u mbufs\n\n",
	       mbuf_pool->name, NUM_MBUFS);

	/* Run the tests */
	test_mbuf_operations(mbuf_pool);

	return 0;
}
