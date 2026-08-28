/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone public driver export.
 */

/**
 * @file    drv_rjgt105_hal.c
 * @brief   RJGT105 RSD 单线硬件抽象层实现。
 *
 * 基于 GPIO 寄存器直写（BSRR/MODER/IDR）+ DWT 周期计数延时，实现 RSD
 * 协议的位级操作（SYNC 同步、写字节、读字节）。
 *
 * 所有时序关键段均关中断执行，以保证 RJGT105 数据手册（第 7 节）要求的
 * 微秒级精度。
 *
 * === 寄存器直写 + DWT 延时设计 ===
 *
 * 背景: rt_pin_write (HAL_GPIO_WritePin, ~0.5~1us)、rt_pin_mode
 * (HAL_GPIO_Init, ~2~5us)、rt_hw_us_delay (SysTick 入口/退出) 的库调用开销
 * 叠加在写 0 末 TREH 高电平之后，使位间总高电平约 4.5~6us，超过芯片帧结束
 * 判定容忍并导致整帧被拒收。因此位级操作使用寄存器直写和 DWT 精确延时。
 *
 * 方向切换统一"先置电平再切模式"（SET_LOW 先于 MODE_OUT / SET_HIGH 先于
 * MODE_OUT），避免切出瞬间高电平毛刺；寄存器直写模式下两者间隔为 ns 级，
 * 不引入可测量的电平宽度偏差。
 *
 * 引脚寄存器基址由配置宏决定（RJGT105_RSD_PORT / RJGT105_RSD_PIN_IDX），
 * rsd_pin 参数保留以维持 API 兼容。
 */

#include "drv_rjgt105_hal.h"
#include "drv_rjgt105_config.h"
#include <board.h>

#define DBG_TAG    "drv.rjgt105.hal"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

/* ==========================================================================
 *  引脚寄存器宏 —— 寄存器直写模式
 *
 *  写位: 推挽输出（先置电平再切输出，避免切出瞬间高电平毛刺）
 *  读位: 推挽下拉 7us → 浮空输入采样 → 位末切回推挽输出主动拉高 (TREH)
 * ========================================================================== */

#define RSD_GPIO_PORT       (RJGT105_RSD_PORT)                  /* GPIOB */
#define RSD_GPIO_PIN_IDX    (RJGT105_RSD_PIN_IDX)               /* 12 */
#define RSD_GPIO_MASK       (1u << RSD_GPIO_PIN_IDX)            /* 1 << 12 */
#define RSD_MODER_POS       (RSD_GPIO_PIN_IDX * 2u)

#define RSD_SET_HIGH()      (RSD_GPIO_PORT->BSRR = RSD_GPIO_MASK)
#define RSD_SET_LOW()       (RSD_GPIO_PORT->BSRR = (RSD_GPIO_MASK << 16u))
#define RSD_MODE_OUT()      (RSD_GPIO_PORT->MODER = ((RSD_GPIO_PORT->MODER & ~(3u << RSD_MODER_POS)) | (1u << RSD_MODER_POS)))
#define RSD_MODE_IN()       (RSD_GPIO_PORT->MODER &= ~(3u << RSD_MODER_POS))
#define RSD_READ()          (((RSD_GPIO_PORT->IDR & RSD_GPIO_MASK) != 0u) ? 1u : 0u)

#define RSD_DELAY_US(us)    rjgt105_hal_delay_us((rt_uint32_t)(us))

/* ==========================================================================
 *  内部（static）辅助函数 —— MISRA C:2012 Rule 8.9（限制作用域）
 * ========================================================================== */

/**
 * @brief   微秒级精确延时（DWT 周期计数忙等）。
 *
 * 基于 DWT->CYCCNT 周期计数，
 * 无 SysTick 入口/退出开销，位级时序精度由内核时钟周期保证。
 * 首次调用自动使能 DWT CYCCNT（需先解锁 DEMCR.TRCENA）。
 *
 * @param   us  延时微秒数（0 直接返回）
 */
void rjgt105_hal_delay_us(rt_uint32_t us)
{
    rt_uint32_t start;
    rt_uint32_t ticks;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u)
    {
        DWT->CYCCNT = 0u;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    start = DWT->CYCCNT;
    ticks = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < ticks)
    {
    }
}

/**
 * @brief   向 RSD 总线写一个位（MSB 优先）。
 *
 * 编码（RSD 协议第 3.2 节）：
 *   - '1': 拉低 7us  后拉高 43us（位间无间隙）。
 *   - '0': 拉低 50us 后拉高 1us 恢复（TREH 0.5~2us）。
 *
 * 寄存器直写，无任何库调用开销。每次位起始先置低再切输出，
 * 保证即使引脚此前处于输入模式也无高电平毛刺。
 *
 * @param   bit       非零 = bit-1，零 = bit-0
 *
 * @note    调用前必须关中断（由 write_byte / write_bytes 帧级临界区保证）。
 */
static void rjgt105_hal_write_bit(rt_uint8_t bit)
{
    if (bit != 0u)
    {
        /* 写逻辑 1: 7us 低电平 + 43us 高电平 */
        RSD_SET_LOW();              /* 先置低再切输出, 避免切出瞬间高电平毛刺 */
        RSD_MODE_OUT();
        RSD_DELAY_US(RJGT105_WRITE_1_US);
        RSD_SET_HIGH();
        RSD_DELAY_US(RJGT105_WRITE_1_HIGH_US);
    }
    else
    {
        /* 写逻辑 0: 50us 低电平 + 1us 高恢复 (TREH) */
        RSD_SET_LOW();
        RSD_MODE_OUT();
        RSD_DELAY_US(RJGT105_WRITE_0_US);
        RSD_SET_HIGH();
        RSD_DELAY_US(RJGT105_WRITE_0_TREH_US);
    }
}

/**
 * @brief   从 RSD 总线读一个位（MSB 优先），3 点过采样 + 多数表决。
 *
 * 时序（推挽 + 浮空输入）：
 *    0us   推挽下拉 7us（先置低再切输出）
 *    7us   切浮空输入，等待芯片驱动总线
 *   15us   第1次采样（芯片此时已驱动总线：拉高=1，拉低=0）
 *   25us   第2次采样
 *   35us   第3次采样
 *   50us   位数据窗口结束（保持浮空输入）
 *   50us   写 ODR 高（输入模式）→ 切回推挽输出 → 主动拉高 2us（TREH，关键!）
 *   52us   下一位开始
 *
 * @return  rt_uint8_t  多数表决结果：1 或 0
 *
 * @note    调用前必须关中断。TREH 主动拉高必须在关中断窗口内完成。
 */
static rt_uint8_t rjgt105_hal_read_bit(void)
{
    rt_uint8_t s1, s2, s3;
    rt_uint8_t ones;

    /* 0-7us: 推挽下拉发起读时隙（先置低再切输出，避免高电平毛刺） */
    RSD_SET_LOW();
    RSD_MODE_OUT();
    RSD_DELAY_US(RJGT105_READ_DRIVE_US);

    /* 7us: 立刻切换浮空输入模式，芯片接管总线 */
    RSD_MODE_IN();

    /* 7-15us: 等待芯片稳定驱动总线，到达 15us 采样点 */
    RSD_DELAY_US(RJGT105_READ_SAMPLE1_US - RJGT105_READ_DRIVE_US);
    s1 = RSD_READ();

    /* 15-25us: 到达第 2 采样点 */
    RSD_DELAY_US(RJGT105_READ_SAMPLE2_US - RJGT105_READ_SAMPLE1_US);
    s2 = RSD_READ();

    /* 25-35us: 到达第 3 采样点 */
    RSD_DELAY_US(RJGT105_READ_SAMPLE3_US - RJGT105_READ_SAMPLE2_US);
    s3 = RSD_READ();

    /* 35-50us: 等待位数据窗口结束（保持浮空输入） */
    RSD_DELAY_US(RJGT105_READ_PERIOD_US - RJGT105_READ_SAMPLE3_US);

    /* 50us: 主动拉高制造恢复时间 (TREH)，必须位于关中断窗口内:
     * 缺失该恢复会导致芯片边沿检测漏检下降沿，内部状态机随机位错位 */
    RSD_SET_HIGH();
    RSD_MODE_OUT();
    RSD_DELAY_US(RJGT105_READ_TREH_US);

    /* 过采样投票: 3 次采样取多数 (3 取 2) */
    ones = (rt_uint8_t)(s1 + s2 + s3);
    return (ones >= 2u) ? 1u : 0u;
}

/* ==========================================================================
 *  公开 API
 * ========================================================================== */

/**
 * @brief   初始化 RSD GPIO 引脚为推挽输出 + 空闲高电平。
 *
 * 引脚初始化: 使能端口时钟 → 推挽 → 高速 → 无上下拉
 * → 置高 → 切输出。写位全程保持推挽输出；读位期间动态切换浮空输入
 * （MODER 寄存器直写），位末切回推挽输出并主动拉高恢复 (TREH)。
 *
 * @param   rsd_pin   RT-Thread 引脚号（通过 GET_PIN 宏获取）
 * @return  rt_err_t  RT_EOK 成功
 */
rt_err_t rjgt105_hal_init(rt_base_t rsd_pin)
{
    /* 使能 RSD 端口时钟（PB12 → GPIOB, AHB1ENR bit1；与 RJGT105_RSD_PORT 一致） */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    /* 推挽输出, 高速, 无上下拉 (浮空), 空闲高 */
    RSD_GPIO_PORT->OTYPER    &= ~RSD_GPIO_MASK;               /* 推挽 */
    RSD_GPIO_PORT->OSPEEDR   |= (3u << RSD_MODER_POS);        /* 高速 */
    RSD_GPIO_PORT->PUPDR     &= ~(3u << RSD_MODER_POS);       /* 无上下拉 */
    RSD_SET_HIGH();
    RSD_MODE_OUT();

    LOG_D("HAL 初始化: RSD 引脚 = %ld (寄存器直写)", (long)rsd_pin);

    return RT_EOK;
}

/**
 * @brief   去初始化 RSD GPIO 引脚。
 *
 * 恢复引脚为浮空输入模式（低功耗、安全状态）。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @return  rt_err_t  RT_EOK 成功
 */
rt_err_t rjgt105_hal_deinit(rt_base_t rsd_pin)
{
    /* 恢复引脚为浮空输入（MODER=00，低功耗、安全状态） */
    RSD_MODE_IN();

    LOG_D("HAL 去初始化: RSD 引脚 = %ld", (long)rsd_pin);

    return RT_EOK;
}

/**
 * @brief   发送 SYNC 同步脉冲：250us 低电平 + 10us 尾高缓冲。
 *
 * 用于每帧起始以及唤醒睡眠中的芯片。
 * 关中断执行以保证脉宽精度。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @return  rt_err_t  RT_EOK 成功
 */
rt_err_t rjgt105_hal_sync(rt_base_t rsd_pin)
{
    rt_base_t level;

    (void)rsd_pin;

    /* 临界段：时序需精确匹配数据手册第 3.1 节 */
    level = rt_hw_interrupt_disable();

    RSD_SET_LOW();              /* 先置低再切输出，避免切出瞬间高电平毛刺 */
    RSD_MODE_OUT();
    RSD_DELAY_US(RJGT105_SYNC_PULSE_US);
    RSD_SET_HIGH();
    RSD_DELAY_US(RJGT105_SYNC_HIGH_US);

    rt_hw_interrupt_enable(level);

    return RT_EOK;
}

/**
 * @brief   唤醒芯片。
 *
 * 芯片在总线持续 3 秒无通信后自动进入休眠。
 * >= 220us 的低脉冲（SYNC）可将其唤醒。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @return  rt_err_t  RT_EOK 成功
 */
rt_err_t rjgt105_hal_wakeup(rt_base_t rsd_pin)
{
    /* 唤醒在电气上与 SYNC 脉冲等价 */
    return rjgt105_hal_sync(rsd_pin);
}

/**
 * @brief   向 RSD 总线写一个字节（MSB 优先）。
 *
 * 连续发送 8 位。整个字节传输期间关中断
 *（最坏情况: 8 × 51us ≈ 408us）。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @param   data      待发送的字节
 * @return  rt_err_t  RT_EOK 成功
 */
rt_err_t rjgt105_hal_write_byte(rt_base_t rsd_pin, rt_uint8_t data)
{
    rt_base_t level;
    rt_uint8_t i;

    (void)rsd_pin;

    /* 临界段 —— MISRA C:2012 Dir 4.6 / Rule 13.2（整型循环计数器） */
    level = rt_hw_interrupt_disable();

    for (i = 0u; i < 8u; i++)
    {
        /* MSB 优先: 依次提取 bit 7, 6, ..., 0 */
        rt_uint8_t bit = (data >> (7u - i)) & 0x01u;
        rjgt105_hal_write_bit(bit);
    }

    rt_hw_interrupt_enable(level);

    return RT_EOK;
}

/**
 * @brief   从 RSD 总线读一个字节（MSB 优先）。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @param[out] p_data 存放读取字节的缓冲区指针
 * @return  rt_err_t  RT_EOK 成功，-RT_EINVAL 参数无效
 */
rt_err_t rjgt105_hal_read_byte(rt_base_t rsd_pin, rt_uint8_t *p_data)
{
    rt_base_t level;
    rt_uint8_t i;
    rt_uint8_t byte_val = 0u;

    (void)rsd_pin;

    /* 参数检查 —— MISRA C:2012 Rule 14.4（布尔控制表达式） */
    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* 临界段 */
    level = rt_hw_interrupt_disable();

    for (i = 0u; i < 8u; i++)
    {
        rt_uint8_t bit = rjgt105_hal_read_bit();

        /* MSB 优先移入 */
        byte_val = (byte_val << 1u) | bit;
    }

    rt_hw_interrupt_enable(level);

    *p_data = byte_val;

    return RT_EOK;
}

/**
 * @brief   向 RSD 总线连续写多个字节。
 *
 * 整段数据一次关中断发送（帧级临界区）：写方向芯片按连续位流解析，
 * 字节间隙若被 SysTick/调度插入（毫秒级高电平），芯片会误判帧结束
 * 或状态机跑飞（实测 BUFF 写 16 字节后芯片挂死）。
 * 16 字节最坏约 8.2ms 关中断，SysTick 挂起在恢复后自动补偿，无 tick 漂移。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @param   p_data    待发送数据缓冲区
 * @param   len       字节数
 * @return  rt_err_t  RT_EOK 成功，-RT_EINVAL p_data 为 NULL
 */
rt_err_t rjgt105_hal_write_bytes(rt_base_t rsd_pin, const rt_uint8_t *p_data,
                                  rt_uint8_t len)
{
    rt_err_t ret = RT_EOK;
    rt_base_t level;
    rt_uint8_t i;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (len == 0u)
    {
        return RT_EOK;
    }

    level = rt_hw_interrupt_disable();

    for (i = 0u; i < len; i++)
    {
        ret = rjgt105_hal_write_byte(rsd_pin, p_data[i]);
        if (ret != RT_EOK)
        {
            break;
        }
    }

    rt_hw_interrupt_enable(level);

    if (ret != RT_EOK)
    {
        LOG_E("写字节[%u] 失败: err=%d", (unsigned int)i, (int)ret);
    }

    return ret;
}

/**
 * @brief   从 RSD 总线连续读多个字节。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @param[out] p_data 接收数据缓冲区
 * @param   len       字节数
 * @return  rt_err_t  RT_EOK 成功，-RT_EINVAL p_data 为 NULL
 */
rt_err_t rjgt105_hal_read_bytes(rt_base_t rsd_pin, rt_uint8_t *p_data,
                                 rt_uint8_t len)
{
    rt_err_t ret = RT_EOK;
    rt_uint8_t i;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (len == 0u)
    {
        return RT_EOK;
    }

    for (i = 0u; i < len; i++)
    {
        ret = rjgt105_hal_read_byte(rsd_pin, &p_data[i]);
        if (ret != RT_EOK)
        {
            LOG_E("读字节[%u] 失败: err=%d", (unsigned int)i, (int)ret);
            break;
        }
    }

    return ret;
}

/**
 * @brief   读帧尾处理：总线空闲保持 50us 后切回推挽输出高电平。
 *
 * 每帧读完数据后调用，
 * 保证芯片完成帧收尾且总线恢复空闲高。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 */
void rjgt105_hal_read_frame_end(rt_base_t rsd_pin)
{
    (void)rsd_pin;

    /* 帧尾: 总线空闲保持 50us（读位末 TREH 已将总线置为推挽输出高），
     * 再切回推挽输出高电平（幂等） */
    RSD_DELAY_US(RJGT105_READ_TAIL_US);
    RSD_SET_HIGH();              /* 先置高再切输出 */
    RSD_MODE_OUT();
}

/**
 * @brief   释放 RSD 总线：恢复到空闲高电平状态。
 *
 * 读位末 TREH 已切回推挽输出，此处确保输出高电平（幂等）。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 */
void rjgt105_hal_release_bus(rt_base_t rsd_pin)
{
    (void)rsd_pin;

    RSD_SET_HIGH();
    RSD_MODE_OUT();
}
