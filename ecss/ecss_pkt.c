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
 * @brief provides functions for manipulating ECSS compliant TMTC packets
 * @note this provides extra functionality on top of the CCSDS base functions
 *
 * NOTE: unless otherwise noted, all references refer to ECSS-E-ST-70-41C
 *
 * if in doubt, consult:
 *	 ECSS-E-70-41A
 *	 ECSS-E-ST-70-41C
 * and optionally:
 *	CSDS 133.0-B-2
 *
 *
 * NOTE: ecss_is_pkt_valid() shall be called on every new buffer before using
 *	 any of the other functions made available here, as they BY DESIGN
 *	 will not perform any sanity/error checks already done in that function.
 *
 *	 Ignore this rule at your own peril.
 */


#include <limits.h>
#include <ecss_pkt.h>
#include <bitman.h>


static int ecss_pus_version_valid(uint8_t v)
{
	switch (v) {
	case PUS_A_VERSION:	/* fallthrough */
	case PUS_C_VERSION:	/* fallthrough */
		return 1;
	case PUS_ESA_VERSION:	/* fallthrough */
	default:
		return 0;
	}
}


/**
 * @brief retrieve the 4-bit TC packet/PUS version number field
 *
 *
 * @note compared to 7.4.4 ECSS-E-ST-70-41C, 5.4.3 ECSS-E-70-41A had this
 *	 split into 1/3 bit segments, because CCSDS 203.0-B-2 mandates that
 *	 the first bit is zero to indicate a non-CCSDS defined secondary header.
 *	 This is implicit given the legal values of the fields, so we only
 *	 use the more recent definition.
 *
 * @returns all bits set (-1) on error, PUS version otherwise
 */

uint8_t ecss_get_pkt_version(uint8_t *pkt)
{
	uint8_t b;
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return (uint8_t) -1;

	p = ccsds_get_payload(pkt);

	b = p[PUS_ECSS_VER_OFFSET] & PUS_ECSS_VER_MASK;

	return b >> PUS_ECSS_VER_SHIFT;
}


/**
 * @brief set the 4-bit TC packet/PUS version number field
 *
 * @note only PUS A and C are legal values
 *
 * @returns < 0 on error
 */

int ecss_set_pkt_version(uint8_t *pkt, uint8_t v)
{
	uint8_t b;
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (!ecss_pus_version_valid(v))
		return -1;

	p = ccsds_get_payload(pkt);

	b = p[PUS_ECSS_VER_OFFSET];

	b &= (uint8_t)~PUS_ECSS_VER_MASK;
	b |= v << PUS_ECSS_VER_SHIFT;

	p[PUS_ECSS_VER_OFFSET] = b;

	return 0;
}


/**
 * @brief retrieve the 4-bit S/C time reference status
 *
 *
 * @note contrary to 7.4.4 ECSS-E-ST-70-41C, 5.4.3 ECSS-E-70-41A 5.4.3 had this
 *	 nibble marked as "spare" and is supposed to be all-zero
 * *
 * @returns all bits set (-1) on error, 4-bit field value otherwise
 */

uint8_t ecss_get_sc_time_ref_status(uint8_t *pkt)
{
	uint8_t b;
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return (uint8_t)-1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return (uint8_t)-1;

	p = ccsds_get_payload(pkt);

	b = p[PUS_ECSS_TREF_OFFSET] & PUS_ECSS_TREF_MASK;

	return b >> PUS_ECSS_TREF_SHIFT;
}


/**
 * @brief set the 4-bit TC packet/PUS version number field
 *
 * @note applies only to PUS C and is mission-defined; set to all zero for PUS A
 *
 * @returns < 0 on error
 */

int ecss_set_sc_time_ref_status(uint8_t *pkt, uint8_t ref)
{
	uint8_t b;
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return -1;

	p = ccsds_get_payload(pkt);

	b = p[PUS_ECSS_TREF_OFFSET];

	b &= (uint8_t)~PUS_ECSS_TREF_MASK;
	b |= ref << PUS_ECSS_TREF_SHIFT;

	p[PUS_ECSS_TREF_OFFSET] = b;

	return 0;
}


/**
 * @brief retrieve the 4-bit TC packet/PUS ACK flags field
 *
 * @returns all bits set (-1) on error, 4-bit field otherwise
 */

