#ifndef SPI_H
#define SPI_H

/* Pro Controller SPI フラッシュの中身。実機記録・資料の写し(仕様値)。
 * 出典: nxbt pairing session / dekuNukem / CTCaer/jc_toolkit#28。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t addr;
    uint8_t size;
    const uint8_t *data;
} spi_entry_t;

extern const spi_entry_t SPI_TABLE[];
extern uint8_t SPI_TABLE_N;
extern uint8_t spi_color_6050[13];  /* 本体/Btn/L/R + 不明1B。C…O 行で書換 */

const spi_entry_t *spi_find(uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif
