#ifndef HUMPLOG_H
#define HUMPLOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void humplog_init(void);
void humplog_process(void);
uint8_t humplog_record_active(void);

#ifdef __cplusplus
}
#endif

#endif