uint8_t ecss_get_ack_flags(uint8_t *pkt)
{
	uint8_t b;
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return (uint8_t)-1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TC)
		return (uint8_t)-1;

	p = ccsds_get_payload(pkt);

	b = p[PUS_ECSS_ACK_OFFSET] & PUS_ECSS_ACK_MASK;

	return b >> PUS_ECSS_ACK_SHIFT;
}


/**
 * @brief set the 4-bit TC packet/PUS ACK flags field
 *
 * @param f the ack flags
 * @note: only the lower nibble may be set, otherwise flags will be rejected
 *
 * @returns < 0 on error
 */

int ecss_set_ack_flags(uint8_t *pkt, uint8_t f)
{
	uint8_t b;
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TC)
		return -1;

	if (f & ~PUS_ECSS_ACK_MASK)
		return -1;

	p = ccsds_get_payload(pkt);

	b = p[PUS_ECSS_ACK_OFFSET];

	b &= (uint8_t)~PUS_ECSS_ACK_MASK;
	b |= f << PUS_ECSS_ACK_SHIFT;

	p[PUS_ECSS_ACK_OFFSET] = b;

	return 0;
}


/**
 * @brief retrieve the 8-bit service type id
 *
 * @returns 0 on error/invalid ST
 *
 * 5.3.1.b legal range is 1-255 for ST
 * 5.3.1.c standard services are 1-127
 * 5.3.1.d mission specific services are 1-127
 */

uint8_t ecss_get_service_type(uint8_t *pkt)
{
	uint8_t *p;


	if (!pkt)
		return 0;

	p = ccsds_get_payload(pkt);

	return p[PUS_ECSS_ST_OFFSET];
}


/**
 * @brief set the 8-bit service type id
 *
 * @param st the service type id (valid: 1-255)
 *
 * @returns < 0 on error
 */

int ecss_set_service_type(uint8_t *pkt, uint8_t st)
{
	uint8_t *p;


	if (!pkt)
		return -1;

	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (!st)
		return -1;

	p = ccsds_get_payload(pkt);

	p[PUS_ECSS_ST_OFFSET] = st;

	return 0;
}


/**
 * @brief retrieve the 8-bit subservice type id
 *
 * @returns 0 on error
 *
 * 5.3.3.1.d legal range is 1-255 for SST
 * 5.3.3.1.e standard subservices use 1-127
 * 5.3.3.1.f mission specific subservices use 128-255
 */

uint8_t ecss_get_sub_service_type(uint8_t *pkt)
{
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return 0;

	p = ccsds_get_payload(pkt);

	return p[PUS_ECSS_SST_OFFSET];
}


/**
 * @brief set the 8-bit sub service type id
 *
 * @param st the service type id (valid: 1-255)
 *
 * @returns < 0 on error
 */

int ecss_set_sub_service_type(uint8_t *pkt, uint8_t sst)
{
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (!sst)
		return -1;

	p = ccsds_get_payload(pkt);

	p[PUS_ECSS_SST_OFFSET] = sst;

	return 0;
}


/**
 * @brief retrieve the 16-bit message type counter
 *
 * @note the returned counter is always 0 if not applicable
 */

uint16_t ecss_get_msg_type_cntr(uint8_t *pkt)
{
	uint16_t c;
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return 0;

	if (ecss_get_pkt_version(pkt) != PUS_C_VERSION)
		return 0;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return 0;


	p = ccsds_get_payload(pkt);


	c = ((uint16_t)p[PUS_ECSS_TCNT_OFFSET + 0] & 0xff) << 8;
	c |= (uint16_t)p[PUS_ECSS_TCNT_OFFSET + 1] & 0xff;

	return c;
}


/**
 * @brief set the 16-bit message type counter
 *
 * @param cntr the message type counter
 *
 * @returns < 0 on error
 */

int ecss_set_msg_type_cntr(uint8_t *pkt, uint16_t tcntr)
{
	uint8_t *p;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (ecss_get_pkt_version(pkt) != PUS_C_VERSION)
		return -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return -1;

	p = ccsds_get_payload(pkt);

	p[PUS_ECSS_TCNT_OFFSET + 0] = (uint8_t)(tcntr >> 8);
	p[PUS_ECSS_TCNT_OFFSET + 1] = (uint8_t)(tcntr & 0xff);

	return 0;
}


