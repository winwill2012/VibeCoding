# OLED Clock — ESP32 桌面时钟

基于 **ESP32** 与 **SH1106 128×64 OLED** 的桌面时钟项目，支持主菜单、时钟、日历、天气、倒计时与秒表，通过三键操作与可选 Web 配置天气城市。

## 功能概览

| 功能     | 说明 |
|----------|------|
| **时钟** | NTP 网络对时（阿里云 NTP），大数字时间 + 秒，顶部栏显示日期、WiFi、电量 |
| **日历** | 月历视图，左右键切换年月，支持 2020–2030 |
| **天气** | 心知天气 API，显示城市、温度、天气描述与图标，支持 Web 配置城市 ID |
| **计时** | 四位数字倒计时（分:秒），结束后蜂鸣器节奏提醒 |
| **秒表** |  lap 式秒表，支持开始/暂停/继续 |

## 硬件

- **MCU**：ESP32-WROOM-32E（240MHz，4MB Flash）
- **显示屏**：SH1106 128×64，硬件 I2C
- **按键**：三键（左 / 中 / 右），支持单击、双击、长按
- **其他**：电池 ADC 电量、蜂鸣器（倒计时结束）

### 引脚

| 功能     | GPIO |
|----------|------|
| I2C SDA  | 21   |
| I2C SCL  | 22   |
| 左键     | 5    |
| 中键     | 19   |
| 右键     | 18   |
| 蜂鸣器   | 23   |
| 电池 ADC | 34   |

## 软件环境

- **PlatformIO**（推荐），Arduino 框架
- **依赖**：U8g2（`olikraus/U8g2`）

### 编译与烧录

在项目根目录（即本 `oled-clock` 目录）执行：

```bash
# 编译
pio run

# 烧录到 ESP32
pio run -t upload

# 串口监视器（115200）
pio device monitor
```

## 配置

### WiFi（智能配网）

首次上电或未保存过 WiFi 时，设备会开放热点 **OLEDClock**（无密码）。用手机连接该热点后，一般会自动弹出配网页；若未弹出，浏览器访问 **http://192.168.4.1**，在页面中选择你家路由器并输入密码，保存后设备会连接该 WiFi 并记住，下次上电自动连网。

- 若已保存过 WiFi：上电后自动连接，无需再配网。
- 更换路由器或需重新配网：设备连网后，用手机访问 **http://<设备IP>/**，在页面底部点击「清除 WiFi 并重新配网」，设备重启后会再次开放 **OLEDClock** 热点，按上述步骤重新选择新 WiFi 即可。

### 天气城市

- 默认使用心知天气城市 ID **昆明**（`kunming`）。
- 设备连上 WiFi 后，用手机/电脑连接同一 WiFi，浏览器访问 **`http://<设备IP>/`**，可修改城市 ID（如 `beijing`、`shanghai`），提交后会自动保存并用于天气页。

## 操作说明

- **主菜单**：左/右键切换高亮项，中键进入；在子页面中键返回主菜单。
- **时钟**：进入时自动 NTP 对时，顶部栏显示日期、WiFi 状态、电量。
- **日历**：左/右键切换月或年（视当前焦点），中键返回。
- **天气**：仅查看，中键返回；城市在 Web 页配置。
- **计时**：左/右键移动光标，中键修改数字或开始/暂停，结束后蜂鸣器响约 10 秒。
- **秒表**：中键开始/暂停/继续，左/右键可作 lap 等（视固件实现）。

## 项目结构

```
oled-clock/
├── platformio.ini       # PlatformIO 配置与依赖
├── src/
│   ├── main.cpp         # 入口：setup/loop、按键与状态机
│   ├── app_state.cpp    # 应用状态与各页共享变量
│   ├── display.cpp      # OLED 顶栏、电池、时间位图、开机/NTP 提示
│   ├── menu_screen.cpp  # 主菜单绘制
│   ├── clock_screen.cpp # 时钟页与 NTP 同步
│   ├── calendar_screen.cpp # 日历月历
│   ├── weather_screen.cpp  # 天气页与心知 API
│   ├── timer_screen.cpp    # 倒计时与蜂鸣
│   ├── stopwatch_screen.cpp # 秒表
│   ├── web_config.cpp   # Web 天气城市配置
│   ├── buttons.cpp      # 三键检测（单击/双击/长按）
│   └── bitmap.h         # 大数字/小数字等位图
├── include/
│   ├── app_state.h
│   ├── display.h
│   ├── menu_screen.h
│   ├── clock_screen.h
│   ├── calendar_screen.h
│   ├── weather_screen.h
│   ├── timer_screen.h
│   ├── stopwatch_screen.h
│   ├── web_config.h
│   ├── buttons.h
│   └── wifi_config.h    # WiFi SSID/密码（需自行修改）
├── .cursor/             # 编辑器/规则（可选）
└── README.md            # 本说明
```

## 许可证与致谢

- 心知天气：[心知天气 API](https://www.seniverse.com/)
- 显示库：U8g2
- NTP：阿里云 NTP（ntp.aliyun.com）

---

使用前请先配置 `include/wifi_config.h` 中的 WiFi，烧录后首次进入时钟页将进行 NTP 对时；需要更改天气城市时，通过浏览器访问设备 IP 即可。
