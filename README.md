# ESP32-S3 触控答题演示系统

基于 **ESP-IDF + LVGL v8** 的嵌入式触控应用，运行在 ESP32-S3 平台，适配 3.49 英寸触控 LCD。系统提供登录、拉取题库、选择题作答、结果统计与上传的完整流程，可用于课堂演示、设备交互样例或轻量答题终端原型。

---

## 1. 项目特性

- 登录鉴权：通过 HTTP 调用后端登录接口，支持从响应体或响应头提取 token。
- 在线题库：从服务端下载题目，支持解析题干、选项、答案和试卷标识。
- 触控答题：A/B/C/D 选项作答，逐题提交，自动切换下一题。
- 结果页展示：显示分数、正确率、错题信息，并支持重新测试。
- Wi-Fi 自动连接：设备启动后自动连接预设 Wi-Fi，断线自动重连。
- 屏幕与触控适配：基于 AXS15231B 面板驱动、I2C 触控读取和 LVGL 刷新任务。

---

## 2. 硬件与软件环境

### 硬件

- ESP32-S3 开发板
- 3.49 英寸触控 LCD（AXS15231B）
- 可用 Wi-Fi 网络
- （可选）实体按键与电源控制电路（项目中已包含按键与 IO 扩展器逻辑）

### 软件

- ESP-IDF（建议 v5.x，按项目实际环境）
- CMake / Ninja（ESP-IDF 默认工具链）
- Python 依赖（由 ESP-IDF 安装脚本管理）

---

## 3. 目录结构

```text
.
├── main/
│   ├── main.cpp          # 硬件初始化、LVGL 驱动接入、任务创建
│   ├── app_network.c     # Wi-Fi 初始化与连接等待
│   ├── app_flow.c        # 页面流程：登录 -> 答题
│   ├── login_app.c       # 登录界面与登录请求逻辑
│   ├── quiz_app.c        # 题库下载、答题流程、提交结果、结果展示
│   └── user_config.h     # GPIO、分辨率、Wi-Fi、服务端接口等配置
├── components/           # 板级驱动组件（I2C、按键、背光等）
├── partitions.csv        # 分区表
└── CMakeLists.txt        # ESP-IDF 项目入口
```

---

## 4. 快速开始

> 以下命令在 Linux/macOS 终端中执行，Windows 可在 ESP-IDF PowerShell 环境执行等效命令。

1. 安装并激活 ESP-IDF 环境。
2. 进入项目目录：

```bash
cd /workspace/esp32_app
```

3. （首次）设置目标芯片：

```bash
idf.py set-target esp32s3
```

4. 编译项目：

```bash
idf.py build
```

5. 烧录并查看串口日志：

```bash
idf.py -p <YOUR_PORT> flash monitor
```

---

## 5. 配置说明

主要配置集中在 `main/user_config.h`：

- 显示与触控引脚（LCD QSPI / Touch I2C）
- 屏幕分辨率与旋转方向（`Rotated`）
- Wi-Fi 账号密码（`APP_WIFI_SSID` / `APP_WIFI_PASS`）
- 服务端地址与端口（`APP_SERVER_HOST` / `APP_SERVER_PORT`）
- 接口路径：
  - 登录：`APP_API_LOGIN_PATH`
  - 题库：`APP_API_QUESTIONS_PATH`
  - 提交：`APP_API_SUBMIT_PATH`

建议将网络与服务端配置改为私有环境值，避免将真实凭据提交到公共仓库。

---

## 6. 运行流程

1. 上电启动，初始化背光、LCD、触控、按钮、LVGL 任务。
2. 显示登录界面，使用数字滚轮 + OK/DEL 输入账号密码。
3. 点击 Sign In，发起登录请求并获取 token。
4. 登录成功后进入主页，点击 Download 下载题库。
5. 点击 Start Test 进入答题，逐题选择选项并 Submit。
6. 完成后自动统计结果并尝试上传。
7. 在 View Results 查看得分、正确率和错题列表。

---

## 7. 常见问题（FAQ）

### Q1：登录一直失败怎么办？

- 检查 Wi-Fi 是否可连通。
- 检查 `APP_SERVER_HOST`、`APP_SERVER_PORT`、`APP_API_LOGIN_PATH` 是否正确。
- 通过串口日志确认 HTTP 状态码和返回体。

### Q2：触控坐标异常或方向不对？

- 检查 `Rotated` 配置。
- 检查显示分辨率宏与触控坐标映射逻辑是否一致。

### Q3：下载题目成功但无法开始测试？

- 检查题库 JSON 结构是否符合当前解析逻辑。
- 确认服务端确实返回了可用题目列表与题目字段。

---

## 8. 后续可扩展方向

- 增加 HTTPS/TLS 支持与证书校验
- 将 Wi-Fi 与服务端参数改为运行时配置页面
- 支持多题型（判断题、填空题）
- 增加本地缓存与断网恢复
- 接入 OTA 升级能力

---

## 9. 许可证

当前仓库未声明开源许可证。若计划公开发布，建议补充 `LICENSE` 文件并明确授权方式。
