#include <assert.h>
#include <stdio.h>
#include "trykernel.h"
static UB simulated_status = 9U;
static BOOL bus_ok = TRUE;
ER tk_dly_tsk(RELTIM delay) { (void)delay; return 0; }
BOOL i2c0_write(UB addr, const UB *data, UINT size)
{ (void)addr; (void)data; (void)size; return bus_ok; }
BOOL i2c0_write_read(UB addr, const UB *w, UINT wn, UB *r, UINT rn)
{
    UH reg = ((UH)w[0] << 8) | w[1];
    (void)addr; (void)wn;
    if(!bus_ok) return FALSE;
    for(UINT i = 0; i < rn; i++) r[i] = 0U;
    switch(reg){
    case 0x00e5: r[0] = 1U; break;
    case 0x010f: r[0] = 0xeaU; r[1] = 0xccU; break;
    case 0x0031: r[0] = 1U; break;
    case 0x0089: r[0] = simulated_status; break;
    case 0x0096: r[0] = 0U; r[1] = 80U; break;
    }
    return TRUE;
}
#include "../../drivers/vl53l1x.c"
int main(void)
{
    UH distance = 777U, raw;
    UB status;
    assert(vl53l1x_read_distance(&distance) == TRUE && distance == 80U);
    simulated_status = 4U;
    distance = 777U;
    assert(vl53l1x_read_distance(&distance) == FALSE && distance == 777U);
    assert(vl53l1x_get_last_result(&raw, &status) == TRUE);
    assert(raw == 80U && status == 4U);
    simulated_status = 8U;
    assert(vl53l1x_read_distance(&distance) == FALSE);
    simulated_status = 0xe9U; /* 下位5ビットのみ使用 */
    assert(vl53l1x_read_distance(&distance) == TRUE);
    bus_ok = FALSE;
    assert(vl53l1x_read_distance(&distance) == FALSE);
    assert(vl53l1x_get_last_result(&raw, &status) == FALSE);
    puts("range tests passed: valid, invalid, status mask and I2C failure");
    return 0;
}
