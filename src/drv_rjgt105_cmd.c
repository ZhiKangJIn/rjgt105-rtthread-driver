/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone public driver export.
 */

/**
 * @file    drv_rjgt105_cmd.c
 * @brief   RJGT105 命令封装层。
 *
 * 在 HAL 层之上封装 RSD 帧读写、寄存器操作、命令执行与状态轮询，
 * 提供芯片级原子操作（EEPROM 读写、TRNG、认证、计次等）。
 *
 * 本层不持有状态，所有函数以 rsd_pin 为第一参数。
 * 调用者负责互斥（设备框架层通过 mutex 保证）。
 */

#include "drv_rjgt105_cmd.h"
#include "drv_rjgt105_config.h"
#include "drv_rjgt105_hal.h"
#include "drv_rjgt105.h"

#define DBG_TAG    "drv.rjgt105.cmd"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

/* ==========================================================================
 *  内部（static）辅助函数 —— MISRA C:2012 Rule 8.9
 * ========================================================================== */

/**
 * @brief   构造命令头字节：操作码 | (5-bit 寄存器地址)。
 */
static rt_uint8_t rjgt105_cmd_make_header(rt_uint8_t op, rt_uint8_t reg_addr)
{
    return op | (reg_addr & 0x1Fu);
}

/**
 * @brief   发送 RSD 写帧: SYNC + 写命令头 + 数据。
 *
 * 帧格式: SYNC | 110_AAAAA | DATA1 ... DATAN
 *
 * 命令头 + 数据段在同一帧级临界区内无缝连续发送：写方向芯片仅作被动
 * 接收（按连续位流解析，芯片以 TREH 高电平间隔区分位边界，最大允许
 * 2us），段间不允许任何间隙 —— 超过 TREH 上限的长高电平会被芯片视为
 * 帧结束并复位接收状态机，导致整帧丢弃（实测: 命令头与数据段间插入
 * 较长段间间隙会使芯片终止当前帧，因此发送必须连续。
 */
static rt_err_t rjgt105_cmd_write_frame(rt_base_t rsd_pin,
                                         rt_uint8_t reg_addr,
                                         const rt_uint8_t *p_data,
                                         rt_uint8_t len)
{
    rt_err_t ret;
    rt_uint8_t header;
    rt_base_t level;

    /* SYNC 同步脉冲 */
    ret = rjgt105_hal_sync(rsd_pin);
    if (ret != RT_EOK)
    {
        LOG_E("write frame sync 失败");
        return ret;
    }

    /* 写命令头 + 数据段（帧级临界区，hal 层嵌套关中断安全）:
     * 写方向必须无缝连续发送，命令头与数据段之间禁止任何 Delay/间隙 */
    header = rjgt105_cmd_make_header(RJGT105_OP_WRITE, reg_addr);
    level = rt_hw_interrupt_disable();
    ret = rjgt105_hal_write_byte(rsd_pin, header);
    if (ret == RT_EOK && p_data != RT_NULL)
    {
        /* 紧接着发送数据，中间不能有任何 Delay */
        ret = rjgt105_hal_write_bytes(rsd_pin, p_data, len);
    }
    rt_hw_interrupt_enable(level);

    if (ret != RT_EOK)
    {
        LOG_E("write frame 失败, len=%u", (unsigned int)len);
        return ret;
    }

    return RT_EOK;
}

/**
 * @brief   发送 RSD 读帧: SYNC + 读命令头 + 读取数据。
 *
 * 帧格式: SYNC | 101_AAAAA | DATA1 ... DATAN
 */
static rt_err_t rjgt105_cmd_read_frame(rt_base_t rsd_pin,
                                        rt_uint8_t reg_addr,
                                        rt_uint8_t *p_data,
                                        rt_uint8_t len)
{
    rt_err_t ret;
    rt_uint8_t header;

    /* 参数检查 */
    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* SYNC 同步脉冲 */
    ret = rjgt105_hal_sync(rsd_pin);
    if (ret != RT_EOK)
    {
        LOG_E("read frame sync 失败");
        return ret;
    }

    /* 读命令头 */
    header = rjgt105_cmd_make_header(RJGT105_OP_READ, reg_addr);
    ret = rjgt105_hal_write_byte(rsd_pin, header);
    if (ret != RT_EOK)
    {
        LOG_E("read frame header 失败: 0x%02X", (unsigned int)header);
        return ret;
    }

    /* 实芯片验证: 命令头后需等待 100us 再读数据，否则会导致读数错乱。 */
    rjgt105_hal_delay_us(RJGT105_CMD_GAP_US);

    /* 读数据段 */
    if (len > 0u)
    {
        ret = rjgt105_hal_read_bytes(rsd_pin, p_data, len);
        if (ret != RT_EOK)
        {
            LOG_E("read frame data 失败, len=%u", (unsigned int)len);
            return ret;
        }
    }

    /* 帧尾: 总线空闲保持 50us 后恢复推挽输出高电平。 */
    rjgt105_hal_read_frame_end(rsd_pin);

    return RT_EOK;
}

