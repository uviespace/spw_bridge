/**
 * @brief provides functions for manipulating ECSS compliant TMTC packets
 * @note this provides extra functionality on top of the CCSDS base functions
 *
 * if in doubt, consult:
 *	 ECSS-E-ST-70-41C
 * and optionally:
 *	CSDS 133.0-B-2
 *	ECSS-E-70-41A
 */
#ifndef ECSS_PKT_H
#define ECSS_PKT_H


#include <ccsds_pkt.h>
#include <crc16.h>

#include <ecss_pkt_cfg.h>	/* user configurations */



/* PUS version: ECSS-E-ST-70-41C 7.4.3 (TM), 7.4.4 (TC) */
#define PUS_ECSS_VER_OFFSET	0x00
#define PUS_ECSS_VER_MASK	0xf0
#define PUS_ECSS_VER_SHIFT	0x04
#define PUS_ECSS_TREF_OFFSET	0x00
#define PUS_ECSS_TREF_MASK	0x0f
#define PUS_ECSS_TREF_SHIFT	0x00

#define PUS_ESA_VERSION		0x00	/* ESA PSS-07-101, not supported */
#define PUS_A_VERSION		0x01	/* ECSS-E-70-41A */
#define PUS_C_VERSION		0x02	/* ECSS-E-ST-70-41C 7.4.3.1.c, 7.4.4.1.c */

/* ACK flags: ECSS-E-ST-70-41C 7.4.4.1.d */
#define PUS_ECSS_ACK_OFFSET	0x00
#define PUS_ECSS_ACK_MASK	0x0f
#define PUS_ECSS_ACK_SHIFT	0x00

#define PUS_ACK_PKT_ACCEPT     0b0001
#define PUS_ACK_EXEC_START     0b0010
#define PUS_ACK_EXEC_PROGR     0b0100
#define PUS_ACK_EXEC_COMPL     0b1000

/* message type id: ECSS-E-ST-70-41C 7.4.3 (TM), 7.4.4.1 (TC), 5.3.3 */
#define PUS_ECSS_ST_OFFSET	0x01
#define PUS_ECSS_SST_OFFSET	0x02

/* message type counter: applies to ECSS-E-ST-70-41C 7.4.3.1 only */
#define PUS_ECSS_TCNT_OFFSET	0x03	/* 2 octets wide */
/* destination ID: applies to ECSS-E-ST-70-41C 7.4.3.1 only */
#define PUS_ECSS_DESTID_OFFSET	0x05	/* 2 octets wide */
/* time: applies to ECSS-E-ST-70-41C 7.4.3.1 only */
#define PUS_ECSS_TIME_OFFSET	0x07

/* source ID: ECSS-E-ST-70-41C 7.4.4, ECSS-E-70-41A 5.3.3 */
#define PUS_ECSS_SRCID_OFFSET	0x03
/* source ID bits: ECSS-E-ST-70-41C 7.4.4.1 (PUS C only!) */
#define PUS_C_SRCID_BITS	(2 * CHAR_BIT)




/* ECSS-E-ST-70-41C, if CRC is used, it is 2 bytes long 7.4.3.2.e (TM) 7.4.4.2.d (TC) */
#define PUS_ECSS_CRC_LEN	2

/* the crc checksum function type */
typedef uint16_t (*crc_handler_t)(const void *, size_t);


uint8_t ecss_get_pkt_version(uint8_t *pkt);
uint8_t ecss_get_sc_time_ref_status(uint8_t *pkt);
uint8_t ecss_get_ack_flags(uint8_t *pkt);
uint8_t ecss_get_service_type(uint8_t *pkt);
uint8_t ecss_get_sub_service_type(uint8_t *pkt);
uint16_t ecss_get_msg_type_cntr(uint8_t *pkt);
uint16_t ecss_get_destination_id(uint8_t *pkt);
int ecss_get_timestamp(uint8_t *pkt, uint8_t *ts);
uint16_t ecss_get_source_id(uint8_t *pkt);


