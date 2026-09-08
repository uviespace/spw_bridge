/**
 * @file ccsds_pkt.c
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

#include <ccsds_pkt.h>



/**
 * @brief get size of then CCSDS header
 */

size_t ccsds_get_hdr_size(void)
{
	return SPP_PKT_HDR_LEN;
}


/**
 * @brief get the pointer to the SPP packet's payload section
 *
 * @returns a pointer into the buffer to the first byte after the header section
 *	    or NULL on error;
 */

uint8_t *ccsds_get_payload(uint8_t *pkt)
{
	if (!pkt)
		return NULL;

	return &pkt[SPP_PKT_HDR_LEN];
}


/**
 * @brief get the packet version
 * @returns all bits set (-1) on error
 */

uint8_t ccsds_get_pkt_version(uint8_t *pkt)
{
	if (!pkt)
		return (uint8_t) -1;

	return pkt[SPP_PKT_VER_OFFSET] >> SPP_PKT_VER_SHIFT;
}


/**
 * @brief set the packet version
 */

void ccsds_set_pkt_version(uint8_t *pkt, uint8_t v)
{
	uint8_t b;


	if (!pkt)
		return;

	b = pkt[SPP_PKT_VER_OFFSET];
	b &= (uint8_t)~SPP_PKT_VER_MASK;
	b |= v << SPP_PKT_VER_SHIFT;

	pkt[SPP_PKT_VER_OFFSET] = b;
}


/**
 * @brief get the length of a packet's payload
 *
 * @returns 0 on error, > 0 otherwise
 */

uint16_t ccsds_get_data_len(uint8_t *pkt)
{
	uint16_t hi;
	uint16_t lo;
	uint16_t len;


	if (!pkt)
		return 0;

	hi = (uint16_t) pkt[SPP_LEN_HI_OFFSET];
	lo = (uint16_t) pkt[SPP_LEN_LO_OFFSET];

	/* we are 16 bit unsigned */
	len = (hi << 8) | (lo & 0xff);

	/**
	 * see 4.1.3.5 in CCSDS 133.0-B-2
	 * note: the packet data field is mandatory as per 4.1.4 so there will always
	 * be at least one payload byte present
	 */
	len += 1;

	return len;
}


/**
 * @brief set the length of a packet's payload
 */

void ccsds_set_data_len(uint8_t *pkt, uint16_t len)
{
	if (!pkt)
		return;

	len -= 1;	/* 4.1.3.5 in CCSDS 133.0-B-2 */

	/* we are 16 bit unsigned */
	pkt[SPP_LEN_HI_OFFSET] = (uint8_t)(len >> 8);
	pkt[SPP_LEN_LO_OFFSET] = (uint8_t)(len & 0xff);
}


/**
 * @brief get the packet type
 *
 * @returns all bits set (-1) on error, otherwise 0: TM, 1: TC
 */

uint8_t ccsds_get_pkt_type(uint8_t *pkt)
{
	uint8_t t;


	if (!pkt)
		return (uint8_t) -1;


	t = pkt[SPP_PKT_TYPE_OFFSET];

	return (t & SPP_PKT_TYPE_MASK) >> SPP_PKT_TYPE_SHIFT;
}


/**
 * @brief set the type of a packet
 *
 * @param t	0: TM, otherwise: TC
 */

void ccsds_set_pkt_type(uint8_t *pkt, uint8_t t)
{
	uint8_t b;


	if (!pkt)
		return;

	if (t)
		t = 1;

	b = pkt[SPP_PKT_TYPE_OFFSET];

	b &= (uint8_t)~SPP_PKT_TYPE_MASK;
	b |= t << SPP_PKT_TYPE_SHIFT;

	pkt[SPP_PKT_TYPE_OFFSET] = b;
}


/**
 * @brief get the secondary header flag
 *
 * @returns all bits set (-1) on error, otherwise 0: not present, 1: present
 */

uint8_t ccsds_get_2nd_hdr_flag(uint8_t *pkt)
{
	uint8_t t;


	if (!pkt)
		return (uint8_t)-1;


	t = pkt[SPP_2ND_HDR_FLAG_OFFSET];

	return (t & SPP_2ND_HDR_FLAG_MASK) >> SPP_2ND_HDR_FLAG_SHIFT;
}


/**
 * @brief set the secondary header flag
 *
 * @param t 0: not present, otherwise: present
 */