/**
 * @brief   写芯片寄存器。
 */
static rt_err_t rjgt105_cmd_reg_write(rt_base_t rsd_pin,
                                       rt_uint8_t reg_addr,
                                       const rt_uint8_t *p_data,
                                       rt_uint8_t len)
{
    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    return rjgt105_cmd_write_frame(rsd_pin, reg_addr, p_data, len);
}

/**
 * @brief   读芯片寄存器。
 */
static rt_err_t rjgt105_cmd_reg_read(rt_base_t rsd_pin,
                                      rt_uint8_t reg_addr,
                                      rt_uint8_t *p_data,
                                      rt_uint8_t len)
{
    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    return rjgt105_cmd_read_frame(rsd_pin, reg_addr, p_data, len);
}

/**
 * @brief   轮询 ES 状态寄存器直至非 BUSY，带超时。
 *
 * @return  RT_EOK 成功，-RT_ETIMEOUT 超时
 */
static rt_err_t rjgt105_cmd_poll_status(rt_base_t rsd_pin, rt_uint32_t timeout_ms)
{
    rt_err_t ret;
    rt_uint8_t es;
    rt_uint32_t elapsed = 0u;

    /* 状态寄存器为 1 Byte，地址 0x12 */
    while (elapsed < timeout_ms)
    {
        ret = rjgt105_cmd_read_frame(rsd_pin, RJGT105_REG_ES, &es, 1u);
        if (ret != RT_EOK)
        {
            return ret;
        }

        if (es != RJGT105_STATUS_BUSY)
        {
            return RT_EOK;
        }

        /* 每 1ms 轮询一次 */
        rt_thread_mdelay(1u);
        elapsed++;
    }

    LOG_E("状态轮询超时 (%lu ms)", (unsigned long)timeout_ms);
    return -RT_ETIMEOUT;
}

/**
 * @brief   发送命令字到 CMD 寄存器并等待完成。
 *
 * 流程:
 *   1. 写 CMD 寄存器 (0x11) = cmd
 *   2. 轮询 ES 寄存器至非 BUSY
 *   3. 读取最终状态码
 *
 * @param   timeout_ms   超时时间 (ms)
 * @param[out] p_status   最终状态码（可为 NULL）
 * @return  RT_EOK 成功，负值为错误
 */
static rt_err_t rjgt105_cmd_send_command(rt_base_t rsd_pin,
                                          rt_uint8_t cmd,
                                          rt_uint32_t timeout_ms,
                                          rt_uint8_t *p_status)
{
    rt_err_t ret;
    rt_uint8_t es = RJGT105_STATUS_BUSY;

    /* 写命令 */
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_CMD, &cmd, 1u);
    if (ret != RT_EOK)
    {
        LOG_E("写命令 0x%02X 失败", (unsigned int)cmd);
        return ret;
    }

    /* 轮询状态 */
    ret = rjgt105_cmd_poll_status(rsd_pin, timeout_ms);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 读取最终状态 */
    ret = rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_ES, &es, 1u);
    if (ret != RT_EOK)
    {
        return ret;
    }

    if (p_status != RT_NULL)
    {
        *p_status = es;
    }

    /* 状态码判断 */
    switch (es)
    {
    case RJGT105_STATUS_SUCCESS:
        return RT_EOK;
    case RJGT105_STATUS_LOCKED:
        LOG_E("芯片已锁定 (计次溢出)");
        return -RJGT105_ECHIP;
    case RJGT105_STATUS_FAIL:
        LOG_E("命令 0x%02X 执行失败", (unsigned int)cmd);
        return -RJGT105_ERROR;
    default:
        LOG_E("未知状态码: 0x%02X", (unsigned int)es);
        return -RJGT105_EIO;
    }
}

