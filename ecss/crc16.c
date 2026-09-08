/*
 * Copyright (C) 2018-2026 Armin Luntzer (armin.luntzer@univie.ac.at)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/**
 * per-byte and sliced CRC16-CCITT-FALSE (i.e. the CRC for ECSS PUS packets)
 *
 * benchmarks taken on the target platform (LEON3/GR712):
 *
 * - typical packet sizes of 1, 2, 4, and 16 kiB
 * - benchmark timings are given in µs/kiB/MHz
 * - d-caches are flushed before benchmark, with the exception of columns
 *   marked (p) where the d-cache is (partially) primed with the CRC LUTs by prior CRC
 *
 *		1	1(p)	2	2(p)	4	4(p)	8	8(p)
 * slice-1	2.23	2.20	2.30	2.18	2.23	2.18	2.20	2.18
 * slice-2	2.06	1.64	1.83	1.63	1.72	1.62	1.67	1.75
 * slice-4	2.19	1.59	1.87	1.51	1.68	1.49	1.58	1.48
 * slice-8	2.00	1.46	1.70	1.27	1.48	1.21	1.34	1.20
 *
 * for clarification: slice-1 is conventional per-byte
 *
 * given the values above the optimum choice will always be slice-by-8
 */


#include <stdint.h>
#include <stddef.h>


/* this table will enable use of up to slice-by-8; if you really need the space
 * make sure to shrink it to the slice you want to use
 */
#define SLICES 8
static uint16_t  crc16_sliced_LUT[SLICES][256];
static uint16_t *crc16_LUT = crc16_sliced_LUT[0];


uint16_t crc16(uint8_t b, uint16_t crc)
{
	uint16_t idx;


	idx = ((crc >> 8) ^ b) & 0x00FF;

	return (((crc << 8) & 0xFF00) ^ crc16_LUT[idx]);
}


void crc_init_lookup_table(void)
{
	uint16_t i;
	uint16_t S;

	for (i = 0; i < 256; i++) {

		S = 0;

		if (i & (1 << 0))
			S = S ^ 0x1021;

		if (i & (1 << 1))
			S = S ^ 0x2042;

		if (i & (1 << 2))
			S = S ^ 0x4084;

		if (i & (1 << 3))
			S = S ^ 0x8108;

		if (i & (1 << 4))
			S = S ^ 0x1231;

		if (i & (1 << 5))
			S = S ^ 0x2462;

		if (i & (1 << 6))
			S = S ^ 0x48C4;

		if (i & (1 << 7))
			S = S ^ 0x9188;

		crc16_LUT[i] = S;
	}
}


uint16_t crc16_buf(const void *buf, size_t len)
{
	size_t i;

	uint16_t S = 0xffff;

	uint8_t *p = (uint8_t *)buf;


	if (!buf)
		return S;

	if (!len)
		return S;


	for (i = 0; i < len; i++)
		S = crc16(p[i], S);

	return S;
}


/* use the property LUT[i ^ j] == LUT[i] ^ LUT[j] for sliced CRC*/
void crc16_init_sliced_lookup_table(void)
{
	size_t n;
	size_t k;

	uint16_t crc;


	crc_init_lookup_table();

	/* generate CRCs for all single byte sequences */
	for (n = 0; n < 256; n++)
		crc16_sliced_LUT[0][n] = crc16_LUT[n];

	/* generate nested CRC crc16_sliced_LUT for future slice-by-8 lookup */
	for (n = 0; n < 256; n++) {
		crc = crc16_sliced_LUT[0][n];
		for (k = 1; k < SLICES; k++) {
			crc = crc16_sliced_LUT[0][(crc >> 8) & 0xff] ^ (crc << 8);
			crc16_sliced_LUT[k][n] = crc;
		}
	}
}


uint16_t crc16_slice2(const void *buf, size_t len)
{
	uint16_t n;

	uint16_t crc = 0xffff;

	uint8_t *next;


	next = (uint8_t *)buf;

	/* align from head until dword boundary */
	while (len && ((uintptr_t)next & 0x7) != 0) {
		crc = crc16_sliced_LUT[0][((crc >> 8) ^ (*next++)) & 0xff] ^ (crc << 8);
		len--;
	}


	/* aligned section */
	while (len >= sizeof(n)) {
		int idx0;
		int idx1;

		n = (*(uint16_t *)next);

#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		idx0 = ((n >> 8) & 0xff) ^ ((crc >> 8) & 0xff);
		idx1 = ((n >> 0) & 0xff) ^ ((crc >> 0) & 0xff);
#else /* __BYTE_ORDER __ */
		idx0 = ((n >>  0) & 0xff) ^ ((crc >> 8) & 0xff);
		idx1 = ((n >>  8) & 0xff) ^ ((crc >> 0) & 0xff);
#endif /* __BYTE_ORDER __ */

		crc  = crc16_sliced_LUT[1][idx0];
	        crc ^= crc16_sliced_LUT[0][idx1];

		next += sizeof(n);
		len  -= sizeof(n);
	}

	/* tail bytes */
	while (len) {
		crc = crc16_sliced_LUT[0][((crc >> 8) ^ (*next++)) & 0xff] ^ (crc << 8);
		len--;
	}

	return crc;
}


