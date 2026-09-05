#ifndef WAKECON_TYPE_H
#define WAKECON_TYPE_H

/* エミュレートするコントローラの種類。
 * 切替えは T コマンドで行い、Flash に保存、再起動で反映する。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAKECON_TYPE_PRO 0u
#define WAKECON_TYPE_JCL 1u
#define WAKECON_TYPE_JCR 2u

/* 起動時に呼ぶ。保存を読んで表示を整える。 */
void type_boot(void);

/* 現在の種類。 */
uint8_t type_get(void);
bool type_is_pro(void);
bool type_is_jcl(void);
bool type_is_jcr(void);

/* 種類別の識別値。 */
uint16_t type_product_id(void);   /* SDP / USB の PID */
uint8_t type_device_byte(void);   /* 0x02 応答の種別 (1=L 2=R 3=Pro) */
const char *type_gap_name(void);  /* 無線で名乗る名前 */
const char *type_short_name(void); /* 表示用 (pro/jcl/jcr) */

/* 種類を保存して再起動する。T コマンドから呼ぶ。戻らない。 */
void type_request_switch(uint8_t want);

#ifdef __cplusplus
}
#endif

#endif