/**
 * @brief retrieve the (16-bit) destination ID
 *
 * @note this also works for PUS A as long as the number of bits configured
 *	 are <= 16
 *	 for PUS A, the returned value will be masked to the number of bits
 *	 specified, anything else is the responsibility of the user
 *
 * @returns the ID or all bits set if not applicable
 */

uint16_t ecss_get_destination_id(uint8_t *pkt)
{
	uint16_t d;
	uint8_t *p;

	size_t off = PUS_ECSS_DESTID_OFFSET;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return (uint16_t) -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return (uint16_t) -1;

	p = ccsds_get_payload(pkt);
	/* in PUS A, dest id is located either 0 or 1 octet after the SST field */
#if (AP_PUS_VERSION == PUS_A_VERSION)
	off = PUS_ECSS_SST_OFFSET + 1;
#if (PUS_A_TM_USE_PKT_SUB_CNTR != 0)
	off += 1;
#endif /* PUS_A_TM_USE_PKT_SUB_CNTR */
#endif /* AP_PUS_VERSION */

	d = ((uint16_t)p[off + 0] & 0xff) << 8;
#if (AP_PUS_VERSION == PUS_C_VERSION) || (PUS_A_TM_DEST_ID_BITS > 8)
	d |= (uint16_t)p[off + 1] & 0xff;
#endif

#if (AP_PUS_VERSION == PUS_A_VERSION)
	d &= (1 << PUS_A_TM_DEST_ID_BITS - 1);
#endif /* AP_PUS_VERSION */

	return d;
}


/**
 * @brief set the (16-bit) destination ID
 *
 * @note this also works for PUS A as long as the number of bits configured
 *	 are <= 16
 *	 for PUS A, the supplied value will be masked to the number of bits
 *	 specified
 *
 * @returns < 0 on error
 */

int ecss_set_destination_id(uint8_t *pkt, uint16_t dest_id)
{
	uint8_t *p;
	size_t off = PUS_ECSS_DESTID_OFFSET;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return (uint16_t) -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return (uint16_t) -1;

	p = ccsds_get_payload(pkt);

	/* in PUS A, dest id is located either 0 or 1 octet after the SST field */
#if (AP_PUS_VERSION == PUS_A_VERSION)
	off = PUS_ECSS_SST_OFFSET + 1;
#if (PUS_A_TM_USE_PKT_SUB_CNTR != 0)
	off += 1;
#endif /* PUS_A_TM_USE_PKT_SUB_CNTR */
#endif /* AP_PUS_VERSION */

#if (AP_PUS_VERSION == PUS_A_VERSION)
	dest_id = dest_id & (1 << PUS_A_TM_DEST_ID_BITS - 1);
#endif /* PUS_A_VERSION */

#if (PUS_A_TM_DEST_ID_BITS > 8)
	dest_id = dest_id << (16 - PUS_A_TM_DEST_ID_BITS);
#endif
	p[off + 0] = (uint8_t)((dest_id >> 8) & 0xff);
#if (AP_PUS_VERSION == PUS_C_VERSION) || (PUS_A_TM_DEST_ID_BITS > 8)
	/* no need to care about the spare bits, just overwrite the octet */
	p[off + 1] = (uint8_t)(dest_id & 0xff);
#endif
	return 0;
}


/**
 * @brief retrieve the timestamp
 *
 * @param[out] ts a sufficiently large array of octets
 *
 * @note the number of octets required is derived from the user-specified
 *	 number of bits/bytes; if the number of bits (as possible in PUS A)
 *	 do not correspond to an integer multiple of octets, the unused bits
 *	 in the MSB position (low array index) are padded 0
 *
 * @returns < 0 on error
 */

