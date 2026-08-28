/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone public driver export.
 */

/**
 * @file    drv_rjgt105.h
 * @brief   RJGT105 加密芯片驱动主头文件（外部引用入口）。
 *
 * 包含设备结构体、错误码、控制命令和公开 API 声明。
 * 应用层通过 RT-Thread 标准设备接口（rt_device_xxx）访问。
 */

#ifndef __DRV_RJGT105_H__
#define __DRV_RJGT105_H__

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "drv_rjgt105_config.h"
#include "drv_rjgt105_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 驱动错误码（负值，与 RT-Thread 错误码兼容）-------------------------------- */
/* 宏定义为正值，return 时加负号 → 符合 RT-Thread 错误返回惯例（return -RT_XXX） */
#define RJGT105_EOK                 RT_EOK              /*  0: 成功 */
#define RJGT105_ERROR               RT_ERROR            /* -1: 通用错误 */
#define RJGT105_ETIMEOUT            RT_ETIMEOUT         /* -2: 超时 */
#define RJGT105_EFULL               RT_EFULL            /* -3: 缓冲区满 */
#define RJGT105_EEMPTY              RT_EEMPTY           /* -4: 缓冲区空 */
#define RJGT105_ENOMEM              RT_ENOMEM           /* -5: 内存不足 */
#define RJGT105_ENOSYS              RT_ENOSYS           /* -6: 功能未实现 */
#define RJGT105_EBUSY               RT_EBUSY            /* -7: 设备忙 */
#define RJGT105_EIO                 RT_EIO              /* -8: I/O 错误 */
#define RJGT105_EINVAL              RT_EINVAL           /* -10: 无效参数 */
#define RJGT105_EAUTH               20                  /* -20: 认证失败 */
#define RJGT105_EPROTECT            21                  /* -21: 保护区域访问拒绝 */
#define RJGT105_ECHIP               22                  /* -22: 芯片通信异常 */

/* 设备控制命令（rt_device_control 使用）------------------------------------- */
#define RJGT105_CTRL_GET_UID        0x01u   /* 获取芯片 UID */
#define RJGT105_CTRL_GET_STATUS     0x02u   /* 获取芯片状态 */
#define RJGT105_CTRL_CHIP_ERASE     0x03u   /* EEPROM 片擦除（仅初始化模式） */
#define RJGT105_CTRL_WAKEUP         0x05u   /* 唤醒芯片 */
#define RJGT105_CTRL_SYNC           0x06u   /* 帧同步 */
#define RJGT105_CTRL_AUTH_GENERATE  0x07u   /* 主机认证：生成 MAC */
#define RJGT105_CTRL_AUTH_VERIFY    0x08u   /* 从机认证：验证 MAC */
#define RJGT105_CTRL_SELF_TEST      0x09u   /* 自检测试 */
#define RJGT105_CTRL_SET_KEY        0x0Au   /* 设置加密密钥 */
#define RJGT105_CTRL_SET_PROTECT    0x0Bu   /* 设置保护控制 (PRT_KEY/PRT_CTRL) */
#define RJGT105_CTRL_GET_COUNTER    0x0Cu   /* 获取计次值 */
#define RJGT105_CTRL_TRNG_GENERATE  0x0Du   /* 产生随机数 */
#define RJGT105_CTRL_COUNTER_INC    0x0Eu   /* 计次自增 1 */
#define RJGT105_CTRL_READ_PAGE      0x0Fu   /* 读 EEPROM 页 */

/* 设备状态枚举 -------------------------------------------------------------- */
enum rjgt105_device_state
{
    RJGT105_STATE_UNINIT = 0,           /* 未初始化 */
    RJGT105_STATE_IDLE,                  /* 空闲 */
    RJGT105_STATE_BUSY,                  /* 忙 */
    RJGT105_STATE_SLEEP,                 /* 睡眠 */
    RJGT105_STATE_ERROR                  /* 错误 */
};

/* 设备句柄 ------------------------------------------------------------------ */
struct rjgt105_device
{
    struct rt_device    parent;                          /* RT-Thread 设备基类 */
    rt_base_t           rsd_pin;                         /* RSD 引脚号 */
    rt_uint8_t          state;                           /* 设备状态 */
    rt_uint8_t          tx_buff[RJGT105_BUFF_SIZE];      /* 发送缓冲区 */
    rt_uint8_t          rx_buff[RJGT105_BUFF_SIZE];      /* 接收缓冲区 */
    rt_uint8_t          uid[RJGT105_UID_SIZE];           /* 芯片 UID 缓存 */
    rt_mutex_t          lock;                            /* 互斥锁（多线程安全） */
};

typedef struct rjgt105_device *rjgt105_dev_t;

/* 公开 API ----------------------------------------------------------------- */

/**
 * @brief   注册 RJGT105 设备到 RT-Thread 设备框架。
 *
 * @param   device_name   设备名称
 * @param   rsd_pin       RSD 数据引脚（GET_PIN 宏）
 * @return  rt_err_t      RT_EOK 成功，其他值失败
 */
rt_err_t rjgt105_register(const char *device_name, rt_base_t rsd_pin);