int ecss_set_pkt_version(uint8_t *pkt, uint8_t v);
int ecss_set_sc_time_ref_status(uint8_t *pkt, uint8_t ref);
int ecss_set_ack_flags(uint8_t *pkt, uint8_t f);
int ecss_set_service_type(uint8_t *pkt, uint8_t st);
int ecss_set_sub_service_type(uint8_t *pkt, uint8_t sst);
int ecss_set_msg_type_cntr(uint8_t *pkt, uint16_t tcntr);
int ecss_set_destination_id(uint8_t *pkt, uint16_t dest_id);
int ecss_set_timestamp(uint8_t *pkt, const uint8_t *ts);
int ecss_set_source_id(uint8_t *pkt, uint16_t src_id);



/**
 * convenience functions
 * note: some of these serve the user at the TC receiver endpoint, so
 * these mostly correspond to _get_()
 */

size_t ecss_get_pkt_hdr_size(int tmtc);
size_t ecss_get_pkt_2nd_hdr_size(int tmtc);
size_t ecss_get_pld_size(uint8_t *pkt);
uint8_t *ecss_get_payload(uint8_t *pkt);
size_t ecss_get_payload_offset(uint8_t *pkt);

int ecss_get_pkt_accept_ack(uint8_t *pkt);
int ecss_get_exec_start_ack(uint8_t *pkt);
int ecss_get_exec_progr_ack(uint8_t *pkt);
int ecss_get_exec_compl_ack(uint8_t *pkt);
size_t ecss_get_pkt_size(uint8_t *pkt);
int ecss_is_pkt_valid(uint8_t *pkt, crc_handler_t crc);
size_t eccs_get_max_pld_size(int tmtc);



/* max pkt size is CCSDS header + 16 bit packet size field */
#if (AP_PKT_SIZE_MAX > (SPP_PKT_HDR_LEN + (1 << 16)))
#define PUS_SIZE_MAX 65542
#warning "Specified packet size larger than legal value, clamping to 65542"
#endif /* AP_PKT_SIZE_MAX */

#if (AP_PUS_VERSION == PUS_A_VERSION)
	/* absolute minimum is CCSDS_HDR + secondary PUS A header
	 * (ECSS-E-70-41A, 5.3.3 and 5.4.3)
	 */
	#if (AP_PKT_SIZE_MAX < (SPP_PKT_HDR_LEN + 3))
	#error "Specified packet size is smaller than the absolute lower limit for PUS A"
	#endif /* AP_PKT_SIZE_MAX */

	/* formally, there is no limit to the size of src/dest id fields, but
	 * we limit this to 16 bits so we can present a unified interface
	 * which favours PUS C since we support PUS A only for legacy reasons
	 * (and to test the library against an existing mission implementation)
	 */
	#if (PUS_A_TC_SOURCE_ID_BITS > 16)
	#error "TC source field in secondary header may only be 16 bits in this implementation"
	#endif /* PUS_A_TM_DEST_ID_BITS */
	#if (PUS_A_TM_DEST_ID_BITS > 16)
	#error "TM destination field in secondary header may only be 16 bits in this implementation"
	#endif /* PUS_A_TM_DEST_ID_BITS */


#elif (AP_PUS_VERSION == PUS_C_VERSION)
	/* absolute minimum is CCSDS_HDR + secondary PUS C TM header (7.4.3.1)
	 * with 1 octet for SCOS-2000 PTC=9, PFC=3 CUC timestamp
	 */
	#if (AP_PKT_SIZE_MAX < (SPP_PKT_HDR_LEN + 8))
	#error "Specified packet size is smaller than the absolute lower limit for PUS C"
	#endif /* AP_PKT_SIZE_MAX */
#else
#error "Unsupported PUS version number"
#endif /* AP_PUS_VERSION */

