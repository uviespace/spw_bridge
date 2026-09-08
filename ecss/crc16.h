#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

/* byte-per-byte variant */
void crc_init_lookup_table(void);
uint16_t crc16(uint8_t b, uint16_t crc);
uint16_t crc16_buf(uint8_t *buf, size_t len);


/* sliced variants */
void crc16_init_sliced_lookup_table(void);
uint16_t crc16_slice2(const void *buf, size_t len);
uint16_t crc16_slice4(const void *buf, size_t len);
uint16_t crc16_slice8(const void *buf, size_t len);

#endif /* CRC16_H */
