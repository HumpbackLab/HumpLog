#ifndef OPENLOG_H
#define OPENLOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void openlog_init(void);
void openlog_process(void);
uint8_t openlog_record_active(void);

#ifdef __cplusplus
}
#endif

#endif