/**
 * @brief   操作入口统一唤醒芯片（每次原子操作一次）。
 *
 * 芯片在总线持续 3 秒无通信后自动进入休眠（TSLEEP，见数据手册第 7 节），
 * 休眠后首帧可能被芯片静默丢弃（实测曾出现二次读取全 0），
 * 因此每个公开 API 在操作开始前必须唤醒一次。
 *
 * 唤醒在电气上与 SYNC 脉冲等价（250us 低 + 10us 尾高缓冲）。唤醒脉冲后
 * 紧跟操作首帧的 SYNC 构成双连续脉冲：脉冲间的 10us 长高电平超过 TREH
 * 上限（2us），芯片接收状态机复位，后续 SYNC 重新起帧，不影响帧解析。
 *
 * @param   rsd_pin   RT-Thread 引脚号
 * @return  rt_err_t  RT_EOK 成功
 */
static rt_err_t rjgt105_cmd_wakeup(rt_base_t rsd_pin)
{
    rt_err_t ret;

    ret = rjgt105_hal_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        LOG_E("操作前唤醒芯片失败");
    }

    return ret;
}

/* ==========================================================================
 *  公开 API —— 芯片级原子操作
 * ========================================================================== */

/**
 * @brief   写数据到 BUFF 缓冲区（寄存器 0x00，16 Byte）。
 *
 * 写帧: SYNC | 110_00000 | DATA1 ... DATAN，直接写 BUFF，不触发命令。
 */
rt_err_t rjgt105_cmd_buff_write(rt_base_t rsd_pin,
                                const rt_uint8_t *p_data,
                                rt_uint8_t len)
{
    rt_err_t ret;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (len > RJGT105_BUFF_SIZE)
    {
        len = RJGT105_BUFF_SIZE;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    return rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_BUFF, p_data, len);
}

/**
 * @brief   读 BUFF 缓冲区数据（寄存器 0x00，16 Byte）。
 *
 * 读帧: SYNC | 101_00000 | DATA1 ... DATAN，直接读 BUFF，不触发命令。
 */
rt_err_t rjgt105_cmd_buff_read(rt_base_t rsd_pin,
                               rt_uint8_t *p_data,
                               rt_uint8_t len)
{
    rt_err_t ret;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (len > RJGT105_BUFF_SIZE)
    {
        len = RJGT105_BUFF_SIZE;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    return rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF, p_data, len);
}

/**
 * @brief   读 EEPROM 页数据到缓冲区并读出。
 *
 * 流程: 设 DEST_ADDR → 发 READ_BUFF 命令 → 等待 → 读 BUFF
 */
rt_err_t rjgt105_cmd_eeprom_read(rt_base_t rsd_pin,
                                  rt_uint8_t page_addr,
                                  rt_uint8_t *p_data,
                                  rt_uint8_t size)
{
    rt_err_t ret;
    rt_uint8_t dest = page_addr;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (size > RJGT105_PAGE_SIZE)
    {
        size = RJGT105_PAGE_SIZE;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 设置目的页地址 */
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_DEST_ADDR, &dest, 1u);
    if (ret != RT_EOK)
    {
        LOG_E("设置 DEST_ADDR=0x%02X 失败", (unsigned int)dest);
        return ret;
    }

    /* 发送 READ_BUFF 命令并等待 */
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_READ_BUFF,
                                    (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 读 BUFF 获取 EEPROM 数据 */
    ret = rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF, p_data, size);

    return ret;
}

/**
 * @brief   写数据到 BUFF 再写入 EEPROM 页。
 *
 * 流程: 写 BUFF → 设 DEST_ADDR → 发 WRITE_BUFF 命令 → 等待
 */
rt_err_t rjgt105_cmd_eeprom_write(rt_base_t rsd_pin,
                                   rt_uint8_t page_addr,
                                   const rt_uint8_t *p_data,
                                   rt_uint8_t size)
{
    rt_err_t ret;
    rt_uint8_t dest = page_addr;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (size > RJGT105_PAGE_SIZE)
    {
        size = RJGT105_PAGE_SIZE;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 写 BUFF */
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_BUFF, p_data, size);
    if (ret != RT_EOK)
    {
        LOG_E("写 BUFF 失败");
        return ret;
    }

    /* 设置目的页地址 */
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_DEST_ADDR, &dest, 1u);
    if (ret != RT_EOK)
    {
        LOG_E("设置 DEST_ADDR=0x%02X 失败", (unsigned int)dest);
        return ret;
    }

    /* 发送 WRITE_BUFF 命令并等待 (EEPROM 写入最长 36ms) */
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_WRITE_BUFF,
                                    (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);

    return ret;
}

