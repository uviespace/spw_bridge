/**
 * @brief manipulate bit streams, i.e. memcpy, but on bits
 */

#ifndef BITMAN_H
#define BITMAN_H

int bitcpy(void *dst, size_t d_off, void *src, size_t s_off, size_t nbits);

#endif /* BITMAN_H */
