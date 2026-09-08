/**
 * @brief provides functions for manipulating TMTC packets compliant to the
 *	 CCSDS Space Packet Protocol; if in doubt, consult CSDS 133.0-B-2
 */

#ifndef CCSDS_PKT_H
#define CCSDS_PKT_H

#include <stdint.h>
#include <stddef.h>


#define SPP_PKT_HDR_LEN		6

/* version number CCSDS 133.0-B-2 4.1.3.2 */
#define SPP_PKT_VER_OFFSET	0x00
#define SPP_PKT_VER_SHIFT	0x05
#define SPP_PKT_VER_MASK	0xe0

/* packet type CCSDS 133.0-B-2 4.1.3.3.2 */
#define SPP_PKT_TYPE_OFFSET	0x00
#define SPP_PKT_TYPE_SHIFT	0x04
#define SPP_PKT_TYPE_MASK	0x10
#define SPP_PKT_TYPE_IS_TM	0x00
#define SPP_PKT_TYPE_IS_TC	0x01

/* packet type CCSDS 133.0-B-2 4.1.3.3.3 */
#define SPP_2ND_HDR_FLAG_OFFSET	0x00
#define SPP_2ND_HDR_FLAG_SHIFT	0x03
#define SPP_2ND_HDR_FLAG_MASK	0x0f

/* application id CCSDS 133.0-B-2 4.1.3.3.4 */
#define SPP_APID_OFFSET_1	0x00
#define SPP_APID_OFFSET_2	0x01
#define SPP_APID_OFFSET_1_MASK	0x07

/* sequence flags CCSDS 133.0-B-2 4.1.3.4.2 */
#define SPP_SEQ_FLAGS_OFFSET	0x02
#define SPP_SEQ_FLAGS_SHIFT	0x06
#define SPP_SEQ_FLAGS_MASK	0xc0

#define SPP_SEQ_FLAG_CONT_SEG	0x00
#define SPP_SEQ_FLAG_FRST_SEG	0x01
#define SPP_SEQ_FLAG_LAST_SEG	0x02
#define SPP_SEQ_FLAG_UNSEG	0x03

/* packet sequence count or packet name CCSDS 133.0-B-2 4.1.3.4.3 */
#define SPP_SEQ_CNT_OFFSET_1		0x02
#define SPP_SEQ_CNT_OFFSET_2		0x03
#define SPP_SEQ_CNT_OFFSET_1_MASK	0x3f

/* packet size fields CCSDS 133.0-B-2 4.1.3.5 */
#define SPP_LEN_LO_OFFSET       0x05
#define SPP_LEN_HI_OFFSET       0x04

size_t ccsds_get_hdr_size(void);

uint8_t *ccsds_get_payload(uint8_t *pkt);

uint8_t ccsds_get_pkt_version(uint8_t *pkt);
uint8_t ccsds_get_pkt_type(uint8_t *pkt);
uint8_t ccsds_get_2nd_hdr_flag(uint8_t *pkt);
uint16_t ccsds_get_apid(uint8_t *pkt);
uint8_t ccsds_get_seq_flags(uint8_t *pkt);
uint16_t ccsds_get_seq_cnt(uint8_t *pkt);
uint16_t ccsds_get_data_len(uint8_t *pkt);

void ccsds_set_pkt_version(uint8_t *pkt, uint8_t v);
void ccsds_set_pkt_type(uint8_t *pkt, uint8_t t);
void ccsds_set_2nd_hdr_flag(uint8_t *pkt, uint8_t t);
void ccsds_set_apid(uint8_t *pkt, uint16_t a);
void ccsds_set_seq_flags(uint8_t *pkt, uint8_t t);
void ccsds_set_seq_cnt(uint8_t *pkt, uint16_t c);
void ccsds_set_data_len(uint8_t *pkt, uint16_t len);

#endif /* CCSDS_PKT_H */
