/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone public driver export.
 */

/**
 * @file    drv_rjgt105_hal.h
 * @brief   RJGT105 硬件抽象层（HAL）接口。
 *
 * 声明 RSD 单线位操作的低层 API。
 * 所有时序关键例程均关中断执行，以保证 RSD 协议要求的微秒级精度。
 *
 * 参考位操作方案：
 *   - 写位: 推挽输出，位间无间隙（写 1 = 7us 低 + 43us 高；写 0 = 50us 低 + 1us 高）
 *   - 读位: 推挽下拉 7us → 浮空输入 → 15/25/35us 三点采样 → 50us 位末
 *           主动拉高 2us 恢复 (TREH，关键)
 *   - 帧尾: 读帧结束总线空闲保持 50us 后切回推挽输出高电平
 *
 * 实现说明：位级操作使用寄存器直写（BSRR/MODER/IDR）+ DWT
 * 周期计数延时。rt_pin_xxx/rt_hw_us_delay 的库调用
 * 开销（每次 0.5~5us）叠加在写 0 末 TREH 高电平之上会触发芯片帧结束判定，
 * 导致写帧被拒收。
 * 引脚寄存器基址由配置宏决定（RJGT105_RSD_PORT / RJGT105_RSD_PIN_IDX），
 * rsd_pin 参数保留以维持 API 兼容。
 *
 * 依赖: board.h (GPIO/RCC/CMSIS 寄存器), rtthread.h (临界区)
 */

#ifndef __DRV_RJGT105_HAL_H__
#define __DRV_RJGT105_HAL_H__

#include <rtthread.h>
#include "drv_gpio.h"
#include "drv_rjgt105_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   微秒级精确延时（DWT 周期计数忙等）。
 *
 * 位级时序必须用 DWT 精确延时，
 * rt_hw_us_delay（SysTick 忙等）的入口/退出开销会叠加在位电平宽度上。
 *
 * @param   us  延时微秒数
 */
void rjgt105_hal_delay_us(rt_uint32_t us);

/**
 * @brief   初始化 RSD GPIO 引脚。
 *
 * 将引脚配置为推挽输出（高速、无上下拉）并驱动高电平（空闲态），
 * 初始化过程使用寄存器级 GPIO 配置。
 * 读位期间动态切换为浮空输入，位末切回推挽输出并主动拉高恢复 (TREH)。
 *
 * @param   rsd_pin     RT-Thread 引脚号（通过 GET_PIN 宏获取）
 * @return  rt_err_t    RT_EOK 成功，负值为错误码
 */
rt_err_t rjgt105_hal_init(rt_base_t rsd_pin);

/**
 * @brief   去初始化 RSD GPIO 引脚。
 *
 * 恢复引脚为浮空输入模式（低功耗、安全状态）。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_hal_deinit(rt_base_t rsd_pin);

/**
 * @brief   发送 SYNC 同步脉冲。
 *
 * 拉低 RSD 线 >= 220us（250us 低 + 10us 尾高缓冲）以标志新帧开始，
 * 同时用于唤醒休眠中的芯片。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 * @return  rt_err_t    RT_EOK 成功
 *
 * @note    关中断执行以保证脉宽精度。
 */
rt_err_t rjgt105_hal_sync(rt_base_t rsd_pin);

/**
 * @brief   唤醒芯片。
 *
 * 等价于 rjgt105_hal_sync()。芯片在总线 3 秒无活动后自动进入休眠。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_hal_wakeup(rt_base_t rsd_pin);

/**
 * @brief   向 RSD 总线写一个字节（MSB 优先）。
 *
 * 按 RSD 编码发送 8 位：
 *   - 写 1: 7us  低电平 + 43us 高电平
 *   - 写 0: 50us 低电平 + 1us 高恢复 (TREH)
 *
 * @param   rsd_pin     RT-Thread 引脚号
 * @param   data        待发送的字节
 * @return  rt_err_t    RT_EOK 成功
 *
 * @note    整个字节期间关中断（最坏 ~8×51us ≈ 408us）。
 */
rt_err_t rjgt105_hal_write_byte(rt_base_t rsd_pin, rt_uint8_t data);

/**
 * @brief   从 RSD 总线读一个字节（MSB 优先），含 3 点过采样。
 *
 * 每位：主机推挽下拉 7us → 浮空输入 → 15/25/35us 三点采样 + 3 取 2 投票，
 * 位末主动拉高 2us 恢复（TREH，缺失会导致芯片状态机位错位）。
 * 芯片通过拉高（bit=1）或保持低（bit=0）来响应。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 * @param[out] p_data   存放读取字节的缓冲区
 * @return  rt_err_t    RT_EOK 成功
 *
 * @note    整个字节期间关中断（最坏 ~8×52us ≈ 416us）。
 */
rt_err_t rjgt105_hal_read_byte(rt_base_t rsd_pin, rt_uint8_t *p_data);

/**
 * @brief   向 RSD 总线连续写多个字节。
 *
 * 依次调用 rjgt105_hal_write_byte()。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 * @param   p_data      待发送数据缓冲区
 * @param   len         字节数
 * @return  rt_err_t    RT_EOK 成功，-RT_EINVAL p_data 为 NULL
 */
rt_err_t rjgt105_hal_write_bytes(rt_base_t rsd_pin, const rt_uint8_t *p_data,
                                  rt_uint8_t len);

/**
 * @brief   从 RSD 总线连续读多个字节。
 *
 * 依次调用 rjgt105_hal_read_byte()。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 * @param[out] p_data   接收数据缓冲区
 * @param   len         字节数
 * @return  rt_err_t    RT_EOK 成功，-RT_EINVAL p_data 为 NULL
 */
rt_err_t rjgt105_hal_read_bytes(rt_base_t rsd_pin, rt_uint8_t *p_data,
                                 rt_uint8_t len);

/**
 * @brief   读帧尾处理：总线空闲保持 50us 后切回推挽输出高电平。
 *
 * 每帧读完数据后调用，
 * 保证芯片完成帧收尾且总线恢复空闲高。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 */
void rjgt105_hal_read_frame_end(rt_base_t rsd_pin);

/**
 * @brief   释放总线：恢复 RSD 线到空闲高电平。
 *
 * 配置引脚为输出并驱动高电平。读/写事务完成后调用以恢复总线空闲。
 *
 * @param   rsd_pin     RT-Thread 引脚号
 */
void rjgt105_hal_release_bus(rt_base_t rsd_pin);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_RJGT105_HAL_H__ */
