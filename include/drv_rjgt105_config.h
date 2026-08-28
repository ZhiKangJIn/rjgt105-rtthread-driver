/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone public driver export.
 */

#ifndef __DRV_RJGT105_CONFIG_H__
#define __DRV_RJGT105_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* RSD 引脚配置 -------------------------------------------------------------- */
/* 可在板级头文件（如 CubeMX main.h）中覆盖以下宏定义。                         */
#ifndef RJGT105_RSD_PIN
#define RJGT105_RSD_PIN             GET_PIN(B, 12)  /* RSD 数据引脚 */
#endif

#ifndef RJGT105_RSD_PORT
#define RJGT105_RSD_PORT            GPIOB           /* RSD GPIO 端口 */
#endif

#ifndef RJGT105_RSD_PIN_NUM
#define RJGT105_RSD_PIN_NUM         GPIO_PIN_12     /* RSD GPIO 引脚号 (位掩码) */
#endif

#ifndef RJGT105_RSD_PIN_IDX
#define RJGT105_RSD_PIN_IDX         12u             /* RSD GPIO 位索引 (寄存器直写位域定位) */
#endif

/* 时序配置（基于 168MHz 系统时钟）------------------------------------------- */
/* 默认值适用于已验证的 STM32F4/168 MHz 参考环境。
 * 移植时应重新测量读位 TREH 与帧尾保持时间。 */
#define RJGT105_SYNC_PULSE_US       250u            /* SYNC 同步脉冲宽度 (>=220us) */
#define RJGT105_SYNC_HIGH_US        10u             /* SYNC 尾高缓冲宽度（唤醒缓冲） */
#define RJGT105_WRITE_1_US          7u              /* 写 1 低电平宽度 (TW1L) */
#define RJGT105_WRITE_1_HIGH_US     43u             /* 写 1 高电平宽度（位间无间隙） */
#define RJGT105_WRITE_0_US          50u             /* 写 0 低电平宽度 (TW0L) */
#define RJGT105_WRITE_0_TREH_US     1u              /* 写 0 末高恢复宽度 (TREH 0.5~2us) */
#define RJGT105_READ_DRIVE_US       7u              /* 读位：推挽下拉宽度 (TRL) */

/* 读位采样配置（过采样 3 点采样 + 3 取 2 投票）------------------------------- */
#define RJGT105_READ_SAMPLE1_US     15u             /* 读位采样点 1 */
#define RJGT105_READ_SAMPLE2_US     25u             /* 读位采样点 2 */
#define RJGT105_READ_SAMPLE3_US     35u             /* 读位采样点 3 */
#define RJGT105_READ_PERIOD_US      50u             /* 读位数据窗口（位周期，不含 TREH） */
#define RJGT105_READ_TREH_US        2u              /* 读位末主动拉高恢复 (TREH, 关键!) */
#define RJGT105_READ_TAIL_US        50u             /* 读帧尾: 总线空闲保持宽度 */

/* 命令时序 ------------------------------------------------------------------ */
#define RJGT105_CMD_GAP_US          100u            /* 命令头后芯片反应时间（读帧必需） */

/* 超时配置 ------------------------------------------------------------------ */
#define RJGT105_CMD_TIMEOUT_MS      100u            /* 命令超时 (EEPROM 写入最大 36ms) */
#define RJGT105_AUTH_TIMEOUT_MS     50u             /* 认证超时 */
#define RJGT105_RETRY_MAX           3u              /* 最大重试次数 */
#define RJGT105_SLEEP_TIMEOUT_S     3u              /* 自动休眠超时 (3s) */

/* 缓冲区大小 ---------------------------------------------------------------- */
#define RJGT105_BUFF_SIZE           16u             /* 数据缓冲区 (Byte) */
#define RJGT105_UID_SIZE            8u              /* UID 大小 (Byte) */
#define RJGT105_KEY_SIZE            16u             /* 密钥大小 (Byte) */
#define RJGT105_MAC_SIZE            8u              /* MAC 大小 (Byte) */
#define RJGT105_IV_SIZE             4u              /* 初始向量 (IV) 大小 (Byte) */
#define RJGT105_PAGE_SIZE           16u             /* EEPROM 页大小 (Byte) */
#define RJGT105_PAGE_COUNT          16u             /* EEPROM 总页数 */
#define RJGT105_EEPROM_SIZE         256u            /* EEPROM 总容量 (Byte) */

/* 设备名称 ------------------------------------------------------------------ */
#define RJGT105_DEVICE_NAME         "rjgt105"       /* RT-Thread 设备名 */

#ifdef __cplusplus
}
#endif

#endif /* __DRV_RJGT105_CONFIG_H__ */