void ccsds_set_2nd_hdr_flag(uint8_t *pkt, uint8_t t)
{
	uint8_t b;


	if (!pkt)
		return;

	if (t)
		t = 1;

	b = pkt[SPP_2ND_HDR_FLAG_OFFSET];

	b &= (uint8_t)~SPP_2ND_HDR_FLAG_MASK;
	b |= t << SPP_2ND_HDR_FLAG_SHIFT;

	pkt[SPP_2ND_HDR_FLAG_OFFSET] = b;
}


/**
 * @brief get the application id
 *
 * @returns all bits set (-1) on error, otherwise 11-bit apid
 */

uint16_t ccsds_get_apid(uint8_t *pkt)
{
	uint16_t hi;
	uint16_t lo;


	if (!pkt)
		return (uint16_t) -1;


	hi = (uint16_t) pkt[SPP_APID_OFFSET_1] & SPP_APID_OFFSET_1_MASK;
	lo = (uint16_t) pkt[SPP_APID_OFFSET_2];

	/* we are 16 bit unsigned, 2nd byte is a full octet */
	return (hi << 8) | (lo & 0xff);
}


/**
 * @brief set the application id
 *
 * @param a 11-bit apid
 */

void ccsds_set_apid(uint8_t *pkt, uint16_t a)
{
	uint8_t b;


	if (!pkt)
		return;

	if (a > (1 << 11) - 1)
		return;

	b = pkt[SPP_APID_OFFSET_1];
	b &= (uint8_t)~SPP_APID_OFFSET_1_MASK;
	b |= (a >> 8) & SPP_APID_OFFSET_1_MASK;	/* 2nd byte is full octet */

	pkt[SPP_APID_OFFSET_1] = b;
	pkt[SPP_APID_OFFSET_2] = (uint8_t)(a & 0xff);	/* see above */
}


/**
 * @brief get the sequence flags
 *
 * @returns all bits set (-1) on error, otherwise:
 *	    0: continuation segment of user data
 *	    1: first segment of user data
 *	    2: last segment of user data
 *	    3: unsegmented user data
 */

uint8_t ccsds_get_seq_flags(uint8_t *pkt)
{
	uint8_t t;


	if (!pkt)
		return (uint8_t) -1;


	t = pkt[SPP_SEQ_FLAGS_OFFSET];

	return (t & SPP_SEQ_FLAGS_MASK) >> SPP_SEQ_FLAGS_SHIFT;
}


/**
 * @brief set the sequence flags
 *
 * @param   0: continuation segment of user data
 *	    1: first segment of user data
 *	    2: last segment of user data
 *	    3: unsegmented user data
 */

void ccsds_set_seq_flags(uint8_t *pkt, uint8_t t)
{
	uint8_t b;


	if (!pkt)
		return;

	if (t > SPP_SEQ_FLAG_UNSEG)
		return;

	b = pkt[SPP_SEQ_FLAGS_OFFSET];

	b &= (uint8_t)~SPP_SEQ_FLAGS_MASK;
	b |= t << SPP_SEQ_FLAGS_SHIFT;

	pkt[SPP_SEQ_FLAGS_OFFSET] = b;
}


/**
 * @brief get the sequence counter
 *
 * @returns all bits set (-1) on error, otherwise 14-bit counter
 */

uint16_t ccsds_get_seq_cnt(uint8_t *pkt)
{
	uint16_t hi;
	uint16_t lo;


	if (!pkt)
		return (uint16_t) -1;


	hi = (uint16_t) pkt[SPP_SEQ_CNT_OFFSET_1] & SPP_SEQ_CNT_OFFSET_1_MASK;
	lo = (uint16_t) pkt[SPP_SEQ_CNT_OFFSET_2];

	/* we are 16 bit unsigned, 2nd byte is a full octet */
	return (hi << 8) | (lo & 0xff);
}


/**
 * @brief set the sequence counter or packet name
 *
 * @param c a 14-bit counter or packet name
 */

void ccsds_set_seq_cnt(uint8_t *pkt, uint16_t c)
{
	uint8_t b;


	if (!pkt)
		return;

	if (c > (1 << 14) - 1)
		return;

	b = pkt[SPP_SEQ_CNT_OFFSET_1];
	b &= (uint8_t)~SPP_SEQ_CNT_OFFSET_1_MASK;
	b |= (c >> 8) & SPP_SEQ_CNT_OFFSET_1_MASK;	/* 2nd byte is full octet */

	pkt[SPP_SEQ_CNT_OFFSET_1] = b;
	pkt[SPP_SEQ_CNT_OFFSET_2] = (uint8_t)(c & 0xff);	/* see above */
}
