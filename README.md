# ddcci

一个基于 Linux I2C/DDC/CI 的小工具，用来扫描本机显示器设备，读取显示器 EDID 信息；如果识别到 Dell 显示器，则进一步尝试拼出固件版本。

当前仓库里的可执行文件名为 `DellDevice`，源码入口为 `main.c`。

## 功能概览

- 扫描系统中的 `/dev/iic*` 和 `/dev/i2c*`
- 读取显示器 EDID
- 判断设备厂商是否为 Dell
- 提取显示器型号和序列号
- 对 Dell 显示器通过 DDC/CI 读取控制码 `0xC8`、`0xC9`、`0xFD`
- 对 Dell 显示器根据读取结果组合出固件版本字符串

## 项目结构

- `main.c`：主程序，包含 EDID 读取、Dell 识别、DDC/CI 通信和固件版本解析逻辑
- `i2c-dev.h`：本地引入的 I2C ioctl 相关头文件
- `Makefile`：构建脚本
- `DellDevice`：当前仓库中已有的已编译产物

## 构建

依赖很少，直接使用 `gcc` 编译即可。

```bash
make
```

生成产物：

```bash
./DellDevice
```

清理：

```bash
make clean
```

## 运行环境

建议在 Linux 环境下运行，并满足以下条件：

- 系统存在可访问的 `/dev/i2c-*` 或 `/dev/iic*` 设备节点
- 当前用户对对应 I2C 设备有读写权限
- 显示器和显卡链路支持 DDC/CI
- 若需要读取固件版本，目标显示器需为 Dell，且其 DDC/CI 控制码响应符合当前实现假设

如果没有权限，通常需要使用 `root` 或调整设备访问权限后再运行。

## 使用方式

直接执行程序即可：

```bash
./DellDevice
```

程序会自动枚举系统中的 I2C 设备，并依次尝试读取显示器信息。

对于非 Dell 显示器，程序只读取并输出 EDID 中的型号和序列号，固件版本显示为 `N/A`。

成功时输出类似：

```text
/dev/i2c-3: Model = P2424HEB, Serial = XXXXXXXX, Firmware Version = M3T102
```

非 Dell 显示器输出类似：

```text
/dev/i2c-4: Model = VG27A, Serial = XXXXXXXX, Firmware Version = N/A
```

失败时会输出对应设备的错误信息，例如：

```text
Failed to get monitor info from /dev/i2c-3
```

## 实现说明

程序的核心流程如下：

1. 枚举 `/dev/iic*` 和 `/dev/i2c*`
2. 对每个设备读取 EDID 地址 `0x50`
3. 从 EDID 中解析厂商 ID、型号和序列号
4. 判断厂商 ID 是否为 `DEL`
5. 如果是 Dell，则向 DDC/CI 地址 `0x37` 发送 presence check
6. Dell 设备继续读取控制码：
   - `0xC8`
   - `0xC9`
   - `0xFD`
7. Dell 设备将这些值映射为固件版本字符串；非 Dell 设备仅保留 EDID 信息

其中型号来自 EDID 的 `0xFC` 描述符，序列号来自 `0xFF` 描述符。

## 已知限制

- 非 Dell 显示器只读取 EDID，不会尝试通过 DDC/CI 读取固件版本
- 设备扫描方式依赖 `ls /dev/iic* /dev/i2c* 2>/dev/null`
- `Makefile` 使用最简单的 `gcc -o DellDevice main.c`，未显式添加额外编译选项
- 固件版本解析逻辑是针对特定 Dell 机型规则写死的，不保证适用于所有型号
- 程序会打印较多调试输出，包括完整 EDID dump 和控制码读取过程
- 目前没有命令行参数、单元测试或安装脚本

## 后续可改进方向

- 增加命令行参数，支持指定单个 I2C 设备
- 增加静默模式或结构化输出模式，例如 JSON
- 改进设备枚举方式，避免依赖 shell 命令
- 将调试日志与正式输出分离
- 补充更多型号的固件版本解析规则
- 增加错误码和更稳定的重试逻辑

## 许可说明

仓库中 `i2c-dev.h` 文件头声明其来自 Linux I2C 相关实现，并带有 GPL 许可证说明。若准备对外分发或用于正式项目，建议进一步确认整个仓库的许可证边界和兼容性。
