#ifndef FLASH_H
#define FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "main.h"

/* Vung Flash luu du lieu. Mac dinh: 2 KB cuoi cua STM32F103C8 (64 KB).
 * Phai giam LENGTH cua FLASH trong file linker (.ld) tu 64K xuong 62K
 * de code chuong trinh khong bao gio nam vao vung nay.
 * Neu dung chip khac, dinh nghia lai 2 macro nay truoc khi include. */
#ifndef FLASH_STORE_ADDR
#define FLASH_STORE_ADDR   0x0800F800u
#endif
#ifndef FLASH_STORE_SIZE
#define FLASH_STORE_SIZE   2048u          /* phai la boi so cua FLASH_PAGE_SIZE */
#endif

/* Doc Flash -> nap vao card_db va danh sach admin.
 * Tra false neu Flash trong hoac du lieu hong (sai magic/version/CRC);
 * khi do card_db va admin KHONG bi thay doi. */
bool flash_store_load(void);

/* Ghi card_db + danh sach admin xuong Flash.
 * Neu du lieu khong doi so voi ban dang luu thi bo qua (do mon Flash). */
bool flash_store_save(void);

/* Xoa sach vung luu tru */
bool flash_store_erase(void);

/* Flash co du lieu hop le khong */
bool flash_store_has_data(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_STORE_H */