uint16_t crc16_slice4(const void *buf, size_t len)
{
	uint32_t n;

	uint16_t crc = 0xffff;

	uint8_t *next;


	next = (uint8_t *)buf;

	/* align from head until dword boundary */
	while (len && ((uintptr_t)next & 0x7) != 0) {
		crc = crc16_sliced_LUT[0][((crc >> 8) ^ (*next++)) & 0xff] ^ (crc << 8);
		len--;
	}

	/* aligned section */
	while (len >= sizeof(n)) {
		int idx0;
		int idx1;
		int idx2;
		int idx3;

		n = (*(uint32_t *)next);

#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		idx0 = ((n >> 24) & 0xff) ^ ((crc >> 8) & 0xff);
		idx1 = ((n >> 16) & 0xff) ^ ((crc >> 0) & 0xff);
		idx2 =  (n >>  8) & 0xff;
		idx3 =  (n >>  0) & 0xff;
#else /* __BYTE_ORDER __ */
		idx0 = ((n >>  0) & 0xff) ^ ((crc >> 8) & 0xff);
		idx1 = ((n >>  8) & 0xff) ^ ((crc >> 0) & 0xff);
		idx2 =  (n >> 16) & 0xff;
		idx3 =  (int)((n >> 24) & 0xff);
#endif /* __BYTE_ORDER __ */

		crc  = crc16_sliced_LUT[3][idx0];
	        crc ^= crc16_sliced_LUT[2][idx1];
	        crc ^= crc16_sliced_LUT[1][idx2];
	        crc ^= crc16_sliced_LUT[0][idx3];

		next += sizeof(n);
		len  -= sizeof(n);
	}

	/* tail bytes */
	while (len) {
		crc = crc16_sliced_LUT[0][((crc >> 8) ^ (*next++)) & 0xff] ^ (crc << 8);
		len--;
	}

	return crc;
}


/* needed or sparc-gaisler-elf-gcc 13.2.1 (bcc-v2.3.1)
 * will not always generate 64-bit load instructions
 */

#if (__sparc__)
#pragma GCC optimize("no-strict-aliasing")
#include <compiler.h>
__diag_push()
__diag_ignore(GCC, 7, "-Wvolatile-register-var", "this is actually what we need to force the optimsation")
#endif /* __sparc__ */

uint16_t crc16_slice8(const void *buf, size_t len)
{
#if (__sparc__)
	register volatile uint64_t n asm("%l0");
#else
	uint64_t n;
#endif
	uint16_t crc = 0xffff;
	uint8_t *next = (uint8_t *)buf;

	uint64_t *pp;


	/* align from head until dword boundary */
	while (len && ((uintptr_t)next & 7) != 0) {
		crc = crc16_sliced_LUT[0][((crc >> 8) ^ (*next++)) & 0xff] ^ (crc << 8);
		len--;
	}

	pp = (uint64_t *)next;

	/* aligned section */
	while (len >= sizeof(n)) {
		int idx0;
		int idx1;
		int idx2;
		int idx3;
		int idx4;
		int idx5;
		int idx6;
		int idx7;


		n = pp[0];

#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		idx0 = ((n >> 56) & 0xff) ^ ((crc >> 8) & 0xff);
		idx1 = ((n >> 48) & 0xff) ^ ((crc >> 0) & 0xff);
		idx2 =  (n >> 40) & 0xff;
		idx3 =  (n >> 32) & 0xff;
		idx4 =  (n >> 24) & 0xff;
		idx5 =  (n >> 16) & 0xff;
		idx6 =  (n >>  8) & 0xff;
		idx7 =  (n >>  0) & 0xff;
#else /* __BYTE_ORDER __ */
		idx0 = ((n >>  0) & 0xff) ^ ((crc >> 8) & 0xff);
		idx1 = ((n >>  8) & 0xff) ^ ((crc >> 0) & 0xff);
		idx2 =  (n >> 16) & 0xff;
		idx3 =  (n >> 24) & 0xff;
		idx4 =  (n >> 32) & 0xff;
		idx5 =  (n >> 40) & 0xff;
		idx6 =  (n >> 48) & 0xff;
		idx7 =  (int)((n >> 56) & 0xff);

#endif /* __BYTE_ORDER __ */
		crc  = crc16_sliced_LUT[7][idx0];
	        crc ^= crc16_sliced_LUT[6][idx1];
	        crc ^= crc16_sliced_LUT[5][idx2];
	        crc ^= crc16_sliced_LUT[4][idx3];
	        crc ^= crc16_sliced_LUT[3][idx4];
	        crc ^= crc16_sliced_LUT[2][idx5];
	        crc ^= crc16_sliced_LUT[1][idx6];
	        crc ^= crc16_sliced_LUT[0][idx7];

		pp++;
		len -= sizeof(n);
	}

	next = (void *)pp;

	/* tail bytes */
	while (len) {
		crc = crc16_sliced_LUT[0][((crc >> 8) ^ (*next++)) & 0xff] ^ (crc << 8);
		len--;
	}

	return crc;
}


#if (__sparc__)
__diag_pop()
#endif /* __sparc__ */
