#ifndef STORE_H
#define STORE_H

/* フラッシュ保存。Classic 鍵は SDK が自動保存。ここは番地・色・取込のみ。 */

#include <stdbool.h>
#include <stdint.h>

#include "btstack.h"
#include "cap.h"

#ifdef __cplusplus
extern "C" {
#endif

void store_host(bd_addr_t addr);
bool store_host_load(void);
void store_wired(bool en);
bool store_wired_load(void);
void store_color(void);
void store_color_load(void);
bool store_cap_save(void);
bool store_cap_load(void);
void store_cap_forget(void);

#ifdef __cplusplus
}
#endif

#endif
