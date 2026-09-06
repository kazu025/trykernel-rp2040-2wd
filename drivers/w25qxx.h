#ifndef W25QXX_H
#define W25QXX_H

#include <trykernel.h>

typedef struct {
    UB manufacturer_id;
    UB memory_type;
    UB capacity_id;
} w25qxx_jedec_id_t;

#define W25QXX_STATUS1_BUSY  (1U << 0)
#define W25QXX_STATUS1_WEL   (1U << 1)
#define W25QXX_STATUS1_BP0   (1U << 2)
#define W25QXX_STATUS1_BP1   (1U << 3)
#define W25QXX_STATUS1_BP2   (1U << 4)
#define W25QXX_STATUS1_TB    (1U << 5)
#define W25QXX_STATUS1_SEC   (1U << 6)
#define W25QXX_STATUS1_SRP0  (1U << 7)
#define W25QXX_SECTOR_SIZE    4096U
#define W25QXX_PAGE_SIZE      256U

void w25qxx_init(void);
BOOL w25qxx_read_jedec_id(w25qxx_jedec_id_t *jedec_id);
BOOL w25qxx_read_status1(UB *status);
BOOL w25qxx_wait_ready(void);
BOOL w25qxx_write_enable(void);
BOOL w25qxx_write_disable(void);
BOOL w25qxx_read(UW address, UB *data, UINT size);
BOOL w25qxx_sector_erase(UW address);
BOOL w25qxx_page_program(UW address, const UB *data, UINT size);
const char *w25qxx_manufacturer_name(UB manufacturer_id);
UW w25qxx_capacity_bytes(UB capacity_id);

#endif /* W25QXX_H */
