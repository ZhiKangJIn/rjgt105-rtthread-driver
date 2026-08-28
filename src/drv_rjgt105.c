/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone public driver export.
 */

/**
 * @file    drv_rjgt105.c
 * @brief   RJGT105 RT-Thread 设备框架层实现。
 *
 * 将命令层操作封装为 RT-Thread 标准设备接口 (open/close/read/write/control)。
 * 通过互斥锁保证多线程安全，对外暴露简洁的驱动级 API。
 */

#include "drv_rjgt105.h"
#include "drv_rjgt105_hal.h"
#include "drv_rjgt105_cmd.h"
#include "drv_rjgt105_config.h"

#define DBG_TAG    "drv.rjgt105"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

/* ==========================================================================
 *  静态设备实例（嵌入式单实例模式）
 * ========================================================================== */
static struct rjgt105_device g_rjgt105_dev;

/* ==========================================================================
 *  内部辅助宏
 * ========================================================================== */

/* 将 rt_device 指针还原为 rjgt105_dev_t */
#define DEV_PTR(rtdev)    ((rjgt105_dev_t)(rtdev))

/* 加锁，失败返回 -RJGT105_EBUSY */
#define DEV_LOCK(dev)                                                         \
    do {                                                                      \
        if (rt_mutex_take((dev)->lock, RT_WAITING_FOREVER) != RT_EOK)         \
        {                                                                     \
            return -RJGT105_EBUSY;                                            \
        }                                                                     \
    } while (0)

/* 解锁 */
#define DEV_UNLOCK(dev)   rt_mutex_release((dev)->lock)

/* ==========================================================================
 *  常量时间比较（防时序攻击，MISRA C 兼容）
 * ========================================================================== */

/**
 * @brief   时间恒定字节比较，用于 MAC/密钥比对。
 *
 * @return  RT_TRUE 相等，RT_FALSE 不等
 */
static rt_bool_t rjgt105_constant_time_compare(const rt_uint8_t *a,
                                                const rt_uint8_t *b,
                                                rt_uint8_t len)
{
    rt_uint8_t diff = 0u;
    rt_uint8_t i;

    if ((a == RT_NULL) || (b == RT_NULL))
    {
        return RT_FALSE;
    }

    for (i = 0u; i < len; i++)
    {
        diff |= (a[i] ^ b[i]);
    }

    return (diff == 0u) ? RT_TRUE : RT_FALSE;
}

/* ==========================================================================
 *  RT-Thread 设备操作实现
 * ========================================================================== */

/**
 * @brief   设备初始化（rt_device_init 回调）。
 *
 * 执行: HAL 初始化 → 唤醒芯片 → 读版本号自检 → 缓存 UID
 */
static rt_err_t _rjgt105_init(rt_device_t rtdev)
{
    rjgt105_dev_t dev = DEV_PTR(rtdev);
    rt_err_t ret;
    rt_uint8_t version[6u];

    /* HAL 初始化 */
    ret = rjgt105_hal_init(dev->rsd_pin);
    if (ret != RT_EOK)
    {
        LOG_E("HAL 初始化失败: %d", (int)ret);
        dev->state = RJGT105_STATE_ERROR;
        return ret;
    }

    /* 唤醒芯片 */
    ret = rjgt105_hal_wakeup(dev->rsd_pin);
    if (ret != RT_EOK)
    {
        LOG_E("唤醒芯片失败");
        dev->state = RJGT105_STATE_ERROR;
        return ret;
    }

    /* 冷启动预热: 实芯片冷启动后前 1~3 帧可能静默（全 FF），预读一帧作废。 */
    ret = rjgt105_cmd_read_version(dev->rsd_pin, version);
    if (ret != RT_EOK)
    {
        LOG_W("预热读取失败, err=%d（忽略，继续自检）", (int)ret);
    }

    /* 自检：读取版本号并验证是否为 "GT1015" */
    {
        const rt_uint8_t expected[6u] = {'G', 'T', '1', '0', '1', '5'};

        ret = rjgt105_cmd_read_version(dev->rsd_pin, version);
        if (ret != RT_EOK)
        {
            LOG_E("自检失败: 无法读取版本号, err=%d", (int)ret);
            dev->state = RJGT105_STATE_ERROR;
            return -RJGT105_ECHIP;
        }

        if (rjgt105_constant_time_compare(version, expected, 6u) != RT_TRUE)
        {
            LOG_E("自检失败: 版本不匹配 [%02X %02X %02X %02X %02X %02X]，期望 GT1015",
                  (unsigned int)version[0], (unsigned int)version[1],
                  (unsigned int)version[2], (unsigned int)version[3],
                  (unsigned int)version[4], (unsigned int)version[5]);
            dev->state = RJGT105_STATE_ERROR;
            return -RJGT105_ECHIP;
        }
    }

    LOG_I("芯片版本: GT1015 (已验证)");

    /* 缓存 UID */
    ret = rjgt105_cmd_read_uid(dev->rsd_pin, dev->uid);
    if (ret != RT_EOK)
    {
        LOG_W("缓存 UID 失败, err=%d（不影响初始化）", (int)ret);
    }

    dev->state = RJGT105_STATE_IDLE;
    LOG_I("RJGT105 设备初始化完成");

    return RT_EOK;
}

/**
 * @brief   设备打开（rt_device_open 回调）。
 */
static rt_err_t _rjgt105_open(rt_device_t rtdev, rt_uint16_t oflag)
{
    rjgt105_dev_t dev = DEV_PTR(rtdev);

    if (dev->state == RJGT105_STATE_UNINIT)
    {
        return -RJGT105_ERROR;
    }

    /* 唤醒芯片（可能处于休眠状态） */
    rjgt105_hal_wakeup(dev->rsd_pin);

    dev->state = RJGT105_STATE_IDLE;
    LOG_D("设备已打开");

    return RT_EOK;
}

/**
 * @brief   设备关闭（rt_device_close 回调）。
 */
static rt_err_t _rjgt105_close(rt_device_t rtdev)
{
    rjgt105_dev_t dev = DEV_PTR(rtdev);

    /* 释放总线到空闲态 */
    rjgt105_hal_release_bus(dev->rsd_pin);

    dev->state = RJGT105_STATE_SLEEP;
    LOG_D("设备已关闭");

    return RT_EOK;
}

/**
 * @brief   读 EEPROM 用户数据页（rt_device_read 回调）。
 *
 * 偏移量 = 页地址 (0x02~0x0C)，缓冲区 >= size 字节。
 */
static rt_ssize_t _rjgt105_read(rt_device_t rtdev,
                                 rt_off_t pos,
                                 void *buffer,
                                 rt_size_t size)
{
    rjgt105_dev_t dev = DEV_PTR(rtdev);
    rt_err_t ret;
    rt_uint8_t page;
    rt_uint8_t len;

    if (buffer == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* pos: 逻辑页地址，映射到物理页 0x02~0x0C */
    page = (rt_uint8_t)(pos + RJGT105_EEPROM_PAGE0);
    if ((page < RJGT105_EEPROM_PAGE0) || (page > RJGT105_EEPROM_PAGE10))
    {
        LOG_E("页地址越界: 0x%02X", (unsigned int)page);
        return -RT_EINVAL;
    }

    len = (size > RJGT105_PAGE_SIZE) ? RJGT105_PAGE_SIZE : (rt_uint8_t)size;

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_eeprom_read(dev->rsd_pin, page, (rt_uint8_t *)buffer, len);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return (ret == RT_EOK) ? (rt_ssize_t)len : (rt_ssize_t)ret;
}

/**
 * @brief   写 EEPROM 用户数据页（rt_device_write 回调）。
 *
 * 偏移量 = 页地址 (0x02~0x0C)，缓冲区 >= size 字节。
 */
static rt_ssize_t _rjgt105_write(rt_device_t rtdev,
                                  rt_off_t pos,
                                  const void *buffer,
                                  rt_size_t size)
{
    rjgt105_dev_t dev = DEV_PTR(rtdev);
    rt_err_t ret;
    rt_uint8_t page;
    rt_uint8_t len;

    if (buffer == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* pos: 逻辑页地址，映射到物理页 0x02~0x0C */
    page = (rt_uint8_t)(pos + RJGT105_EEPROM_PAGE0);
    if ((page < RJGT105_EEPROM_PAGE0) || (page > RJGT105_EEPROM_PAGE10))
    {
        LOG_E("页地址越界: 0x%02X", (unsigned int)page);
        return -RT_EINVAL;
    }

    len = (size > RJGT105_PAGE_SIZE) ? RJGT105_PAGE_SIZE : (rt_uint8_t)size;

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_eeprom_write(dev->rsd_pin, page,
                                    (const rt_uint8_t *)buffer, len);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return (ret == RT_EOK) ? (rt_ssize_t)len : (rt_ssize_t)ret;
}

/**
 * @brief   设备控制（rt_device_control / ioctl 回调）。
 */
static rt_err_t _rjgt105_control(rt_device_t rtdev, int cmd, void *args)
{
    rjgt105_dev_t dev = DEV_PTR(rtdev);
    rt_err_t ret = RT_EOK;

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;

    switch (cmd)
    {
    case RJGT105_CTRL_GET_UID:
        /* 返回已缓存的 UID（8 Byte） */
        if (args != RT_NULL)
        {
            rt_memcpy((rt_uint8_t *)args, dev->uid, RJGT105_UID_SIZE);
        }
        else
        {
            ret = -RT_EINVAL;
        }
        break;

    case RJGT105_CTRL_GET_STATUS:
        if (args != RT_NULL)
        {
            ret = rjgt105_cmd_get_status(dev->rsd_pin, (rt_uint8_t *)args);
        }
        else
        {
            ret = -RT_EINVAL;
        }
        break;

    case RJGT105_CTRL_CHIP_ERASE:
        ret = rjgt105_cmd_chip_erase(dev->rsd_pin);
        break;

    case RJGT105_CTRL_WAKEUP:
        ret = rjgt105_hal_wakeup(dev->rsd_pin);
        break;

    case RJGT105_CTRL_SYNC:
        ret = rjgt105_hal_sync(dev->rsd_pin);
        break;

    case RJGT105_CTRL_AUTH_GENERATE:
        /* args: rt_uint8_t[12] = { iv[4] (in), mac[8] (out) } */
        if (args != RT_NULL)
        {
            rt_uint8_t *buf = (rt_uint8_t *)args;
            ret = rjgt105_cmd_host_auth(dev->rsd_pin, &buf[0], &buf[RJGT105_IV_SIZE]);
        }
        else
        {
            ret = -RT_EINVAL;
        }
        break;

    case RJGT105_CTRL_AUTH_VERIFY:
        /* args: rt_uint8_t[12] = { iv[4] (in), mac[8] (in) } */
        if (args != RT_NULL)
        {
            rt_uint8_t *buf = (rt_uint8_t *)args;
            ret = rjgt105_cmd_slave_auth(dev->rsd_pin, &buf[0],
                                          &buf[RJGT105_IV_SIZE]);
        }
        else
        {
            ret = -RT_EINVAL;
        }
        break;

    case RJGT105_CTRL_TRNG_GENERATE:
        /* args: 4 Byte 输出缓冲区 */
        if (args != RT_NULL)
        {
            ret = rjgt105_cmd_trng_generate(dev->rsd_pin, (rt_uint8_t *)args);
        }
        else
        {
            ret = -RT_EINVAL;
        }
        break;

    case RJGT105_CTRL_COUNTER_INC:
        ret = rjgt105_cmd_counter_increment(dev->rsd_pin);
        break;

    case RJGT105_CTRL_GET_COUNTER:
        /* args: 指向 rt_uint16_t[2] = {CNT_VAL, CNT_TH} */
        if (args != RT_NULL)
        {
            ret = rjgt105_cmd_read_counter(dev->rsd_pin,
                                            (rt_uint8_t *)args,
                                            (rt_uint8_t *)args + 2u);
        }
        else
        {
            ret = -RT_EINVAL;
        }
        break;

    case RJGT105_CTRL_SELF_TEST:
    {
        /* 简单自检：读版本号，检查是否匹配 "GT1015" */
        rt_uint8_t ver[6u];
        const rt_uint8_t expected[6u] = {'G', 'T', '1', '0', '1', '5'};

        ret = rjgt105_cmd_read_version(dev->rsd_pin, ver);
        if (ret != RT_EOK)
        {
            break;
        }

        if (rjgt105_constant_time_compare(ver, expected, 6u) != RT_TRUE)
        {
            LOG_E("版本不匹配: %c%c%c%c%c%c",
                  ver[0], ver[1], ver[2], ver[3], ver[4], ver[5]);
            ret = -RJGT105_ECHIP;
        }
        break;
    }

    default:
        LOG_E("未知控制命令: 0x%02X", (unsigned int)cmd);
        ret = -RT_EINVAL;
        break;
    }

    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return ret;
}

/* ==========================================================================
 *  设备操作表
 * ========================================================================== */
#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops rjgt105_ops =
{
    .init    = _rjgt105_init,
    .open    = _rjgt105_open,
    .close   = _rjgt105_close,
    .read    = _rjgt105_read,
    .write   = _rjgt105_write,
    .control = _rjgt105_control,
};
#endif /* RT_USING_DEVICE_OPS */

/* ==========================================================================
 *  公开 API
 * ========================================================================== */

/**
 * @brief   注册 RJGT105 设备到 RT-Thread 设备框架。
 *
 * 分配并初始化设备句柄、互斥锁，注册到内核。
 * 注册后需调用 rjgt105_init() 或 rt_device_init() 完成硬件初始化。
 *
 * @param   device_name   设备名称（如 "rjgt105"）
 * @param   rsd_pin       RSD 数据引脚号（GET_PIN 宏）
 * @return  rt_err_t      RT_EOK 成功，其他值失败
 */
rt_err_t rjgt105_register(const char *device_name, rt_base_t rsd_pin)
{
    rjgt105_dev_t dev = &g_rjgt105_dev;
    rt_err_t ret;

    RT_ASSERT(device_name != RT_NULL);

    /* 初始化设备结构体 */
    rt_memset(dev, 0, sizeof(struct rjgt105_device));

    dev->rsd_pin = rsd_pin;
    dev->state   = RJGT105_STATE_UNINIT;

    /* 创建互斥锁 */
    dev->lock = rt_mutex_create("rjgt105_mtx", RT_IPC_FLAG_FIFO);
    if (dev->lock == RT_NULL)
    {
        LOG_E("创建互斥锁失败");
        return -RT_ENOMEM;
    }

    /* 注册 RT-Thread 设备 */
    dev->parent.type    = RT_Device_Class_Char;
    dev->parent.rx_indicate = RT_NULL;
    dev->parent.tx_complete = RT_NULL;

#ifdef RT_USING_DEVICE_OPS
    dev->parent.ops = &rjgt105_ops;
#else
    dev->parent.init    = _rjgt105_init;
    dev->parent.open    = _rjgt105_open;
    dev->parent.close   = _rjgt105_close;
    dev->parent.read    = _rjgt105_read;
    dev->parent.write   = _rjgt105_write;
    dev->parent.control = _rjgt105_control;
#endif /* RT_USING_DEVICE_OPS */

    dev->parent.user_data = RT_NULL;

    ret = rt_device_register(&(dev->parent), device_name,
                              RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE);
    if (ret != RT_EOK)
    {
        LOG_E("设备注册失败: %d", (int)ret);
        rt_mutex_delete(dev->lock);
        dev->lock = RT_NULL;
        return ret;
    }

    LOG_I("RJGT105 设备 \"%s\" 注册完成 (RSD pin=%ld)",
          device_name, (long)rsd_pin);

    return RT_EOK;
}

/**
 * @brief   初始化 RJGT105 设备（硬件初始化 + 自检）。
 *
 * 等价于调用 rt_device_init()。
 */
rt_err_t rjgt105_init(rjgt105_dev_t dev)
{
    RT_ASSERT(dev != RT_NULL);

    if (dev->state != RJGT105_STATE_UNINIT)
    {
        LOG_W("设备已初始化，跳过");
        return RT_EOK;
    }

    return _rjgt105_init(&(dev->parent));
}

rt_err_t rjgt105_device_lock(rjgt105_dev_t dev)
{
    if ((dev == RT_NULL) || (dev->lock == RT_NULL))
    {
        return -RT_EINVAL;
    }
    return rt_mutex_take(dev->lock, RT_WAITING_FOREVER);
}

void rjgt105_device_unlock(rjgt105_dev_t dev)
{
    if ((dev != RT_NULL) && (dev->lock != RT_NULL))
    {
        (void)rt_mutex_release(dev->lock);
    }
}

/**
 * @brief   读 EEPROM 用户数据页（带锁版本）。
 */
rt_ssize_t rjgt105_read_page(rjgt105_dev_t dev, rt_uint8_t page_addr,
                              rt_uint8_t *data, rt_uint8_t size)
{
    rt_err_t ret;
    rt_uint8_t len;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(data != RT_NULL);

    if (size > RJGT105_PAGE_SIZE)
    {
        size = RJGT105_PAGE_SIZE;
    }
    len = size;

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_eeprom_read(dev->rsd_pin, page_addr, data, len);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return (ret == RT_EOK) ? (rt_ssize_t)len : (rt_ssize_t)ret;
}

/**
 * @brief   写 EEPROM 用户数据页（带锁版本）。
 */
rt_ssize_t rjgt105_write_page(rjgt105_dev_t dev, rt_uint8_t page_addr,
                               const rt_uint8_t *data, rt_uint8_t size)
{
    rt_err_t ret;
    rt_uint8_t len;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(data != RT_NULL);

    if (size > RJGT105_PAGE_SIZE)
    {
        size = RJGT105_PAGE_SIZE;
    }
    len = size;

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_eeprom_write(dev->rsd_pin, page_addr, data, len);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return (ret == RT_EOK) ? (rt_ssize_t)len : (rt_ssize_t)ret;
}

/**
 * @brief   获取芯片 UID（带锁版本，返回缓存值）。
 */
rt_err_t rjgt105_get_uid(rjgt105_dev_t dev, rt_uint8_t *uid_buff)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(uid_buff != RT_NULL);

    DEV_LOCK(dev);
    rt_memcpy(uid_buff, dev->uid, RJGT105_UID_SIZE);
    DEV_UNLOCK(dev);

    return RT_EOK;
}

/**
 * @brief   MAC 双向认证——主机认证 + 从机认证（带锁版本）。
 *
 * 本函数完成数据手册 §8.1 步骤 4-5 和 §8.2 全部步骤。
 * 调用者必须先完成 TRNG 获取和主机 MAC 计算：
 *
 *   调用顺序:
 *     1. rjgt105_trng_generate(dev, rng)   → 获取 4 Byte 随机数
 *     2. host_mac = RC4(key, rng)          → 应用层用密钥 + 随机数算出 8 Byte MAC
 *     3. rjgt105_auth_mac(dev, host_mac, chip_mac)  ← 本函数
 *     4. 比较 (host_mac == chip_mac) ?     → 应用层比较，相等则主机认证通过
 *
 *   本函数内部流程:
 *     a. 发 0x55 (MAC Generate) → 芯片内部计算 MAC → 读取芯片 MAC
 *     b. 写 host_mac 到 BUFF
 *     c. 发 0xA5 (MAC Verify)  → 芯片比较 MAC → 读取 ES 状态
 *
 * @param   dev         设备句柄
 * @param   host_mac    主机 MAC (8 Byte)，由应用层用 RC4(key, TRNG随机数) 计算
 * @param[out] chip_mac 芯片 MAC (8 Byte)，用于应用层比对。可为 NULL 跳过返回
 * @return  rt_err_t    RT_EOK 双向认证均通过
 * @retval  -RJGT105_EAUTH  从机认证失败（芯片拒绝了主机 MAC）
 * @retval  -RJGT105_EIO    通信错误
 *
 * @note    TRNG 必须在调用本函数之前执行，芯片需要 BUFF 中的随机数来计算 MAC。
 */
rt_err_t rjgt105_host_auth(rjgt105_dev_t dev,
                            const rt_uint8_t *iv,
                            rt_uint8_t *mac_out)
{
    rt_err_t ret;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(iv != RT_NULL);
    RT_ASSERT(mac_out != RT_NULL);

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_host_auth(dev->rsd_pin, iv, mac_out);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return ret;
}

rt_err_t rjgt105_slave_auth(rjgt105_dev_t dev,
                             const rt_uint8_t *iv,
                             const rt_uint8_t *mac_in)
{
    rt_err_t ret;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(iv != RT_NULL);
    RT_ASSERT(mac_in != RT_NULL);

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_slave_auth(dev->rsd_pin, iv, mac_in);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return ret;
}

/**
 * @brief   产生 4 Byte 真随机数并读出（带锁版本）。
 */
rt_err_t rjgt105_trng_generate(rjgt105_dev_t dev, rt_uint8_t *rng_buff)
{
    rt_err_t ret;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(rng_buff != RT_NULL);

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_trng_generate(dev->rsd_pin, rng_buff);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return ret;
}

/**
 * @brief   芯片计次自增 1（带锁版本）。
 */
rt_err_t rjgt105_counter_increment(rjgt105_dev_t dev)
{
    rt_err_t ret;

    RT_ASSERT(dev != RT_NULL);

    DEV_LOCK(dev);
    dev->state = RJGT105_STATE_BUSY;
    ret = rjgt105_cmd_counter_increment(dev->rsd_pin);
    dev->state = RJGT105_STATE_IDLE;
    DEV_UNLOCK(dev);

    return ret;
}