/**
 * @brief   初始化 RJGT105 设备（硬件初始化 + 自检）。
 *
 * @param   dev           设备句柄
 * @return  rt_err_t      RT_EOK 成功
 */
rt_err_t rjgt105_init(rjgt105_dev_t dev);

/** 获取设备总线互斥锁；用于跨多条命令的认证/事务会话。支持同线程递归。 */
rt_err_t rjgt105_device_lock(rjgt105_dev_t dev);

/** 释放由 rjgt105_device_lock() 获取的设备总线互斥锁。 */
void rjgt105_device_unlock(rjgt105_dev_t dev);

/**
 * @brief   读 EEPROM 用户数据页。
 *
 * @param   dev           设备句柄
 * @param   page_addr     页地址 (0x02~0x0C)
 * @param   data          输出缓冲区 (>=16 Byte)
 * @param   size          读取大小 (<=16 Byte)
 * @return  rt_ssize_t    实际读取字节数，负值为错误码
 */
rt_ssize_t rjgt105_read_page(rjgt105_dev_t dev, rt_uint8_t page_addr,
                              rt_uint8_t *data, rt_uint8_t size);

/**
 * @brief   写 EEPROM 用户数据页。
 *
 * @param   dev           设备句柄
 * @param   page_addr     页地址 (0x02~0x0C)
 * @param   data          输入数据
 * @param   size          写入大小 (<=16 Byte)
 * @return  rt_ssize_t    实际写入字节数，负值为错误码
 */
rt_ssize_t rjgt105_write_page(rjgt105_dev_t dev, rt_uint8_t page_addr,
                               const rt_uint8_t *data, rt_uint8_t size);

/**
 * @brief   获取芯片 UID。
 *
 * @param   dev           设备句柄
 * @param   uid_buff      8 Byte 输出缓冲区
 * @return  rt_err_t      RT_EOK 成功
 */
rt_err_t rjgt105_get_uid(rjgt105_dev_t dev, rt_uint8_t *uid_buff);

/**
 * @brief   主机认证——MAC Generate (0x55)。
 *
 * 芯片用内部 key + TRNG随机数 + IV 计算 MAC 并返回。
 *
 *   调用顺序:
 *     1. rjgt105_trng_generate(dev, rng)   → 获取 4 Byte 随机数
 *     2. 主机生成 4 Byte IV
 *     3. rjgt105_host_auth(dev, iv, mac)   ← 本函数 (0x55)
 *     4. host_mac = RC4(key, rng, iv)      → 应用层计算 MAC
 *     5. 比较 host_mac == mac              → 主机认证结果
 *
 * @param   dev           设备句柄
 * @param   iv            4 Byte 初始向量（主机生成）
 * @param[out] mac_out    8 Byte 芯片 MAC 输出
 * @return  rt_err_t      RT_EOK 成功
 */
rt_err_t rjgt105_host_auth(rjgt105_dev_t dev,
                            const rt_uint8_t *iv,
                            rt_uint8_t *mac_out);

/**
 * @brief   从机认证——MAC Verify (0xA5)。
 *
 * 芯片验证主机提供的 IV + MAC。
 *
 *   调用顺序:
 *     1. rjgt105_trng_generate(dev, rng)   → 获取 4 Byte 随机数
 *     2. 主机生成 4 Byte IV
 *     3. mac = RC4(key, rng, iv)           → 应用层计算 8 Byte MAC
 *     4. rjgt105_slave_auth(dev, iv, mac)  ← 本函数 (0xA5)
 *
 * @param   dev           设备句柄
 * @param   iv            4 Byte 初始向量（主机生成）
 * @param   mac_in        8 Byte 主机 MAC（RC4 计算结果）
 * @return  rt_err_t      RT_EOK 芯片验证通过
 * @retval  -RJGT105_EAUTH 芯片拒绝（MAC 不匹配）
 */
rt_err_t rjgt105_slave_auth(rjgt105_dev_t dev,
                             const rt_uint8_t *iv,
                             const rt_uint8_t *mac_in);

/**
 * @brief   产生 4 Byte 真随机数并读出。
 *
 * 执行流程:
 *   1. 发送 0x31 命令 → 芯片产生 4 Byte TRNG 存入 BUFF[0:3]
 *   2. 读取 BUFF[0:3] → 将随机数返回给调用者
 *
 * @param   dev           设备句柄
 * @param   rng_buff      4 Byte 输出缓冲区
 * @return  rt_err_t      RT_EOK 成功
 */
rt_err_t rjgt105_trng_generate(rjgt105_dev_t dev, rt_uint8_t *rng_buff);

/**
 * @brief   芯片计次自增 1。
 *
 * @param   dev           设备句柄
 * @return  rt_err_t      RT_EOK 成功
 *
 * @warning 当 CNT_VAL 达到 CNT_TH 时，芯片会自动执行片擦除并永久锁定
 *         （返回状态 0xF5），此过程不可逆。使用前务必确认阈值设置正确。
 */
rt_err_t rjgt105_counter_increment(rjgt105_dev_t dev);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_RJGT105_H__ */
