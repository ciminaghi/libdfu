#ifndef __DFU_STM32_H__
#define __DFU_STM32_H__

#ifdef __cplusplus
extern "C" {
#endif

extern struct dfu_target_ops stm32_dfu_target_ops;

/*
 * STM32F469BI device data (Arduino STAR8)
 */
extern const struct stm32_device_data stm32f469bi_device_data;

#ifdef __cplusplus
}
#endif

#endif /* __DFU_STM32_H__ */
