/**
 * @file bitman.c
 *
 * @copyright GPLv2
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

#include <stdint.h>
#include <limits.h>
#include <string.h>


static void putbits8(void *dst, size_t d_off, void *src, size_t s_off, size_t nbits)
{
	uint8_t tmp;

	size_t bits;
	size_t end;

	uint8_t *p;


	if (!nbits)
		return;

	/* fetch the source bits first */
	p = (uint8_t *)src + (s_off >> 3);	/* offset into octet array */
	bits = s_off & 0x7;			/* remaining bit offset within octet */
	end = bits + nbits;			/* relative offset in bits from octet */
	if (end <= 8) {
		/* all bits are within source octet, mask relevant bits */
		tmp = (*p) >> (8 - nbits - bits);
		tmp &= 0xff >> (8 - nbits);
	} else {
		size_t nb;

		/* grab bits from first octet, mask and shift to proper position */
		nb = nbits + bits  - 8;
		tmp = (uint8_t)(((*p) & (0xff >> (bits))) << nb);

		/* grab remaining bits and insert */
		tmp |= ((*(p + 1) >> (8 - nb)) & (0xff >> (8 - nb)));
	}


	/* put into destination */
	p = (uint8_t *)dst + (d_off >> 3);
	bits = d_off & 0x7;
	end = bits + nbits;
	if (end <= 8) {
		/* clear destination bits within octet */
		(*p) &= (uint8_t)~((0xff >> (8 - nbits)) << (8 - end));
		(*p) |= tmp << (8 - end);
	} else {
		/* clear destination bits in first octet */
		(*p) &= (uint8_t)~(0xff >> bits);
		(*p) |= tmp >> bits;

		/* insert remaining bits in next octet */
		(*(p + 1)) &= (uint8_t)~((0xff >> (nbits - bits)) << (8 - bits));
		(*(p + 1)) |= tmp << (8 - bits);
	}
}


/*
 * @note this is for arbitrary bitfields based on octets only;
 *	 if you need endianness conversion, do it yourself
 *
 * @note this follows the same alignment logic as memmove_fwd() in flightos libc
 */
int bitcpy(void *dst, size_t d_off, void *src, size_t s_off, size_t nbits)
{
	size_t c;
	size_t b;

	uint8_t *d;
	uint8_t *s;


	if (!dst)
		goto error;

	if (!src)
		goto error;

	if (!nbits)
		goto exit;


	if ((d_off | s_off) & (CHAR_BIT - 1)) {

		/* if the bit offsets are not aligned to octet boundaries but
		 * can be aligned by advancing a few bits, copy those bits,
		 * then move on to more efficient memcpy() for the main segment.
		 * If both addresses are always unaligned, just do a
		 * copy using putbits8() only
		 */

		if (nbits < CHAR_BIT) {
			c = nbits;	/* there isn't much to copy ... */
		} else if ((d_off ^ s_off) & (CHAR_BIT - 1)) {
			c = nbits;
		} else {
			/* offsets can be aligned to an octet boundary */
			c = CHAR_BIT - (d_off & (CHAR_BIT - 1));
		}

		/* adjust */
		nbits -= c;

		/* copy lead-in bits or full block if unaligned */
		while (c) {
			size_t n = CHAR_BIT;

			if (c < CHAR_BIT)
				n = c;

			putbits8(dst, d_off, src, s_off, n);
			c     -= n;
			d_off += n;
			s_off += n;
		}
	}

	c = nbits / CHAR_BIT;	/* bytes to copy */
	b = c * CHAR_BIT;	/* bits copied in memcpy() */

	/* use fast copy for main block and adjust */
	d = (uint8_t *)dst + d_off / CHAR_BIT;
	s = (uint8_t *)src + s_off / CHAR_BIT;
	memcpy(d, s, c);
	d_off += b;
	s_off += b;
	nbits -= b;	/* bits in tail remain */

	/* copy tail bits */
	putbits8(dst, d_off, src, s_off, nbits);

exit:
	return 0;
error:
	return -1;
}
