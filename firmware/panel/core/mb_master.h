/* mb_master.h - Modbus RTU master frame builder / response parser (portable). */
#ifndef MB_MASTER_H
#define MB_MASTER_H
#include <stdint.h>
#include <stdbool.h>

#define MBM_MAX 128u
enum { MBM_OK = 0, MBM_ERR_CRC = -100, MBM_ERR_FORMAT = -101 };   /* > 0: Modbus exception code (negated) */

uint16_t mbm_crc16(const uint8_t *p, uint16_t n);
uint16_t mbm_read(uint8_t *f, uint8_t addr, uint8_t fc, uint16_t start, uint16_t cnt);   /* fc 3 or 4 */
uint16_t mbm_write1(uint8_t *f, uint8_t addr, uint16_t reg, uint16_t val);
uint16_t mbm_writen(uint8_t *f, uint8_t addr, uint16_t start, const uint16_t *v, uint16_t cnt);
/* expected response length for a request (for the link timeout) */
uint16_t mbm_resp_len(const uint8_t *req);
/* parse rsp for req; registers of a read go to out[]. returns MBM_OK, -exception or MBM_ERR_* */
int mbm_parse(const uint8_t *req, const uint8_t *rsp, uint16_t n, uint16_t *out);
#endif