/**
 * we encode our types in a 32 bit integers for size efficiency:
 *
 * 2 bits are used to encode a base type:
 *	- integer
 *	- float
 *	- bitfield
 *	- octet strings (== byte arrays)
 *
 * 2 bits for size:
 *	- impies 2^n for number of bytes, ie.
 *		0x0 == 1,
 *		0x1 == 2,
 *		0x2 == 4
 *		0x3 == 8
 *	- multiples of octets for integers and floats
 *	- ignored for bit fields (see mask bits)
 *	- this field may be used to assert the input sizes of data for a
 *	  particular item in user code
 *	- for integer types, endianness conversion is performed based on
 *	  the width of the type (8 byte - 64 bit int max)
 *	- OCTET STRINGS: first part of position reference; see mask bits below
 *
 *	XXX OCTET STRINGS: implement
 *
 * 2 bits are used to encode the position reference for the bit offset field
 *	- 0x0 indicates an absolute offset
 *	- 0x1 indicates a relative offset within a group
 *	- 0x2 indicates a relative enumerated position within the variable
 *	  items or groups, with the bit offset used to indicate the relative
 *	  position in ascending order (0, 1, 2, ...)
 *	  => these packets use a reference to the parent type of the particular
 *	     group in a separate field
 *	- 0x3 is special in that it defines a "deduced" field type which requires
 *	  the preceding field to be a numeric ID which is used to resolve and
 *	  set/retrieve the associated value in/from an external source
 *	  note: we use the base type to indicate the source, which must be
 *	  of base type INT, FLOAT or BITFIELD since OCTET is special.
 *	  This allows us to define up to 3 sources for resolving deduced types.
 *	  Currently, we only require one which is the _datapool_ implementation,
 *	  and uses the INT type. The other two remain available for use later.
 *	- OCTET STRINGS: second part of position reference; see mask bits below
 *
 * 7 bits for number of mask bits (implies mask bit value + 1):
 *	- 0x3f => full-width 64 bit natural type
 *	- 0x1f => full-width 32 bit natural type
 *	- 0x0f => full-width 16 bit natural type
 *	- 0x07 => full-width  8 bit natural type
 *	- FIELD IS IGNORED FOR FLOAT TYPES!
 *	- the mask is computed as follows: unsigned [(1 << (mask_bits + 1) - 1]
 *	- mask determines the actual size of a bitfield type
 *	  => since SCOS-2000 does not support bit strings, the largest possible
 *	     non-group type is PTC=9, PFC=2 (absolute time CDS format with µs)
 *	     as per S2K-MCS-ICD-0001-TOS-GC, thus requiring 64 (== 0x3f) mask
 *	     bits for any bitfield type at most; since we have one spare byte
 *	     at the moment, we'll leave this at 128 (0x7f) mask bits
 *	 - bitfield types are treated as a arrays of octets with the array
 *	   size rounded to the next integer at the user interface, but
 *	   are treated as the precise size within the bitstream of the packet
 *	   payload. E.g. a bitfield of 55 bits requires an array size supplied
 *	   by the user of int(55 bits / 8 + 1) = 7 elements
 *
 *	- the following rules apply when placing/extracting maskeable values:
 *	  => bit fields based on natural types are defined by the number of
 *	     valid bits in big endian order from LSB, i.e. a natural 16 bit
 *	     value of 0xabcd with a shift mask value of 8 becomes 0x1cd
 *	     (AND masked with unsigned ((1 << (8 + 1) - 1))
 *	  => shift masks for bitfields are applied analogously with the
 *	     considered the LSB of the byte of the greatest offset,
 *	     i.e. for a bitfield with a size of 9, with the underlying array
 *	     being 2 bytes long and initialised to {0xc3, 0x2f} a mask of
 *	     (1 << (9 + 1) - 1) = 0x3ff will be set as {0x03, 0x2f) in the
 *	     packet bitstream
 *
 *	- OCTET STRINGS: in case of octet strings, this field together with
 *			 the position reference and the size field identify
 *			 the RELATIVE ENUMERATED POSITION with regards to the
 *			 parent
 *		=> octet string ARE ALWAYS PARENT-RELATIVE!
 *		-> since this forms an 11-bit field, the enumeration limit is
 *		   2048 positions max. Hence, at most 2048 individual
 *		   octet string fields can follow a single parent, i.e.
 *		   2048 fields of 32 bytes (ignoring the secondary header) each
 *		   can fit into a single CCSDS packet which contains no other
 *		   fields.
 *
 * 19 bits to encode the bit offset within a packet
 *	- this accounts for every bit within the possible 2^16-1 bytes of the
 *	  payload
 *	- OCTET STRINGS: this is field is used as the number of elements in the
 *			 array-like type
 *		=> a value of 0 shall indicate a deduced (variable) sized
 *		   octet string
 */