rt_err_t rjgt105_cmd_eeprom_read_session(rt_base_t rsd_pin,
                                         rt_uint8_t page_addr,
                                         rt_uint8_t *p_data,
                                         rt_uint8_t size)
{
    rt_err_t ret;
    rt_uint8_t dest = page_addr;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (size > RJGT105_PAGE_SIZE)
    {
        size = RJGT105_PAGE_SIZE;
    }
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_DEST_ADDR, &dest, 1u);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_READ_BUFF,
                                   (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);
    if (ret != RT_EOK)
    {
        return ret;
    }
    return rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF, p_data, size);
}

rt_err_t rjgt105_cmd_eeprom_write_session(rt_base_t rsd_pin,
                                          rt_uint8_t page_addr,
                                          const rt_uint8_t *p_data,
                                          rt_uint8_t size)
{
    rt_err_t ret;
    rt_uint8_t dest = page_addr;

    if (p_data == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (size > RJGT105_PAGE_SIZE)
    {
        size = RJGT105_PAGE_SIZE;
    }
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_BUFF, p_data, size);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_DEST_ADDR, &dest, 1u);
    if (ret != RT_EOK)
    {
        return ret;
    }
    return rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_WRITE_BUFF,
                                    (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);
}

/**
 * @brief   产生 4 Byte 真随机数并读出。
 *
 * 流程: 发 TRNG 命令 → 等待 → 读 BUFF[0:3]
 */
rt_err_t rjgt105_cmd_trng_generate(rt_base_t rsd_pin, rt_uint8_t *p_rng)
{
    rt_err_t ret;

    if (p_rng == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 发送 TRNG 命令 */
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_TRNG_GENERATE,
                                    (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 读 BUFF[0:3] 获取随机数 */
    ret = rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF, p_rng, 4u);

    return ret;
}

/**
 * @brief   主机认证 (MAC Generate 0x55)。
 *
 * 流程:
 *   1. 写 4 Byte IV 到 BUFF[0:3]
 *   2. 发 0x55 命令 → 芯片用 key + TRNG随机数 + IV 计算 8 Byte MAC → 存 BUFF[0:7]
 *   3. 等待完成 → 读 BUFF[0:7] 获取芯片 MAC
 *
 * @param   rsd_pin     RSD 引脚号
 * @param   iv          4 Byte 初始向量（主机生成）
 * @param[out] mac_out  8 Byte 芯片 MAC 输出
 * @return  rt_err_t    RT_EOK 成功
 *
 * @note    调用前必须已执行 TRNG (0x31)，芯片需要 BUFF 中的随机数。
 */
rt_err_t rjgt105_cmd_host_auth(rt_base_t rsd_pin,
                                const rt_uint8_t *iv,
                                rt_uint8_t *mac_out)
{
    rt_err_t ret;

    if ((iv == RT_NULL) || (mac_out == RT_NULL))
    {
        return -RT_EINVAL;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 写 IV 到 BUFF[0:3] */
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_BUFF, iv, RJGT105_IV_SIZE);
    if (ret != RT_EOK)
    {
        LOG_E("写 IV 到 BUFF 失败");
        return ret;
    }

    /* 发送 MAC Generate 命令 (0x55) */
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_AUTH_MAC_GEN,
                                    (rt_uint32_t)RJGT105_AUTH_TIMEOUT_MS, RT_NULL);
    if (ret != RT_EOK)
    {
        LOG_E("MAC Generate (0x55) 失败: %d", (int)ret);
        return ret;
    }

    /* 读 BUFF[0:7] 获取芯片 MAC */
    ret = rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF, mac_out, RJGT105_MAC_SIZE);

    return ret;
}

rt_err_t rjgt105_cmd_host_auth_exchange(rt_base_t rsd_pin,
                                        const rt_uint8_t *iv,
                                        rt_uint8_t *rng_out,
                                        rt_uint8_t *mac_out)
{
    rt_err_t ret;

    if ((iv == RT_NULL) || (rng_out == RT_NULL) || (mac_out == RT_NULL))
    {
        return -RT_EINVAL;
    }
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_TRNG_GENERATE,
                                   (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF, rng_out, RJGT105_IV_SIZE);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_BUFF, iv, RJGT105_IV_SIZE);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_AUTH_MAC_GEN,
                                   (rt_uint32_t)RJGT105_AUTH_TIMEOUT_MS, RT_NULL);
    if (ret != RT_EOK)
    {
        return ret;
    }
    return rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF,
                                mac_out, RJGT105_MAC_SIZE);
}

rt_err_t rjgt105_cmd_auth_session_begin(rt_base_t rsd_pin,
                                        rt_uint8_t *rng_out)
{
    rt_err_t ret;

    if (rng_out == RT_NULL)
    {
        return -RT_EINVAL;
    }
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_TRNG_GENERATE,
                                   (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);
    if (ret != RT_EOK)
    {
        return ret;
    }
    return rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_BUFF,
                                rng_out, RJGT105_IV_SIZE);
}

rt_err_t rjgt105_cmd_slave_auth_session_finish(rt_base_t rsd_pin,
                                                const rt_uint8_t *iv,
                                                const rt_uint8_t *mac_in)
{
    rt_err_t ret;
    rt_uint8_t es = 0u;
    rt_uint8_t buffer[RJGT105_IV_SIZE + RJGT105_MAC_SIZE];

    if ((iv == RT_NULL) || (mac_in == RT_NULL))
    {
        return -RT_EINVAL;
    }
    rt_memcpy(buffer, iv, RJGT105_IV_SIZE);
    rt_memcpy(&buffer[RJGT105_IV_SIZE], mac_in, RJGT105_MAC_SIZE);
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_BUFF, buffer, sizeof(buffer));
    if (ret == RT_EOK)
    {
        ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_AUTH_MAC_VERIFY,
                                       (rt_uint32_t)RJGT105_AUTH_TIMEOUT_MS, &es);
    }
    rt_memset(buffer, 0, sizeof(buffer));
    if ((ret != RT_EOK) && (es == RJGT105_STATUS_FAIL))
    {
        return -RJGT105_EAUTH;
    }
    return ret;
}

/**
 * @brief   从机认证 (MAC Verify 0xA5)。
 *
 * 流程:
 *   1. 写 4 Byte IV + 8 Byte 主机 MAC 到 BUFF[0:11]
 *   2. 发 0xA5 命令 → 芯片用 key + TRNG随机数 + IV 计算 MAC
 *                     并与 BUFF[4:11] 的主机 MAC 比较
 *   3. 等待完成 → 读 ES 状态
 *
 * @param   rsd_pin     RSD 引脚号
 * @param   iv          4 Byte 初始向量（主机生成）
 * @param   mac_in      8 Byte 主机 MAC（由 RC4(key, TRNG随机数, IV) 计算）
 * @return  rt_err_t    RT_EOK 认证通过
 * @retval  -RJGT105_EAUTH 芯片拒绝（MAC 不匹配）
 *
 * @note    调用前必须已执行 TRNG (0x31)。
 */
rt_err_t rjgt105_cmd_slave_auth(rt_base_t rsd_pin,
                                 const rt_uint8_t *iv,
                                 const rt_uint8_t *mac_in)
{
    rt_err_t ret;
    rt_uint8_t es;
    rt_uint8_t buf[RJGT105_IV_SIZE + RJGT105_MAC_SIZE];  /* 12 Byte: IV + MAC */

    if ((iv == RT_NULL) || (mac_in == RT_NULL))
    {
        return -RT_EINVAL;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* 拼装 IV + MAC 到临时缓冲区 */
    rt_memcpy(&buf[0], iv, RJGT105_IV_SIZE);
    rt_memcpy(&buf[RJGT105_IV_SIZE], mac_in, RJGT105_MAC_SIZE);

    /* 写 IV + MAC 到 BUFF[0:11] */
    ret = rjgt105_cmd_reg_write(rsd_pin, RJGT105_REG_BUFF,
                                 buf, RJGT105_IV_SIZE + RJGT105_MAC_SIZE);
    if (ret != RT_EOK)
    {
        LOG_E("写 IV+MAC 到 BUFF 失败");
        return ret;
    }

    /* 发送 MAC Verify 命令 (0xA5) */
    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_AUTH_MAC_VERIFY,
                                    (rt_uint32_t)RJGT105_AUTH_TIMEOUT_MS, &es);
    if (ret != RT_EOK)
    {
        if (es == RJGT105_STATUS_FAIL)
        {
            LOG_E("从机认证失败: MAC 不匹配");
            return -RJGT105_EAUTH;
        }
        return ret;
    }

    LOG_D("从机认证通过");
    return RT_EOK;
}

