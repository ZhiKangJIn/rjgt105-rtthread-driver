/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone public driver export.
 */

/**
 * @file    drv_rjgt105_cmd.h
 * @brief   RJGT105 命令层：寄存器/命令字/状态码宏定义 及 芯片级操作 API。
 */

#ifndef __DRV_RJGT105_CMD_H__
#define __DRV_RJGT105_CMD_H__

#include <rtthread.h>
#include "drv_rjgt105_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 *  寄存器地址
 * ========================================================================== */
#define RJGT105_REG_BUFF            0x00u   /* 数据缓冲区 (16 Byte) */
#define RJGT105_REG_DEST_ADDR       0x10u   /* 目的页地址（低 4 位有效） */
#define RJGT105_REG_CMD             0x11u   /* 命令寄存器 */
#define RJGT105_REG_ES              0x12u   /* 状态寄存器 */
#define RJGT105_REG_COUNT_VAL       0x13u   /* 计次值 (16 bit) */
#define RJGT105_REG_COUNT_TH        0x15u   /* 计次阈值 (16 bit) */
#define RJGT105_REG_VERSION         0x17u   /* 版本号 (48 bit, "GT1015") */

/* ==========================================================================
 *  操作码（已左移 5 位，直接 | 5-bit 地址即可拼装命令头字节）
 *  用法: cmd_header = RJGT105_OP_WRITE | (reg_addr & 0x1F);
 * ========================================================================== */
#define RJGT105_OP_WRITE            0xC0u   /* 110_00000: 写操作 */
#define RJGT105_OP_READ             0xA0u   /* 101_00000: 读操作 */

/* ==========================================================================
 *  命令字
 * ========================================================================== */
#define RJGT105_CMD_WRITE_BUFF      0x11u   /* BUFF -> EEPROM 写入 */
#define RJGT105_CMD_READ_BUFF       0x19u   /* EEPROM -> BUFF 读取 */
#define RJGT105_CMD_TRNG_GENERATE   0x31u   /* 产生 4 Byte 随机数到 BUFF */
#define RJGT105_CMD_CHIP_ERASE      0x35u   /* EEPROM 片擦除（仅初始化模式） */
#define RJGT105_CMD_AUTH_MAC_GEN    0x55u   /* 主机认证: 生成 8 Byte MAC 到 BUFF */
#define RJGT105_CMD_AUTH_MAC_VERIFY 0xA5u   /* 从机认证: 比较 MAC */
#define RJGT105_CMD_COUNTER_INC     0x69u   /* 芯片计次自增 1 */

/* ==========================================================================
 *  状态码
 * ========================================================================== */
#define RJGT105_STATUS_BUSY         0x00u   /* 操作正在进行 */
#define RJGT105_STATUS_SUCCESS      0x01u   /* 操作成功 */
#define RJGT105_STATUS_FAIL         0x05u   /* 操作失败 */
#define RJGT105_STATUS_LOCKED       0xF5u   /* 计次溢出，系统锁定 */

/* ==========================================================================
 *  EEPROM 页地址
 * ========================================================================== */
#define RJGT105_EEPROM_KEY          0x00u   /* 密钥页 (WO) */
#define RJGT105_EEPROM_UID          0x01u   /* UID 页 (RW*) */
#define RJGT105_EEPROM_PAGE0        0x02u   /* 用户数据页 0 */
#define RJGT105_EEPROM_PAGE1        0x03u   /* 用户数据页 1 */
#define RJGT105_EEPROM_PAGE2        0x04u   /* 用户数据页 2 */
#define RJGT105_EEPROM_PAGE3        0x05u   /* 用户数据页 3 */
#define RJGT105_EEPROM_PAGE4        0x06u   /* 用户数据页 4 */
#define RJGT105_EEPROM_PAGE5        0x07u   /* 用户数据页 5 */
#define RJGT105_EEPROM_PAGE6        0x08u   /* 用户数据页 6 */
#define RJGT105_EEPROM_PAGE7        0x09u   /* 用户数据页 7 */
#define RJGT105_EEPROM_PAGE8        0x0Au   /* 用户数据页 8 */
#define RJGT105_EEPROM_PAGE9        0x0Bu   /* 用户数据页 9 */
#define RJGT105_EEPROM_PAGE10       0x0Cu   /* 用户数据页 10 */
#define RJGT105_EEPROM_RSV          0x0Du   /* 保留页 (RW*) */
#define RJGT105_EEPROM_CNT_VAL      0x0Eu   /* 计数器值 */
#define RJGT105_EEPROM_FUNCTRL      0x0Fu   /* 功能控制页 */

/* ==========================================================================
 *  功能控制页偏移
 * ========================================================================== */
#define RJGT105_FUNCTRL_CNT_VAL     0x00u   /* CNT_VAL 偏移 */
#define RJGT105_FUNCTRL_CNT_TH      0x02u   /* CNT_TH 偏移 */
#define RJGT105_FUNCTRL_PRT_KEY     0x04u   /* PRT_KEY 偏移 */
#define RJGT105_FUNCTRL_PRT_CTRL    0x08u   /* PRT_CTRL 偏移 */
#define RJGT105_FUNCTRL_MODE_SEL    0x09u   /* MODE_SEL 偏移 */

/* ==========================================================================
 *  保护控制值 (PRT_CTRL)
 * ========================================================================== */
#define RJGT105_PRT_SELFDEST_5A     0x5Au   /* 自毁使能 + 全面认证保护 */
#define RJGT105_PRT_SELFDEST_A5     0xA5u   /* 自毁使能 + 仅写保护 */
#define RJGT105_PRT_DISABLE         0x00u   /* 自毁禁止 + 无需认证 */

/* ==========================================================================
 *  命令层 API —— 芯片级原子操作
 *
 *  所有函数以 rsd_pin 为第一参数。
 *  调用者负责互斥（设备框架层通过 mutex 保证）。
 * ========================================================================== */

/**
 * @brief   写数据到 BUFF 缓冲区（寄存器 0x00，16 Byte）。
 *
 * 直接写 BUFF，不触发任何芯片命令。BUFF 为易失缓冲区，
 * 会被命令操作改写（TRNG/MAC 等），断电后内容丢失。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param   p_data      输入数据
 * @param   len         写入大小 (<=16)
 * @return  rt_err_t    RT_EOK 成功，负值为错误码
 */
rt_err_t rjgt105_cmd_buff_write(rt_base_t rsd_pin,
                                const rt_uint8_t *p_data,
                                rt_uint8_t len);

/**
 * @brief   读 BUFF 缓冲区数据（寄存器 0x00，16 Byte）。
 *
 * 直接读 BUFF，不触发任何芯片命令。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param[out] p_data   输出缓冲区
 * @param   len         读取大小 (<=16)
 * @return  rt_err_t    RT_EOK 成功，负值为错误码
 */
rt_err_t rjgt105_cmd_buff_read(rt_base_t rsd_pin,
                               rt_uint8_t *p_data,
                               rt_uint8_t len);

/**
 * @brief   读 EEPROM 页数据。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param   page_addr   页地址 (0x00~0x0F)
 * @param[out] p_data   输出缓冲区
 * @param   size        读取大小 (<=16)
 * @return  rt_err_t    RT_EOK 成功，负值为错误码
 */
rt_err_t rjgt105_cmd_eeprom_read(rt_base_t rsd_pin,
                                  rt_uint8_t page_addr,
                                  rt_uint8_t *p_data,
                                  rt_uint8_t size);

/**
 * @brief   写数据到 EEPROM 页。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param   page_addr   页地址 (0x00~0x0F)
 * @param   p_data      输入数据
 * @param   size        写入大小 (<=16)
 * @return  rt_err_t    RT_EOK 成功，负值为错误码
 */
rt_err_t rjgt105_cmd_eeprom_write(rt_base_t rsd_pin,
                                   rt_uint8_t page_addr,
                                   const rt_uint8_t *p_data,
                                   rt_uint8_t size);

/** 认证成功后在同一会话内读取 EEPROM；不会再次发送唤醒脉冲。 */
rt_err_t rjgt105_cmd_eeprom_read_session(rt_base_t rsd_pin,
                                         rt_uint8_t page_addr,
                                         rt_uint8_t *p_data,
                                         rt_uint8_t size);

/** 认证成功后在同一会话内写入 EEPROM；不会再次发送唤醒脉冲。 */
rt_err_t rjgt105_cmd_eeprom_write_session(rt_base_t rsd_pin,
                                          rt_uint8_t page_addr,
                                          const rt_uint8_t *p_data,
                                          rt_uint8_t size);

