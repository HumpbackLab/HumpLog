# OpenLog for AT32F421

这是一个运行在 `AT32F421x8` 上的 OpenLog 兼容固件工程，目标是兼容旧设备对 SparkFun OpenLog 的串口使用方式，同时将日志写入 SPI SD 卡上的 FAT 文件系统。

## 当前能力

- 兼容常用 OpenLog 命令：`new`、`append`、`write`、`rm`、`size`、`read`、`cat`、`ls`、`md`、`cd`、`sync`、`init`、`reset`、`disk`、`baud`、`set`、`verbose`、`echo`
- 支持 `newlog`、`seqlog`、`command` 三种启动模式
- 使用 `config.txt` 保存兼容配置：`baud,escape,esc#,mode,verb,echo,ignoreRX`
- 使用 `SPI1 + SD SPI + FatFs` 作为存储后端
- 日志模式带有流式写缓冲，降低逐字节刷盘带来的性能损失

## 目录结构

- `project/src/`：主固件源码
- `project/inc/`：工程头文件
- `tests/`：主机侧测试，当前主要覆盖 `openlog_fs`
- `third_party/FatFs/`：FatFs 子模块
- `libraries/`：AT32 CMSIS 与外设驱动

## 硬件连接

当前 SD 卡使用 `SPI1`：

- `PA5`：SCK
- `PA6`：MISO
- `PA7`：MOSI
- `PA4`：SD CS
- `PA9`：USART1 TX
- `PA10`：USART1 RX

## 构建与测试

在仓库根目录执行：

```bash
make host-test
make -B
```

- `make host-test`：使用本机 `gcc` 编译并运行 `openlog_fs` 主机测试
- `make -B`：使用 `arm-none-eabi-gcc` 全量重编译固件，生成 `build/openlog.elf/.hex/.bin`

## 兼容性说明

本工程优先兼容旧 OpenLog 设备的串口协议和 `config.txt` 语义，但仍建议做实板验证，尤其是：

- 高波特率连续记录
- SD 卡品牌差异
- 热插拔、满卡、异常断电恢复

如果你要继续扩展，建议优先关注真实硬件压测，而不是再单纯增加命令分支。