int ecss_get_timestamp(uint8_t *pkt, uint8_t *ts)
{
	uint8_t *p;

	size_t s_off;
	size_t d_off;
	size_t sz;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (!ts)
		return -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return -1;


	p = ccsds_get_payload(pkt);


	/* in PUS A, dest id is located either 0 or 1 octet after the SST field */
#if (AP_PUS_VERSION == PUS_A_VERSION)
	s_off = PUS_ECSS_SST_OFFSET + 1;
#if (PUS_A_TM_USE_PKT_SUB_CNTR != 0)
	s_off += 1;
#endif /* PUS_A_TM_USE_PKT_SUB_CNTR */
	/* convert to bits and add bit width of destination ID */
	s_off = s_off * CHAR_BIT + PUS_A_TM_DEST_ID_BITS;
	sz  = PUS_A_TM_TIME_BITS;
#else /* AP_PUS_VERSION == PUS_C_VERSION */
	s_off = PUS_ECSS_TIME_OFFSET * CHAR_BIT;
	sz  = PUS_C_TM_TIME_OCTETS * CHAR_BIT;
#endif /* AP_PUS_VERSION */

	/* round up source s_offset to find alignment in target array */
	d_off = ALIGN_MASK(sz, 0b111) - sz;
	bitcpy(ts, d_off, p, s_off, sz);

	return 0;
}


/**
 * @brief et the timestamp
 *
 * @param[in] ts a sufficiently large array of octets
 *
 * @note the number of octets required is derived from the user-specified
 *	 number of bits/bytes; if the number of bits (as possible in PUS A)
 *	 do not correspond to an integer multiple of octets, the unused bits
 *	 in the MSB position (low array index) are padded 0
 *
 * @returns < 0 on error
 */

int ecss_set_timestamp(uint8_t *pkt, const uint8_t *ts)
{
	uint8_t *p;

	size_t s_off;
	size_t d_off;
	size_t sz;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return -1;

	if (!ts)
		return -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TM)
		return -1;


	p = ccsds_get_payload(pkt);


	/* in PUS A, dest id is located either 0 or 1 octet after the SST field */
#if (AP_PUS_VERSION == PUS_A_VERSION)
	d_off = PUS_ECSS_SST_OFFSET + 1;
#if (PUS_A_TM_USE_PKT_SUB_CNTR != 0)
	d_off += 1;
#endif /* PUS_A_TM_USE_PKT_SUB_CNTR */
	/* convert to bits and add bit width of destination ID */
	d_off = d_off * CHAR_BIT + PUS_A_TM_DEST_ID_BITS;
	sz  = PUS_A_TM_TIME_BITS;
#else /* AP_PUS_VERSION == PUS_C_VERSION */
	d_off = PUS_ECSS_TIME_OFFSET * CHAR_BIT;
	sz  = PUS_C_TM_TIME_OCTETS * CHAR_BIT;
#endif /* AP_PUS_VERSION */

	/* round up source s_offset to find alignment  */
	s_off = ((sz + (CHAR_BIT - 1)) & (size_t)~(CHAR_BIT - 1)) - sz;
	bitcpy(p, d_off, (void *)ts, s_off, sz);

	return 0;
}


/**
 * @brief retrieve the (16-bit) source ID
 *
 * @note this also works for PUS A as long as the number of bits configured
 *	 are <= 16
 *	 for PUS A, the returned value will be masked to the number of bits
 *	 specified, anything else is the responsibility of the user
 *
 * @returns the ID or all bits set if not applicable
 */

uint16_t ecss_get_source_id(uint8_t *pkt)
{
	uint16_t d;
	uint8_t *p;

	size_t off = PUS_ECSS_SRCID_OFFSET;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return (uint16_t) -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TC)
		return (uint16_t) -1;


	p = ccsds_get_payload(pkt);

	d = ((uint16_t)p[off + 0] & 0xff) << 8;
	d |= (uint16_t)p[off + 1] & 0xff;

#if (AP_PUS_VERSION == PUS_A_VERSION)
	d &= (1 << PUS_A_TC_SOURCE_ID_BITS - 1);
#endif /* AP_PUS_VERSION */

	return d;
}


/**
 * @brief set the (16-bit) source ID
 *
 * @note this also works for PUS A as long as the number of bits configured
 *	 are <= 16
 *	 for PUS A, the supplied value will be masked to the number of bits
 *	 specified
 *
 * @returns < 0 on error
 */