/**
 * @brief   产生 4 Byte 真随机数并读出。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param[out] p_rng    4 Byte 输出缓冲区
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_cmd_trng_generate(rt_base_t rsd_pin, rt_uint8_t *p_rng);

/**
 * @brief   主机认证 (MAC Generate 0x55)。
 *
 * 写 IV 到 BUFF → 发 0x55 → 读芯片 MAC。
 * 调用前必须已执行 TRNG (0x31)。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param   iv          4 Byte 初始向量（主机生成）
 * @param[out] mac_out  8 Byte 芯片 MAC 输出
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_cmd_host_auth(rt_base_t rsd_pin,
                                const rt_uint8_t *iv,
                                rt_uint8_t *mac_out);

/** 连续执行 唤醒→TRNG→读 RNG→写 IV→主机认证→读 MAC，避免会话间再次唤醒。 */
rt_err_t rjgt105_cmd_host_auth_exchange(rt_base_t rsd_pin,
                                        const rt_uint8_t *iv,
                                        rt_uint8_t *rng_out,
                                        rt_uint8_t *mac_out);

/**
 * @brief   从机认证 (MAC Verify 0xA5)。
 *
 * 写 IV + MAC 到 BUFF → 发 0xA5 → 读 ES 状态。
 * 调用前必须已执行 TRNG (0x31)。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param   iv          4 Byte 初始向量（主机生成）
 * @param   mac_in      8 Byte 主机 MAC（RC4 计算结果）
 * @return  rt_err_t    RT_EOK 认证通过
 * @retval  -RJGT105_EAUTH 芯片拒绝（MAC 不匹配）
 */
rt_err_t rjgt105_cmd_slave_auth(rt_base_t rsd_pin,
                                 const rt_uint8_t *iv,
                                 const rt_uint8_t *mac_in);

/** 开始连续从机认证会话：唤醒并产生/返回 RNG。 */
rt_err_t rjgt105_cmd_auth_session_begin(rt_base_t rsd_pin,
                                        rt_uint8_t *rng_out);

/** 完成连续从机认证会话：不再次唤醒，立即写 IV+MAC 并验证。 */
rt_err_t rjgt105_cmd_slave_auth_session_finish(rt_base_t rsd_pin,
                                                const rt_uint8_t *iv,
                                                const rt_uint8_t *mac_in);

/**
 * @brief   芯片计次自增 1。
 *
 * @param   rsd_pin     RSD 引脚号
 * @return  rt_err_t    RT_EOK 成功
 *
 * @warning 达阈值后芯片永久锁定，不可逆。
 */
rt_err_t rjgt105_cmd_counter_increment(rt_base_t rsd_pin);

/**
 * @brief   EEPROM 片擦除（仅初始化模式）。
 *
 * @param   rsd_pin     RSD 引脚号
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_cmd_chip_erase(rt_base_t rsd_pin);

/**
 * @brief   读取芯片版本号 (6 Byte)。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param[out] p_version 6 Byte 版本输出缓冲区
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_cmd_read_version(rt_base_t rsd_pin, rt_uint8_t *p_version);

/**
 * @brief   读取芯片 UID (8 Byte)。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param[out] p_uid    8 Byte UID 输出缓冲区
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_cmd_read_uid(rt_base_t rsd_pin, rt_uint8_t *p_uid);

/**
 * @brief   读取计次值 (CNT_VAL + CNT_TH)。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param[out] p_count_val 2 Byte CNT_VAL 输出（可为 NULL）
 * @param[out] p_count_th  2 Byte CNT_TH 输出（可为 NULL）
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_cmd_read_counter(rt_base_t rsd_pin,
                                   rt_uint8_t *p_count_val,
                                   rt_uint8_t *p_count_th);

/**
 * @brief   读取芯片当前状态。
 *
 * @param   rsd_pin     RSD 引脚号
 * @param[out] p_status 状态码输出
 * @return  rt_err_t    RT_EOK 成功
 */
rt_err_t rjgt105_cmd_get_status(rt_base_t rsd_pin, rt_uint8_t *p_status);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_RJGT105_CMD_H__ */
