# HumpLog

这是由HumpbackLab推出的 OpenLog 兼容固件工程，目标是兼容旧设备对 SparkFun OpenLog 的串口使用方式，基于串口记录log至SPI SD卡中，
同时支持更高波特率(1M)和更高吞吐率。

## 当前能力

- 兼容常用 OpenLog 命令：`new`、`append`、`write`、`rm`、`size`、`read`、`cat`、`ls`、`md`、`cd`、`sync`、`init`、`reset`、`disk`、`baud`、`set`、`verbose`、`echo`
- 支持 `newlog`、`seqlog`、`command` 三种启动模式
- 使用 `config.txt` 保存兼容配置：`baud,escape,esc#,mode,verb,echo,ignoreRX`
- 使用 `SPI1 + SD SPI + FatFs` 作为存储后端
- 日志模式带有流式写缓冲，降低逐字节刷盘带来的性能损失
- 当前实板最高可靠记录速率约为 `74 KB/s @ 1000000 baud`（`256KB` 连续写入校验通过）

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
- `PB0`：状态 LED（低电平有效）

## LED 状态指示

| 模式 | 数据流 | LED 行为 |
|------|--------|---------|
| `newlog` / `append` | 无数据（空闲 >100ms） | **常亮** |
| `newlog` / `append` | 数据写入中 | **快闪**（~20 Hz） |
| `command` / `write` | — | **灭** |
| `baud` / `set` 菜单 | — | **灭** |

## config.txt 默认值

首次上电时，固件在 SD 卡根目录自动生成 `config.txt`，格式为：

```
115200,26,0,0,1,1,1
baud,escape,esc#,mode,verb,echo,ignoreRX
```

| 字段 | 默认值 | 含义 |
|------|--------|------|
| `baud` | `115200` | 串口波特率（300–1000000） |
| `escape` | `26` (0x1A) | ESCAPE 字符的十进制值 |
| `esc#` | `0` | ESCAPE 触发次数（0=禁用 ESCAPE，设为 3 启用） |
| `mode` | `0` | 启动模式：0=newlog, 1=seqlog, 2=command |
| `verb` | `1` | 详细错误信息：0=简短(!), 1=完整 |
| `echo` | `1` | 命令回显：0=关, 1=开 |
| `ignoreRX` | `1` | 上电时忽略 RX 噪声：0=关, 1=开 |

可用 `set` 命令在线修改，或直接编辑 `config.txt` 后执行 `init` 重新加载。
为防止在记录log时非预期地进入命令模式，默认禁用了ESCAPE。如果需要执行压测，需要将esc#设置为3，否则无法进入命令模式。

## 构建与测试

在仓库根目录执行：

```bash
make host-test
make -B
```

- `make host-test`：使用本机 `gcc` 编译并运行 `openlog_fs` 主机测试
- `make -B`：使用 `arm-none-eabi-gcc` 全量重编译固件，生成 `build/openlog.elf/.hex/.bin`
- 烧录参考命令：
```bash
/home/ncer/at32_openocd/bin/openocd -s /home/ncer/at32_openocd/scripts -f /home/ncer/openlog/openlog/at32f421.cfg -c "program /home/ncer/openlog/openlog/build/openlog.elf verify reset exit"
```

## 兼容性说明

当前文件系统路径缓存上限为 `32B`，因此更适合将日志文件直接放在根目录，或仅使用很浅的目录层级；如果目录路径过深，可能导致路径缓存失败。

当前文件系统最多支持`64`个节点。当节点数超过该数量之后，会影响命令模式下的`ls`、`new`等命令的返回结果，但不会影响NewLog模式下的记录。
