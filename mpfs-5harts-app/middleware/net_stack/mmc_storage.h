#ifndef MMC_STORAGE_H
#define MMC_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MMC_SECTOR_SIZE         512U
/* Store metadata at 10 MiB offset (sector 20480) and file payload starting at sector 20481 */
#define MMC_META_SECTOR         20480U
#define MMC_DATA_START_SECTOR   20481U
#define MMC_MAX_FILE_BYTES      (16U * 1024U * 1024U)   /* 16 MiB max transfer */

bool     mmc_storage_init(void);
bool     mmc_storage_is_ready(void);
void     mmc_storage_print_info(void);
void     mmc_storage_run_selftest(void);



bool     mmc_storage_init_media(bool use_emmc);


/* Streaming TFTP file write/read APIs */
bool     mmc_file_open_write(const char *filename);
int      mmc_file_write_chunk(const uint8_t *data, uint16_t len);
void     mmc_file_close_write(void);

bool     mmc_file_open_read(const char *filename, uint32_t *out_size);
int      mmc_file_read_chunk(uint8_t *buf, uint32_t max_len, uint32_t offset);

#endif /* MMC_STORAGE_H */