#define FIELD_TYPE_IS_INT	0	/* native integer type */
#define FIELD_TYPE_IS_FLOAT	1	/* native float type */
#define FIELD_TYPE_IS_BITS	2	/* bitfield */
#define FIELD_TYPE_IS_OCT_STR	3	/* octet string (i.e. byte array) of arbitrary size */

#define FIELD_SRC_DATAPOOL	0	/* look up enum as datapool id; the mask size is always the size of the datapool ID enums */
#define FIELD_SRC_PKT		1	/* look up enum as derived packet source (i.e. discriminant); mask size is >0 <33 */
#define FIELD_SRC_UNUSED_1	2
#define FIELD_SRC_UNUSED_2	3


#define FIELD_REF_ABSOLUTE	0	/* absolute bit offset from start of payload storage (== toplevel group) */
#define FIELD_REF_GROUPREL	1	/* relative bit offset from start of parent group */
#define FIELD_REF_ENUMERATE	2	/* enumerated offset within parent group; this is not an enumeration in the sense of pus/scos2000 */
#define FIELD_REF_DEDUCED	3	/* deduced type; this is a deduced (enumerated) type in the sense of scos2000 PTC==2
					 * NOTE: field refs of this type use the type field to determine the lookup source
					 */

/* t: type field of pkt_field_entry */
#define FIELD_TYPE_GET(t)		(((t) >> 30) & 0x00000003)
#define FIELD_TYPE_SIZE(t)		(1 << (((t) >> 28) & 0x00000003))
#define FIELD_REF_TYPE(t)		(((t) >> 26) & 0x00000003)
#define FIELD_MASK_BITS(t)		(((t) >> 19) & 0x0000007f)
#define FIELD_BITS_OFFSET(t)		(((t) >>  0) & 0x0007ffff)


#define FIELD_UNUSED	0xffffffffUL

/* Bit Twiddling Hacks for (int)ceil(log_2((uint32_t)x)), ILOG2(0) = 0 */
#define ILOG2(x) ((x) > 1 ? 32 - __builtin_clz((x) - 1) : 0)

#define FIELD_DEFINE(type, size, ref, mask, offset) \
	((((type)	& 0x00000003) << 30) | \
	 ((ILOG2(size)	& 0x00000003) << 28) | \
	 (((ref)	& 0x00000003) << 26) | \
	 (((mask)	& 0x0000007f) << 19) | \
	 ((offset)	& 0x0007ffff))

/* common types with absolute offsets */
#define FIELD_ABS_INT8(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,			\
					     sizeof(int8_t),			\
					     FIELD_REF_ABSOLUTE,		\
					     (CHAR_BIT * sizeof(int8_t) - 1),	\
					     (off))
#define FIELD_ABS_INT16(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,			\
					     sizeof(int16_t),			\
					     FIELD_REF_ABSOLUTE,		\
					     (CHAR_BIT * sizeof(int16_t) - 1), \
					     (off))
#define FIELD_ABS_INT32(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,			\
					     sizeof(int32_t),			\
					     FIELD_REF_ABSOLUTE,		\
					     (CHAR_BIT * sizeof(int32_t) - 1),	\
					     (off))
#define FIELD_ABS_INT64(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,			\
					     sizeof(int64_t),			\
					     FIELD_REF_ABSOLUTE,		\
					     (CHAR_BIT * sizeof(int64_t) - 1),	\
					     (off))
#define FIELD_ABS_FLOAT(off)	FIELD_DEFINE(FIELD_TYPE_IS_FLOAT,		\
					     sizeof(float),			\
					     FIELD_REF_ABSOLUTE,		\
					     (CHAR_BIT * sizeof(float) - 1),	\
					     (off))
#define FIELD_ABS_DOUBLE(off)	FIELD_DEFINE(FIELD_TYPE_IS_FLOAT,		\
					     sizeof(double),			\
					     FIELD_REF_ABSOLUTE,		\
					     (CHAR_BIT * sizeof(double) - 1),	\
					     (off))


/* common types with group relative offsets */
#define FIELD_GREL_INT8(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int8_t),		\
					     FIELD_REF_GROUPREL,	\
					     (8 * sizeof(int8_t) - 1),	\
					     (off))
#define FIELD_GREL_INT16(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int16_t),		\
					     FIELD_REF_GROUPREL,	\
					     (8 * sizeof(int16_t) - 1), \
					     (off))
#define FIELD_GREL_INT32(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int32_t),		\
					     FIELD_REF_GROUPREL,	\
					     (8 * sizeof(int32_t) - 1),	\
					     (off))
#define FIELD_GREL_INT64(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int64_t),		\
					     FIELD_REF_GROUPREL,	\
					     (8 * sizeof(int64_t) - 1),	\
					     (off))
#define FIELD_GREL_FLOAT(off)	FIELD_DEFINE(FIELD_TYPE_IS_FLOAT,	\
					     sizeof(float),		\
					     FIELD_REF_GROUPREL,	\
					     (8 * sizeof(float) - 1),	\
					     (off))
#define FIELD_GREL_DOUBLE(off)	FIELD_DEFINE(FIELD_TYPE_IS_FLOAT,	\
					     sizeof(double),		\
					     FIELD_REF_GROUPREL,	\
					     (8 * sizeof(double) - 1),	\
					     (off))

/* common types with enumerated offsets */
#define FIELD_ENUM_INT8(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int8_t),		\
					     FIELD_REF_ENUMERATE,	\
					     (8 * sizeof(int8_t) - 1),	\
					     (off))
#define FIELD_ENUM_INT16(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int16_t),		\
					     FIELD_REF_ENUMERATE,	\
					     (8 * sizeof(int16_t) - 1), \
					     (off))
#define FIELD_ENUM_INT32(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int32_t),		\
					     FIELD_REF_ENUMERATE,	\
					     (8 * sizeof(int32_t) - 1),	\
					     (off))
#define FIELD_ENUM_INT64(off)	FIELD_DEFINE(FIELD_TYPE_IS_INT,		\
					     sizeof(int64_t),		\
					     FIELD_REF_ENUMERATE,	\
					     (8 * sizeof(int64_t) - 1),	\
					     (off))
#define FIELD_ENUM_FLOAT(off)	FIELD_DEFINE(FIELD_TYPE_IS_FLOAT,	\
					     sizeof(float),		\
					     FIELD_REF_ENUMERATE,	\
					     (8 * sizeof(float) - 1),	\
					     (off))
#define FIELD_ENUM_DOUBLE(off)	FIELD_DEFINE(FIELD_TYPE_IS_FLOAT,	\
					     sizeof(double),		\
					     FIELD_REF_ENUMERATE,	\
					     (8 * sizeof(double) - 1),	\
					     (off))

/* deduced field type(s)
 * - data pool size definition is always the size of the DP enums; note that
 *   the lookup is implicitly, so there's no need to actually set the octet and
 *   mask sizes here
 * - packet enumerations are always 4 octets as per scos2000 PTC==2 requirement
 *   N mask bits are as per definition of the field
 */
#define FIELD_DEDUCED_DATAPOOL	FIELD_DEFINE(FIELD_SRC_DATAPOOL,	\
					     0,				\
					     FIELD_REF_DEDUCED,		\
					     (8 * sizeof(int32_t) - 1),	\
					     0)
#define FIELD_DEDUCED_PKT(n, off) FIELD_DEFINE(FIELD_SRC_PKT,		\
					     sizeof(int32_t),		\
					     FIELD_REF_DEDUCED,		\
					     ((n) - 1),			\
					     off)