/**
 * @brief   芯片计次自增 1。
 *
 * @warning 达到阈值后芯片将永久锁定，不可逆。
 */
rt_err_t rjgt105_cmd_counter_increment(rt_base_t rsd_pin)
{
    rt_err_t ret;
    rt_uint8_t es;

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_COUNTER_INC,
                                    (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, &es);
    if (ret != RT_EOK)
    {
        if (es == RJGT105_STATUS_LOCKED)
        {
            LOG_E("计次已达阈值，芯片已永久锁定!");
        }
        return ret;
    }

    return RT_EOK;
}

/**
 * @brief   EEPROM 片擦除（仅初始化模式下有效）。
 */
rt_err_t rjgt105_cmd_chip_erase(rt_base_t rsd_pin)
{
    rt_err_t ret;
    rt_uint8_t page[RJGT105_PAGE_SIZE];

    /* 软件安全闸：MODE_SEL=0x5A 时数据手册明确禁止片擦除。
     * 若功能页因认证保护不可读，也采取保守策略拒绝发送 0x35。 */
    ret = rjgt105_cmd_eeprom_read(rsd_pin, RJGT105_EEPROM_FUNCTRL,
                                  page, RJGT105_PAGE_SIZE);
    if (ret != RT_EOK)
    {
        LOG_E("无法确认初始化模式，拒绝发送片擦除命令");
        return -RJGT105_EPROTECT;
    }
    if (page[RJGT105_FUNCTRL_MODE_SEL] == 0x5Au)
    {
        LOG_E("MODE_SEL=0x5A（正常模式），拒绝发送片擦除命令");
        return -RJGT105_EBUSY;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    return rjgt105_cmd_send_command(rsd_pin, RJGT105_CMD_CHIP_ERASE,
                                     (rt_uint32_t)RJGT105_CMD_TIMEOUT_MS, RT_NULL);
}

/**
 * @brief   读取芯片版本号 (6 Byte, "GT1015")。
 */
rt_err_t rjgt105_cmd_read_version(rt_base_t rsd_pin, rt_uint8_t *p_version)
{
    rt_err_t ret;

    if (p_version == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    return rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_VERSION, p_version, 6u);
}

/**
 * @brief   读取芯片 UID (8 Byte)。
 *
 * 先读 EEPROM 页 0x01 (UID 页) 到 BUFF，再读 BUFF。
 *
 * @note    纯包装函数，唤醒由内部 rjgt105_cmd_eeprom_read 完成，不重复唤醒。
 */
rt_err_t rjgt105_cmd_read_uid(rt_base_t rsd_pin, rt_uint8_t *p_uid)
{
    if (p_uid == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* 通过 EEPROM 读流程读取 UID 页 */
    return rjgt105_cmd_eeprom_read(rsd_pin, RJGT105_EEPROM_UID,
                                    p_uid, RJGT105_UID_SIZE);
}

/**
 * @brief   读取计次值 (CNT_VAL + CNT_TH)。
 */
rt_err_t rjgt105_cmd_read_counter(rt_base_t rsd_pin,
                                   rt_uint8_t *p_count_val,
                                   rt_uint8_t *p_count_th)
{
    rt_err_t ret;

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* CNT_VAL: 寄存器 0x13, 2 Byte */
    if (p_count_val != RT_NULL)
    {
        ret = rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_COUNT_VAL,
                                    p_count_val, 2u);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }

    /* CNT_TH: 寄存器 0x15, 2 Byte */
    if (p_count_th != RT_NULL)
    {
        ret = rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_COUNT_TH,
                                    p_count_th, 2u);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }

    return RT_EOK;
}

/**
 * @brief   读取芯片当前状态。
 */
rt_err_t rjgt105_cmd_get_status(rt_base_t rsd_pin, rt_uint8_t *p_status)
{
    rt_err_t ret;

    if (p_status == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* 操作前唤醒芯片（每次操作一次） */
    ret = rjgt105_cmd_wakeup(rsd_pin);
    if (ret != RT_EOK)
    {
        return ret;
    }

    return rjgt105_cmd_reg_read(rsd_pin, RJGT105_REG_ES, p_status, 1u);
}