int ecss_set_source_id(uint8_t *pkt, uint16_t src_id)
{
	uint8_t *p;

	size_t off = PUS_ECSS_SRCID_OFFSET;


	if (!ccsds_get_2nd_hdr_flag(pkt))
		return (uint16_t) -1;

	if (ccsds_get_pkt_type(pkt) != SPP_PKT_TYPE_IS_TC)
		return (uint16_t) -1;


	p = ccsds_get_payload(pkt);

#if (AP_PUS_VERSION == PUS_A_VERSION)
	src_id = src_id & (1 << PUS_A_TM_DEST_ID_BITS - 1);
#if (PUS_A_TM_DEST_ID_BITS > 8)
	src_id = src_id << (16 - PUS_A_TM_DEST_ID_BITS);
#endif
	p[off + 0] = (src_id >> 8) & 0xff;
#if (PUS_A_TM_DEST_ID_BITS > 8)
	/* no need to care about the spare bits, just overwrite the octet */
	p[off + 1] = src_id & 0xff;
#endif
#else	/* AP_PUS_VERSION == PUS_C_VERSION */

	p[off + 0] = (uint8_t)((src_id >> 8) & 0xff);
	p[off + 1] = (uint8_t)((src_id >> 0) & 0xff);

#endif	/* AP_PUS_VERSION */
	return 0;
}


/**
 * @brief get the secondary header size for a PUS packet
 *
 * @param tmtc  0: TM, 1:TC
 *
 * @returns the size of the secondary PUS header
 */

size_t ecss_get_pkt_2nd_hdr_size(int tmtc)
{
	size_t sz = 0;


	if (tmtc > 1)
		return 0;

	if (tmtc == SPP_PKT_TYPE_IS_TC) {
		sz += PUS_ECSS_SRCID_OFFSET * CHAR_BIT;
		sz += PUS_C_SRCID_BITS;
#if (AP_PUS_VERSION == PUS_A_VERSION)
		sz += PUS_A_TC_SOURCE_ID_BITS;
		sz += PUS_A_TC_SPARE_BITS;
#else /* AP_PUS_VERSION == PUS_C_VERSION */
		sz += PUS_C_TC_SPARE_BITS;
#endif
	} else {
#if (AP_PUS_VERSION == PUS_A_VERSION)

		sz += (PUS_ECSS_SST_OFFSET + 1) * CHAR_BIT;
#if (PUS_A_TM_USE_PKT_SUB_CNTR != 0)
		sz += CHAR_BIT;
#endif /* PUS_A_TM_USE_PKT_SUB_CNTR */
		sz += PUS_A_TM_DEST_ID_BITS;
		sz += PUS_A_TM_TIME_BITS;
		sz += PUS_A_TM_SPARE_BITS;
#else /* AP_PUS_VERSION == PUS_C_VERSION */
		sz += PUS_ECSS_TIME_OFFSET * CHAR_BIT;
		sz += PUS_C_TM_TIME_OCTETS * CHAR_BIT;
		sz += PUS_C_TM_SPARE_BITS;
#endif
	}

	/* make sure this is rounded to the next full octet */
	sz = ALIGN_MASK(sz, 0b111);
	sz /= CHAR_BIT;

	return sz;
}


/**
 * @brief get the header size for a PUS packet
 *
 * @param tmtc  0: TM, 1:TC
 *
 * @returns the size of the CCSDS header + the secondary
 *	    PUS header
 */

size_t ecss_get_pkt_hdr_size(int tmtc)
{
	size_t sz = SPP_PKT_HDR_LEN;


	if (tmtc > 1)
		return 0;


	sz += ecss_get_pkt_2nd_hdr_size(tmtc);

	return sz;
}


/**
 * @brief get the pointer to the ECSS packet's payload section
 *
 * @returns a pointer into the buffer to the first byte after
 *	   the secondary header section or NULL on error
 */

uint8_t *ecss_get_payload(uint8_t *pkt)
{
	size_t off;


	if (!pkt)
		return NULL;

	off = ecss_get_pkt_hdr_size(ccsds_get_pkt_type(pkt));
	if (!off)
		return NULL;

	return &pkt[off];
}


/**
 * @brief get offset to the ECSS packet's payload section
 *
 * @returns the offset to the payload in bits
 */

size_t ecss_get_payload_offset(uint8_t *pkt)
{
	size_t off;


	if (!pkt)
		return 0;

	off = ecss_get_pkt_hdr_size(ccsds_get_pkt_type(pkt));

	return off << 3;
}


