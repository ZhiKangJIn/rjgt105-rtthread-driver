# RJGT105 RT-Thread Driver

面向 RJGT105 安全芯片的独立 RT-Thread 驱动。仓库只包含芯片通信、命令和 RT-Thread 设备层，不包含具体产品状态机、业务数据布局、生产密钥或公司内部测试资料。

## 功能

- RSD 单总线 GPIO 通信与微秒级时序
- EEPROM 页读写、UID、TRNG 和状态查询
- 主机/从机认证命令
- 硬件计数器查询与递增
- RT-Thread 设备注册和互斥保护

## 目录

```text
include/    公开头文件与可覆盖配置
src/        HAL、命令层和设备层实现
SConscript  RT-Thread SCons 构建入口
```

## 集成

将仓库加入 RT-Thread BSP，在父级 `SConscript` 中调用本目录的 `SConscript`。板级工程需要提供 RT-Thread、`board.h`、`drv_gpio.h`、CMSIS GPIO 定义以及 `GET_PIN()`。

默认引脚配置位于 `include/drv_rjgt105_config.h`。建议在板级配置中覆盖 `RJGT105_RSD_PIN`、`RJGT105_RSD_PORT`、`RJGT105_RSD_PIN_NUM` 和 `RJGT105_RSD_PIN_IDX`，不要直接修改驱动协议逻辑。

```c
#include "drv_rjgt105.h"

int secure_chip_init(void)
{
    return rjgt105_register(RJGT105_DEVICE_NAME, RJGT105_RSD_PIN);
}
```

## 安全提示

- 仓库不提供密钥。密钥必须由调用方通过安全存储或安全注入机制管理。
- `rjgt105_counter_increment()` 会改变芯片的不可逆计数状态。
- 片擦除、保护位和计数阈值相关操作应在量产工具中增加人工确认与状态校验。
- 默认时序来自 STM32F4/168 MHz 环境，移植到其他主频或 MCU 时必须用逻辑分析仪复核。

## 文档边界

设计说明、实物测试记录、内部路径与产品集成资料保存在本地 `private-docs/`，该目录已被 `.gitignore` 排除，不会上传。

## 许可证

源文件中的 SPDX 标识适用于对应文件。仓库级许可证需由代码权利人确认后另行添加。