/* */
#define FIELD_IS_ABS(entry)		(FIELD_REF_TYPE((entry).type) == FIELD_REF_ABSOLUTE)
#define FIELD_IS_GREL(entry)		(FIELD_REF_TYPE((entry).type) == FIELD_REF_GROUPREL)
#define FIELD_IS_ENUM(entry)		(FIELD_REF_TYPE((entry).type) == FIELD_REF_ENUMERATE)
#define FIELD_IS_UNUSED(entry)		(entry.type == FIELD_UNUSED)
#define FIELD_IS_DED_POOL(entry)	((FIELD_TYPE_GET((entry).type) == FIELD_SRC_DATAPOOL) && \
					 (FIELD_REF_TYPE((entry).type) == FIELD_REF_DEDUCED))
#define FIELD_IS_DED_PKT(entry)		((FIELD_TYPE_GET((entry).type) == FIELD_SRC_PKT) && \
					 (FIELD_REF_TYPE((entry).type) == FIELD_REF_DEDUCED))
/* add more if needed */
#define FIELD_IS_DEDUCED(entry) (FIELD_IS_DED_POOL(entry) || FIELD_IS_DED_PKT(entry))


struct pkt_field_entry {
	uint8_t			st;	/* service type */
	uint8_t			sst;	/* subservice type */
	enum pkt_field_id	id;	/* unique identifier */
	enum pkt_field_id	parent;	/* parent id */
	uint32_t		type;	/* type code */
};


/* note: discriminant and ID are size_t for now, so we are guaranteed any
 * enum value will fit
 */
struct pkt_disc_field_entry {
	uint8_t			st;		/* service type */
	uint8_t			sst;		/* subservice type */
	size_t			disc;		/* parent discriminant id */
	size_t			id;		/* unique field id */
	uint32_t		type;		/* type code */
};

#if 0
struct service {
	uint8_t			st;	/* service type */
	uint8_t			sst;	/* subservice type */
	size_t			n_par;	/* number of field parameters in this service */
	struct pkt_field_entry	**fields;
};
#endif

#if 0

/* We cannot pre-bake, but we can define a recipe for building certain
 * packets. There are two distinct options how to define these:
 *
 *  1) fully-defined, including source item address
 *	- useful for custom HK and similar transfers
 *	- define packet and items once, then call bake_pkt() to generate
 *	  the packet including its contents
 *  2) semi-defined, using a serialised collection of the relevant entries from
 *     the pkt_field table
 *	- useful for TM/TC with changing parameters, e.g. events
 *	- define once, set items by their unique identifier
 *
 * semi-defined packets could be made to partially auto-fill. If the source
 * address of the field was NULL, the field is left unset until explicitly
 * written by the user via its reference id; same can be done to partially
 * or temporarily overwriting fully-defined packets. They are really only a
 * variant of each other.
 *
 * To properly access the fields, we need at least the following information:
 * ID, type, and size; enumeration in case of dynamic groups, the absolute offset,
 * i.e. we would not have to look up or compute the offset for the particular
 * item once the recipe has been defined.
 *
 * Note: enumerated items here refer to the group elements (in the sense of
 * array indices) of dynamic groups or subgroups; in case of dynamically placed
 * "enumerated" items as defined in the pkt_field_entry tables, we compute the
 * actual position once and the just enable insertion at the proper location.
 *
 * In case of groups and subgroups, the responsibility will be upon the user
 * to track the proper offsets. This is needed because referring to items
 * by their group and subgroup index (e.g. [1][0], [0][1][3]) would defeat the
 * purpose of the serialisation of the packet recipe and make the interface
 * rather clunky, since we would have to walk an arbitrary number of nested connections
 * every time.
 * Instead, indices 0...n of any (sub-)group type will refer to the
 * entry in serialised form;
 * E.g. if there is a group of N=3 with subgroups of M=2 each,
 * the entries of M will be referred to by the indices 0...5 for any ID of
 * type subgroup M; For this purpose, we need an extra array type within our
 * recipe items which stores the offsets for the individual items
 */

struct pkt_item {
	enum pkt_field_id	id;	/* unique identifier */
	enum pkt_field_id	parent;	/* parent id */
	uint32_t type;

}

struct pkt_recipe {
	size_t n_elem;
	struct pkt_item items[];
}
#endif


#define ALIGN_MASK(x, mask)    (((x) + (mask)) & (size_t)~(mask))


#endif /* CCSDS_PKT_H */