/**
 * @brief check if pkt accept ack is set
 *
 * @return 0 if not set or error
 */

int ecss_get_pkt_accept_ack(uint8_t *pkt)
{
	uint8_t a;


	a = ecss_get_ack_flags(pkt);
	if (a == (uint8_t) -1)
		return 0;

	if (a & PUS_ACK_PKT_ACCEPT)
		return 1;

	return 0;
}


/**
 * @brief check if exec start ack is set
 *
 * @return 0 if not set or error
 */

int ecss_get_exec_start_ack(uint8_t *pkt)
{
	uint8_t a;


	a = ecss_get_ack_flags(pkt);
	if (a == (uint8_t) -1)
		return 0;

	if (a & PUS_ACK_EXEC_START)
		return 1;

	return 0;
}


/**
 * @brief check if exec progress ack is set
 *
 * @return 0 if not set or error
 */

int ecss_get_exec_progr_ack(uint8_t *pkt)
{
	uint8_t a;


	a = ecss_get_ack_flags(pkt);
	if (a == (uint8_t) -1)
		return 0;

	if (a & PUS_ACK_EXEC_PROGR)
		return 1;

	return 0;
}


/**
 * @brief check if exec completion ack is set
 *
 * @return 0 if not set or error
 */

int ecss_get_exec_compl_ack(uint8_t *pkt)
{
	uint8_t a;


	a = ecss_get_ack_flags(pkt);
	if (a == (uint8_t) -1)
		return 0;

	if (a & PUS_ACK_EXEC_COMPL)
		return 1;

	return 0;
}


/**
 * @brief get the total size of the packet, header and all
 */

size_t ecss_get_pkt_size(uint8_t *pkt)
{
	size_t sz;


	if (!ecss_is_pkt_valid(pkt, NULL))
		return 0;

	sz  = ccsds_get_hdr_size();
	sz += ccsds_get_data_len(pkt);

	return sz;
}


/**
 * @brief get the total size of the payload without the 2nd header
 */

size_t ecss_get_pld_size(uint8_t *pkt)
{
	size_t sz;


	if (!ecss_is_pkt_valid(pkt, NULL))
		return 0;

	sz = ccsds_get_data_len(pkt) - ecss_get_pkt_2nd_hdr_size(ccsds_get_pkt_type(pkt));

	return sz;
}


/**
 * @brief verify that buffer contains a CCSDS packet structure compliant to
 *	  ECSS-E-70-41A or ECSS-E-ST-70-41C (i.e. PUS A or C)
 *
 * @param check_crc set function to apply CRC check
 * @note CRC may not be present for all packets due to
 *	 tailoring as per 7.4.3.2d of ECSS-E-ST-70-41C
 *	 and 5.4.3 of ECSS-E-70-41A
 *	 CRC is only checked with the user-supplied function which should
 *	 either be standard CRC16 or ISO 16-bit checksum as per mission
 *	 tailoring (7.4.3.2e of ECSS-E-ST-70-41C)
 *
 *
 */

int ecss_is_pkt_valid(uint8_t *pkt, crc_handler_t crc __attribute__((unused)))
{
	/* only CCSDS version 1 packets are supported */
	if (ccsds_get_pkt_version(pkt))
		goto error;


	if (ccsds_get_2nd_hdr_flag(pkt)) {
		if (!ecss_pus_version_valid(ecss_get_pkt_version(pkt)))
			goto error;
	}

#if 0
	if (crc)
		crc(pkt, ccsds_get_data_len(pkt) + ccsds_get_hdr_size() - PUS_ECSS_CRC_LEN);
#endif

	return 1;
error:
	return 0;
}


/**
 * @brief get the maximum payload size for a packet given the configuration
 *
 * @param tmtc  0: TM, 1:TC
 *
 * @returns the maximum payload size
 *
 * @note the returned value is the "informative" payload size, i.e. the CRC bytes are
 *	 subtracted as we consider part of the packet instead of the payload
 *	 consider this in case a particular packet type does not use CRC
 */

size_t eccs_get_max_pld_size(int tmtc)
{
	return AP_PKT_SIZE_MAX - ecss_get_pkt_2nd_hdr_size(tmtc) - PUS_ECSS_CRC_LEN;

}
