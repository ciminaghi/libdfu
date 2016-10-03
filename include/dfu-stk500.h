#ifndef __DFU_STK500_H__
#define __DFU_STK500_H__

#ifdef __cplusplus
extern "C" {
#endif

struct dfu_target_ops;

extern struct dfu_target_ops stk500_dfu_target_ops;

/*
 * Parameters for supported devices
 */
extern const struct stk500_device_data atmega328p_device_data;

#ifdef __cplusplus
}
#endif

#endif /* __DFU_STK500_H__ */
