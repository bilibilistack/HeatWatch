# HeatWatch 生理热应激算法说明书 / Physiological Heat Strain Algorithm Documentation

本文件详细阐述了 HeatWatch 智能可穿戴设备中搭载的动态生理热应激和核心体温评估算法。该算法结合了环境物理量与人体的生理信号，用于在极端工作环境下（如矿井）提供更高精度的中暑风险预警。

This document elaborates on the dynamic physiological heat strain and core body temperature estimation algorithm embedded in the HeatWatch smart wearable device. The algorithm integrates ambient environmental metrics with physiological signals to provide high-precision heatstroke warnings in extreme working conditions (e.g., mines).

---

## 1. 算法核心模块与数学模型 / Core Modules & Mathematical Models

算法由三个相互交织的数学模型构成，每 10 秒（$\Delta t = 10s$）动态执行一次：
The algorithm consists of three interconnected mathematical models, executed dynamically every 10 seconds ($\Delta t = 10s$):

```
+------------------+     +-------------------------+     +-----------------------+
|  Environment &   | --> | Dynamic Core Temp (Tc)  | --> | Physiological Strain  |
|  Wet-Bulb Temp   |     |  Differential Equation  |     |   Index (Moran PSI)   |
+------------------+     +-------------------------+     +-----------------------+
                                                                     |
                                                                     v
                                                         +-----------------------+
                                                         | Cumulative Heat Strain|
                                                         |   (Leaky Integrator)  |
                                                         +-----------------------+
```

### 1.1 核心体温估算微分方程 / Core Body Temperature ($T_c$) Differential Equation

人体核心体温的动态变化（$\frac{dT_c}{dt}$）被建模为体内代谢产热（由心率驱动）与体表及外部散热（由核心与皮肤温差驱动）之间的动态热平衡：
The dynamic rate of change of human core body temperature ($\frac{dT_c}{dt}$) is modeled as a thermal balance between metabolic heat generation (driven by heart rate) and effective heat dissipation to the skin/environment (driven by core-to-skin temperature gradient):

$$\frac{dT_c}{dt} = \alpha \cdot HR_{input} - \beta \cdot \eta \cdot (T_c - T_{skin})$$

#### A. 离散差分形式 / Discretized Difference Equation (for $\Delta t = 10s$):
$$T_c(t) = T_c(t-1) + \Delta t \cdot \left[ \alpha \cdot HR_{input}(t) - \beta \cdot \eta \cdot (T_c(t-1) - T_{skin}(t)) \right]$$

#### B. 散热受阻惩罚系数 / Evaporative Heat Dissipation Penalty ($\eta$):
湿球温度（$T_{wet}$）是衡量汗液蒸发冷却能力的物理极限。当 $T_{wet}$ 升高并逼近核心温度时，排汗散热能力（$\eta$）呈线性衰减，最终在极端情况下（湿球温度 $>35^\circ\text{C}$）降至仅剩 $10\%$ 的极微弱物理辐射散热：
The environmental wet-bulb temperature ($T_{wet}$) represents the thermodynamic limit for evaporative cooling (sweating efficiency). As $T_{wet}$ approaches core temperature, the heat dissipation capacity factor ($\eta$) scales down linearly, down to a minimum of $10\%$ (representing residual radiative cooling):

$$\eta = \text{constrain}\left(\frac{T_c - T_{wet}}{37.0 - 20.0}, 0.1, 1.0\right)$$

#### C. 参数定义 / Parameter Definitions:
* **$\alpha = 0.000286$**：每次心跳引起的代谢热量增加系数 / Metabolic heat generation coefficient per heartbeat.
* **$\beta = 0.005$**：基准核心至体表的传热阻力系数 / Baseline core-to-skin heat transfer coefficient.
* **$HR_{input}$**：输入心率。当心率处于 $[60, 180]\text{ bpm}$ 的合法区间时，使用实际心率；若由于传感器脱落或噪声导致心率异常，则自动使用静息心率常数 $70\text{ bpm}$ 代替 / Input heart rate. Uses actual HR if within $[60, 180]\text{ bpm}$, otherwise defaults to $70\text{ bpm}$.
* **$T_{skin}$**：由红外温度传感器测得的体表皮肤温度 / Skin temperature measured by the infrared thermometer.
* **初始化 / Initialization**：核心体温默认初始化为健康的 $37.0^\circ\text{C}$，体表温度默认初始化为健康的 $33.0^\circ\text{C}$。后续根据生理与环境状态通过微分方程进行连续平滑调整。这避免了红外传感器未就绪或未佩戴（读取低于 $30.0^\circ\text{C}$ 被过滤，退回到默认体表温度 $33.0^\circ\text{C}$）时，因初始体温梯度过大导致开机或静置时误触发警报 / $T_c$ is initialized to $37.0^\circ\text{C}$ and $T_{skin}$ to $33.0^\circ\text{C}$ on startup. Subsequent changes are continuous and smooth, preventing false alarm spikes from uninitialized or unworn sensors.
* **安全限制 / Safety Constraints**：为防止数值积分累加或极端的传感器噪声导致数值失真，核心温度被钳制在合适的生理学范围 $[35.0^\circ\text{C}, 42.0^\circ\text{C}]$ 内 / To prevent runaway mathematical integration, $T_c$ is clamped within a physiologically realistic range of $[35.0^\circ\text{C}, 42.0^\circ\text{C}]$.

---

### 1.2 Moran 生理应激指数 / Moran's Physiological Strain Index ($PSI$)

Moran 指数结合体温和心率来量化即时生理压力，范围为 $0 \sim 10$（数值越大，生理负荷越高）：
The Moran PSI quantifies instantaneous physiological strain on a scale from $0$ to $10$ by combining core temperature and heart rate:

#### A. 双模式自适应计算 / Dual-Mode Adaptive Calculation:

* **若心率有效（$60 \le HR \le 180\text{ bpm}$） / When Heart Rate is Valid:**
  $$PSI = 5 \cdot \left(\frac{T_c - 37.0}{2.5}\right) + 5 \cdot \left(\frac{HR - 70}{110}\right)$$
* **若心率无效（如传感器脱落） / When Heart Rate is Invalid (fallback):**
  $$PSI = 10 \cdot \left(\frac{T_c - 37.0}{2.5}\right)$$

#### B. 夹紧边界 / Clamping Boundaries:
* $PSI$ 结果强制约束在 $[0.0, 10.0]$ 区间内。任何分子差值项如果为负，则该项对 $PSI$ 的贡献被夹紧为 $0$ / $PSI$ is constrained to $[0.0, 10.0]$. Negative term contributions are clamped to $0$.

---

### 1.3 累积热应激 / Cumulative Heat Strain ($CHS$)

为了追踪工人在高温环境下长时间作业累积的热疲劳和热剂量，引入带恢复衰减的漏积分器（Leaky Integrator）：
To track long-term accumulated thermal fatigue (heat dose) under prolonged heat exposure, we introduce a Leaky Integrator for Cumulative Heat Strain ($CHS$):

$$\frac{d(CHS)}{dt} = k_1 \cdot \max(0, PSI - 3.0) - k_2 \cdot CHS$$

#### A. 离散差分形式 / Discretized Difference Equation (for $\Delta t = 10s$):
$$CHS(t) = CHS(t-1) + \Delta t \cdot \left[ k_1 \cdot \max(0, PSI(t) - 3.0) - k_2 \cdot CHS(t-1) \right]$$

#### B. 参数定义 / Parameter Definitions:
* **$k_1 = 0.1$**：应激累积增益 / Accumulation gain.
* **$k_2 = 0.001$**：静息恢复/热消散速率（时间常数约为 16.6 分钟） / Recovery and cooling decay rate (time constant $\approx 16.6$ minutes).
* **物理机制 / Physical Mechanism**：只有当即时生理应激指数 $PSI > 3.0$ 时，身体才开始累积热应激；当工人进入凉爽环境或休息导致 $PSI \le 3.0$ 时，累积值会以 $k_2$ 速率缓慢衰减释放 / Heat dose accumulates only when $PSI > 3.0$. It decays gradually when the worker cools down or rests ($PSI \le 3.0$).

---

## 2. 生理-环境联合风险评定 / Joint Risk Assessment

设备通过**环境危险指标（WBGT/DI）**与**生理负荷指标（$T_c$ / PSI / CHS）**的“双通道触发”机制判断警报等级：
The device determines the risk level via a "dual-channel trigger" combining **environmental hazards (WBGT/DI)** and **physiological strain ($T_c$ / PSI / CHS)**:

| 风险等级 / Risk Level | 环境通道条件 / Environment Conditions | 生理通道条件 / Physiology Conditions | 警报动作 / Alarm Action |
| :--- | :--- | :--- | :--- |
| **CRITICAL** (严重) | `effectiveWBGT >= 30.0` OR `DI >= 32.0` | `T_c >= 38.5` OR `PSI >= 7.5` OR `CHS >= 50.0` | 模式 2 强震动 / Pattern 2 Strong Vibration |
| **WARNING** (警告) | `effectiveWBGT >= 23.0` OR `DI >= 24.0` | `T_c >= 37.8` OR `PSI >= 4.0` OR `CHS >= 20.0` | 模式 1 短震动 / Pattern 1 Short Vibration |
| **NORMAL** (正常) | 均不满足 / Neither | 均不满足 / Neither | 无震动，重置静音 / None, reset mute |

> [!NOTE]
> 任何一个通道超标都会触发相应的风险等级。生理通道能防止工人因高强度作业或个体虚弱而在环境温度看似安全（如 $22^\circ\text{C}$）的情况下发生热应激中暑。
> Either channel meeting the threshold triggers the respective risk level. The physiological channel prevents heat stroke under mild environment conditions if metabolic demand or physical strain is excessively high.

---

## 3. 下行控制与校准机制 / Downlink Command Processing & Calibration

### 3.1 10分钟强制静音保护 / 10-Minute Force Mute Protection
当收到云端下发的 `0xF3` 警报重置指令时：
1. **时间戳记录**：系统记录当前静音启动时间戳：`muteStartTime = millis()`。
2. **屏蔽报警**：标志位设为有效：`alarmMuted = true`，在此期间屏蔽声光/震动警报。
3. **溢出安全复位**：在每 10 秒的环境评估中，如果 `millis() - muteStartTime >= 600,000 ms (10 minutes)`，系统自动将 `alarmMuted` 复位为 `false`，从而恢复报警功能。使用无符号长整型减法运算，天然免疫 49.7 天的 `millis()` 溢出回零问题。

When receiving the downlink `0xF3` command:
1. `muteStartTime` is recorded using `millis()`, and `alarmMuted` is set to `true` (muting physical alarms).
2. **Mute Timeout Check**: If `millis() - muteStartTime >= 10 minutes` (checked every 10 seconds), `alarmMuted` is automatically reset to `false`. Unsigned subtraction handles the 49.7-day timer overflow naturally.

### 3.2 外部校准值接入 / External Calibration Input
ThingsBoard 平台可下发 `0xF2` 类型的环境校准指令：
1. **载荷解析**：校准指令中包含 2 字节的环境湿球温度（$\times 10$ 放大精度），设备接收后自动解析为浮点数并写入 `externalWBGT`。
2. **时效期机制**：更新 `lastExternalWBGTTime = millis()`。在 30 分钟（`EXTERNAL_WBGT_TIMEOUT`）的时效内，设备会以该外部校准值替代本地 BME280 计算的 `localWBGT`。
3. **指令隔离**：接收到 `0xF3` 静音指令时，**不再**执行 `externalWBGT = 0`，使其物理校准状态在整个 30 分钟生存期内保持有效，避免因复位报警导致校准丢失。

ThingsBoard sends external calibrated WBGT via `0xF2`. The device parses a 2-byte raw value (in tenths of a degree), updates `externalWBGT`, and marks the time `lastExternalWBGTTime`. External calibrations override local estimates for 30 minutes. Receiving `0xF3` **no longer clears** this calibration.

---

## 4. 硬件电源管理与电池监测 / Hardware Power Management & Battery Monitoring

### 4.1 AXP192 电源配置与 GPS 关闭 / AXP192 & GPS Power Control
T-Beam 板载的 AXP192 是一款高度集成的电源管理芯片（PMIC）。由于可穿戴设备要求在井下长达 24 小时以上的工作续航，固件在 `batteryBegin()` 初始化中进行了以下低功耗配置：
* **关闭 GPS 芯片电源**：通过 `axp.setPowerOutPut(AXP192_LDO3, false)` 物理截断 LDO3 供电轨道。NEO-M8N GPS 模块在未定位时电流高达 $60 \sim 80\text{mA}$，关闭 GPS 可将整机待机功耗降低约 $70\%$。
* **开启 LoRa 芯片电源**：通过 `axp.setPowerOutPut(AXP192_LDO2, true)` 开启 LDO2，为 SX1276/RFM95 射频芯片提供稳定的 $3.3\text{V}$ 工作电压。
* **启动 ADC 传感器通道**：注册并使能 AXP192 内部的电压和电流 ADC 模数转换器，用于实时读取电池电压（`AXP202_BATT_VOL_ADC1`）和 USB 接入状态（`AXP202_VBUS_VOL_ADC1`）。

The AXP192 PMIC is configured at startup to optimize power consumption:
* **GPS Disabled**: LDO3 (`AXP192_LDO3`) powering the NEO-M8N GPS chip is shut down (`false`), saving $60 \sim 80\text{mA}$ of inactive current draw.
* **LoRa Enabled**: LDO2 (`AXP192_LDO2`) powering the SX1276 LoRa module is enabled (`true`).
* **ADC Registration**: Enables internal ADCs for monitoring battery voltage and VBUS plug-in status.

### 4.2 锂电池电量非线性映射 / Piecewise Linear Battery Mapping
单节锂离子电池的放电曲线（电压 vs 剩余容量）具有强烈的非线性特征。为了向平台反馈真实的剩余百分比，固件在 `liionPctFromVoltage()` 中实现了分段线性映射：
* $V_{cell} \ge 4.20\text{V} \rightarrow 100\%$ （满电状态）
* $V_{cell} \in [3.95\text{V}, 4.20\text{V}] \rightarrow 80\% \sim 100\%$ （高平缓放电区）
* $V_{cell} \in [3.80\text{V}, 3.95\text{V}] \rightarrow 20\% \sim 80\%$ （中段平缓放电区）
* $V_{cell} \in [3.60\text{V}, 3.80\text{V}] \rightarrow 5\% \sim 20\%$ （陡峭放电前期）
* $V_{cell} \in [3.30\text{V}, 3.60\text{V}] \rightarrow 0\% \sim 5\%$ （电量即将耗尽）
* $V_{cell} \le 3.30\text{V} \rightarrow 0\%$ （欠压保护截止点）

Li-ion battery voltage discharge is highly non-linear. The `liionPctFromVoltage()` function maps voltages to capacity percentages using a piecewise linear lookup table to prevent misleading sudden drops.

### 4.3 瞬间功耗滤波与 USB 覆盖 / EMA Filtering & USB Override
* **指数移动平均滤波 (EMA)**：在 LoRa 发送射频脉冲瞬间，功率放大器（PA）会突发拉取高达 $120\text{mA}$ 的电流，这会导致电池端产生短暂的电压跌落（Voltage Dip）。为防止假电量低警报，固件在 `smoothBatteryVoltage()` 中使用一阶无限脉冲响应（IIR/EMA）滤波器对电压进行滤波：
  $$V_{smooth}(t) = 0.85 \cdot V_{smooth}(t-1) + 0.15 \cdot V_{raw}(t)$$
* **USB 电源检测**：通过 `axp.isVBUSPlug()` 判断是否接通外部供电。若处于供电状态，为防止锂电池充电时电压虚高影响百分比判断，直接强置并汇报电量为 `100%`。

* **EMA Filter**: Peak currents up to $120\text{mA}$ during LoRa transmission cause transient battery voltage drops. An IIR/EMA filter with $\alpha = 0.15$ stabilizes the reading: $V_{smooth}(t) = 0.85 \cdot V_{smooth}(t-1) + 0.15 \cdot V_{raw}(t)$.
* **USB Check**: If `axp.isVBUSPlug()` returns true (charging), the percent defaults to $100\%$ to prevent charging-voltage skew.

---

## 5. LoRaWAN 通信与网络配置 / LoRaWAN Communication & Network Configuration

### 5.1 ABP 激活模式与参数调优 / ABP Mode & Parameter Tuning
设备采用 **ABP (Activation By Personalization)** 激活模式，具有连接速度快、无需双向握手、适合井下断网后重新上线等优点：
* **物理频段与子信道**：通过 `LMIC_selectSubBand(1)` 锁死在美国 US915 标准的 Sub-band 1（信道 8 至 15）。这是匹配 ThingsBoard 专属网关下发与上行的物理配置。
* **时钟误差容差窗口 (Clock Error)**：使用 `LMIC_setClockError(MAX_CLOCK_ERROR * 80 / 100)` 将时钟容差窗口放大至 $80\%$。ESP32 运行 RTOS 时存在任务切换调度微秒级延时，放大误差窗口能确保在下行接收窗口 RX1/RX2 开启时准确捕捉网关的下行帧。
* **射频配置**：上行绑定 SF7 扩频因子（`DR_SF7`），发射功率限制在 $14\text{dBm}$；下行 RX2 接收路径指定为高灵敏度的 SF9 模式（`DR_SF9`），RX1 接收延迟配置为 `1` 秒。

The device operates under LoRaWAN **ABP Activation Mode**:
* **Sub-band**: Locks onto US915 Channel Block 1 (channels 8-15) via `LMIC_selectSubBand(1)`.
* **Clock Error**: Expands window tolerance to $80\%$ via `LMIC_setClockError(...)` to compensate for RTOS scheduling jitter and avoid missing downlinks.
* **RF Config**: SF7 (`DR_SF7`) at $14\text{dBm}$ for TX. SF9 (`DR_SF9`) for RX2, with an RX1 delay of 1 second.

### 5.2 20字节紧凑二进制数据载荷与传输优化 / 20-Byte Binary Telemetry Payload & Transmission Optimization

为了在极其有限的信道资源（Duty-cycle）中塞入所有关键的实时生理与环境数据，并最大限度地节省 ThingsBoard 的数据库存储空间和前端轮询带宽，系统采用 **20字节高度压缩的二进制载荷** 并在云端和前端进行了 **Key 简短化优化（Shorten Keys）**。

To transmit all critical real-time physiological and environmental metrics within tight LoRaWAN airtime (duty-cycle) limits while minimizing ThingsBoard storage and frontend poll bandwidth, the system implements a **20-byte binary payload** and **Shorten Keys** optimization.

#### A. 20字节二进制数据结构 / 20-Byte Binary Data Structure:
* **`[0 - 1] (int16_t)`**：空气温度 $T_{air} \times 10$（有符号，精度 $0.1^\circ\text{C}$，云端 Key: `temp` / `ambientTemp`）
* **`[2] (uint8_t)`****：空气湿度 $RH \times 2$（无符号，精度 $0.5\%$，云端 Key: `hum` / `humidity`）
* **`[3 - 4] (int16_t)`**：有效温湿指数 $W_{eff} \times 10$（有符号，精度 $0.1^\circ\text{C}$，云端 Key: `wetBulb` / `HeatStressIndex`）
* **`[5] (uint8_t)`**：当前危险等级 `currentRisk`（$0=$ NORMAL, $1=$ WARNING, $2=$ CRITICAL，云端 Key: `risk` / `riskLevel`）
* **`[6] (uint8_t)`**：剩余电量百分比 $0 \sim 100$（云端 Key: `battery`）
* **`[7] (uint8_t)`**：状态掩码（Bit 0: 跌倒标志 `fall`；Bit 1: 外部 USB 供电中 `usb`；Bit 2: 系统运行中）
* **`[8] (uint8_t)`**：平均心率 $BPM$（云端 Key: `heartRateAvg`）
* **`[9 - 10] (int16_t)`**：本地湿球温度 $LocW \times 10$（有符号，精度 $0.1^\circ\text{C}$，云端 Key: `locW`）
* **`[11 - 12] (int16_t)`**：外部气象下发 WBGT $ExtW \times 10$（有符号，精度 $0.1^\circ\text{C}$，云端 Key: `extW`）
* **`[13 - 14] (int16_t)`**：体温 $Sk \times 10$（有符号，精度 $0.1^\circ\text{C}$，云端 Key: `skinTemp`）
* **`[15 - 16] (int16_t)`**：核心体温 $Tc \times 10$（有符号，精度 $0.1^\circ\text{C}$，云端 Key: `tc`）
* **`[17] (uint8_t)`**：生理应激指数 $PSI \times 10$（无符号，精度 $0.1$，云端 Key: `psi`）
* **`[18 - 19] (uint16_t)`**：累积热应变蓄积量 $CHS \times 10$（无符号，精度 $0.1$，云端 Key: `chs`）

#### B. 存储与传输优化 (Shorten Keys):
为防止庞大的 JSON 键名塞满云端时序数据库（Timeseries Database）并拖慢前端的网络数据轮询响应，ThingsBoard 在解析 20 字节时做了如下 Key 缩短处理，Next.js 前端在读取到时会自动进行匹配：
- 本地湿球温度 $\to$ **`locW`**
- 外部气象湿球温度 $\to$ **`extW`**
- 体表皮肤温度 $\to$ **`skT`** (仅在 JSON 嵌套中简写为 `skT`)
- 估算核心温度 $\to$ **`tc`**
- 生理应激指数 $\to$ **`psi`**
- 累积热应变蓄积量 $\to$ **`chs`**

---

## 6. 跌倒检测与硬件中断逻辑 / Fall Detection & Hardware Interrupt Logic

### 6.1 硬件中断阈值与引脚映射 / Hardware Interrupt & Pin Mapping
加速度计 LIS3DH 被配置为硬件自触发中断模式，以降低 ESP32 频繁通过 I2C 轮询的功耗开销：
* **引脚绑定**：LIS3DH 物理 INT1 引脚连接至 ESP32 GPIO25。配置为带上拉输入，并在上升沿触发硬件 ISR 中断服务程序 `onLisInterrupt()`。
* **高-G 冲击阈值配置**：在初始化时，通过 I2C 写入 LIS3DH 控制寄存器：
  * 向 `0x36`（`INT1_THS`）写入 `40`：配置动作触发阈值约为 $2.5\text{G}$。
  * 向 `0x37`（`INT1_DURATION`）写入 `5`：配置触发脉宽需持续至少 $50\text{ms}$，过滤日常手臂挥动的瞬时冲击。
  * 向 `0x30`（`INT1_CFG`）写入 `0x2A`：使能三轴的高-G 检测。
  * 向 `0x22`（`CTRL_REG3`）写入 `0x40`：将 LIS3DH 内部的中断信号路由到外置 INT1 物理引脚。

LIS3DH is configured as a hardware interrupt source routed to GPIO25 on rising edges:
* **Registers**: Write `40` to `0x36` ($\approx 2.5\text{G}$ threshold) and `5` to `0x37` ($50\text{ms}$ debounce window) to generate physical interrupt pulses on INT1, ignoring rapid arm movements.

### 6.2 跌倒状态机判定算法 / Fall Detection State Machine
跌倒检测结合了**高-G 物理冲击（Impact）**、**失重低-G 跌落（Freefall）**以及**跌落后无动作静止（Stillness）与姿态翻转（Posture Change）**等多维判据：
1. **失重判定**：监测合加速度 SVM（Signal Vector Magnitude）：
   $$SVM = \frac{\sqrt{A_x^2 + A_y^2 + A_z^2}}{G}$$
   若任意时刻 $SVM < 0.70\text{G}$，则记录当前时间戳 `lastLowGTime = now`。
2. **状态跳转 IDLE -> CONFIRMING**：当硬件中断触发或软件读到 $SVM > 2.7\text{G}$（表明产生碰撞），并且检测到刚才 $900\text{ms}$ 内曾发生过低-G 失重过程，系统进入 `STATE_CONFIRMING` 确认期。
3. **静止确认**：在长达 $8$ 秒的确认窗口内，系统统计人体的静止占比：
   * 静止定义：$SVM \in [0.85\text{G}, 1.15\text{G}]$ 且 $\Delta SVM \le 0.08\text{G}$。
   * 静止率要求：`stillRatio >= 30%`。
   * 末端平静区：末尾连续平静时间 `quietTail >= 1200ms`。
4. **姿态角判定**：计算静止后的三轴平均重力倾角与碰撞前基准倾角之间的夹角：
   $$\theta = \arccos\left(\frac{\vec{A}_{base} \cdot \vec{A}_{post}}{\|\vec{A}_{base}\| \cdot \|\vec{A}_{post}\|}\right)$$
   倾角变化 $\ge 8^\circ$ 作为跌倒辅助证据。
5. **确认与旁路直发**：若条件达成判定为真实跌倒，`pendingFallAlert` 被置为 `true`，触发 3 次震动警报，并通过调用 `triggerImmediateSend()` **强行打断现有的 LoRa 发送定时器，立即插队上传跌倒警报帧**。

The state machine implements multivariable fall analysis:
* Transitions from `IDLE` to `CONFIRMING` upon hardware interrupt or $SVM > 2.7\text{G}$ with a preceding freefall.
* Evaluates stillness ratio ($\ge 30\%$, with $\Delta SVM \le 0.08\text{G}$) and baseline posture angular deviation ($\ge 8^\circ$) within an 8-second window.
* Confirmed falls trigger immediate LoRaWAN transmissions (`triggerImmediateSend()`) to bypass normal logging intervals.

---

## 7. FreeRTOS 双核并发架构 / FreeRTOS Dual-Core Concurrency Architecture

### 7.1 双核任务调度划分 / Task Allocation & Core Pinning
ESP32 配备双对称 Tensilica Xtensa 核心（Core 0 和 Core 1）。固件在实时操作系统（FreeRTOS）框架下，根据任务的**实时性高低**在双核间进行硬性划分：

```
+------------------------------------+      +------------------------------------+
|               CORE 0               |      |               CORE 1               |
+------------------------------------+      +------------------------------------+
|  TaskEnvironment (Priority 1)      |      |  TaskBioMotion (Priority 2)        |
|  - BME280 Ambient Read (10s)       |      |  - MAX30102 PPG Sampling (20ms)    |
|  - MLX90614 Skin Temp Read (10s)   |      |  - LIS3DH Fall State Machine       |
|  - determineHeatStress() Math      |      |  - loop() [LMIC LoRaWAN Runloop]   |
+------------------------------------+      +------------------------------------+
                                       \  /
                                 [ i2cMutex Lock ]
```

* **`TaskBioMotion` (Core 1, 优先级 2)**：
  * **采样间隔**：$20\text{ms}$（$50\text{Hz}$ 高频执行）。
  * **工作职责**：实时读取 MAX30102 心率血氧传感器的 IR 原始信号以提取脉搏特征，并在 $20\text{ms}$ 精度的时空尺度上执行跌倒状态机的姿态与加速度特征确认。
  * **调度设计**：分配较高的优先级 `2`，以防止被其他后台计算或 LoRa 状态机阻塞导致丢失心率红外信号。
* **`TaskEnvironment` (Core 0, 优先级 1)**：
  * **采样间隔**：$10\text{s}$（低频执行）。
  * **工作职责**：读取环境温湿度 BME280、体表红外 MLX90614，并在获取新参数后，集中调用生理算法计算核心体温、PSI 和 CHS。
  * **调度设计**：部署在 Core 0（通常为 Wi-Fi/BT 射频协议栈核心），优先级为低等 `1`，充分利用 Core 0 空闲时间完成温和的大周期数学计算。
* **`loop()` 主线程 (Core 1)**：
  * 运行 LMIC 协议栈轮询 `os_runloop_once()`，负责射频物理层报文的处理及周期发送事件。和 `TaskBioMotion` 共存，由 RTOS 分时复用 Core 1。

The firmware isolates execution threads using FreeRTOS Task Pinning:
* **`TaskBioMotion` (Core 1, Priority 2)**: $20\text{ms}$ sampling period. Higher priority ensures time-sensitive MAX30102 PPG reading and fall algorithm steps are not preempted.
* **`TaskEnvironment` (Core 0, Priority 1)**: $10\text{s}$ execution period. Performs BME280/MLX90614 reads and updates physiological equations.
* **`loop()` (Core 1)**: Drives the LMIC stack loop `os_runloop_once()`.

### 7.2 I2C 总线互斥锁同步控制 / I2C Bus Synchronization (`i2cMutex`)
系统中的五款传感器组件（MAX30102 心率、BME280 环境、MLX90614 体温、LIS3DH 加速度、AXP192 电源）全部共用同一个硬件 I2C 控制器（`Wire` 端口，引脚 SDA=21, SCL=22）。
* **冲突危险**：由于 `TaskBioMotion` 频繁在 Core 1 高频读取心率，而 `TaskEnvironment` 定时在 Core 0 读取环境参数，若无保护同时读写 `Wire` 总线，会导致 I2C 时序错乱（SDA/SCL 状态竞争），使 ESP32 瞬间发生硬中断崩溃（Guru Meditation Error）。
* **互斥锁实现**：定义全局互斥信号量 `i2cMutex = xSemaphoreCreateMutex();`：
  * 任何任务在开始读写 I2C 总线之前，必须通过 `xSemaphoreTake(i2cMutex, ticks)` 获取锁。
  * 读写结束或超时后，必须执行 `xSemaphoreGive(i2cMutex)` 释放锁。
  * 保证了跨多核、多优先级的 I2C 访问在时序上绝对安全。

All five onboard devices communicate via a single shared I2C bus (`Wire`).
* **Conflict**: Concurrent access from Core 1 (`TaskBioMotion`) and Core 0 (`TaskEnvironment`) causes I2C state collisions and hardware crashes.
* **Mutex**: A global semaphore `i2cMutex` synchronizes access. Before writing or reading, tasks must successfully query `xSemaphoreTake(...)` and release the lock via `xSemaphoreGive(...)` immediately afterwards.

---

## 8. 功耗管理与智能节能策略 / Power Management & Intelligent Energy-Saving Strategy

为了将设备部署寿命从不到 3 天提高到接近 10 天，固件在开机 30 分钟后将自动从全速的“演示模式”切换为周期性的“省电模式”。
To extend the deployment battery life from less than 3 days to nearly 10 days, the firmware automatically transitions from the continuous "Demo Mode" to a duty-cycled "Power-Saving Mode" 30 minutes after boot.

### 8.1 功耗模型与对比分析 / Power Consumption Model & Comparative Analysis

#### A. 各组件功耗特征 / Power Draw Profile per Component:
* **ESP32 MCU (主控)**:
  * 正常工作电流 / Active Current: $\approx 45\text{ mA}$
  * 轻度睡眠电流 / Light Sleep Current: $\approx 0.8\text{ mA}$
* **MAX30102 (心率传感器)**:
  * 正常采样电流 / Active PPG Current: $\approx 2.5\text{ mA}$
  * 软件关断电流 / Shutdown Current: $\approx 0.7\ \mu\text{A}$ (Register `0x02` write `0x01`)
* **MLX90614 (皮温传感器)**:
  * 正常采样电流 / Active Current: $\approx 1.5\text{ mA}$
  * 低功耗休眠电流 / Sleep Current: $\approx 150\ \mu\text{A}$ (I2C sleep command)
* **BME280 (环温湿度传感器)**:
  * Forced 单次测量 / Active: $\approx 350\ \mu\text{A}$
  * 软件休眠电流 / Sleep: $\approx 0.2\ \mu\text{A}$
* **LIS3DH (加速度计)**:
  * 正常运行电流 / Active: $\approx 20\ \mu\text{A}$ (100Hz ODR)
* **LoRa 发射模块 (SX1276)**:
  * 每 90 秒发射一次（发射 120mA 持续 1s，接收 12mA 持续 2s）折合的平均功耗 / Avg LoRa current over 90s interval: $\approx 1.6\text{ mA}$

#### B. 整机平均电流与续航对比 / Overall Current & Battery Life Comparison:
基于标准的 $3400\text{ mAh}$ 18650 锂离子电池进行评估：
Based on a standard $3400\text{ mAh}$ 18650 Li-ion battery:

1. **现有高频轮询模式 (Demo Mode)**:
   * **总平均电流 / Total Avg Current**: $\approx 50.62\text{ mA}$
   * **电池寿命 / Battery Life**: **约 67 小时 (2.8 天) / ~67 hours (2.8 days)**
2. **新增低功耗模式 (Power-Saving Mode)**:
   * **MAX30102 心率**: 每分钟工作 15 秒，关断 45 秒。平均电流 / Avg Current: $\approx 0.625\text{ mA}$
   * **MLX90614 皮温**: 每分钟工作 5 秒，休眠 55 秒。平均电流 / Avg Current: $\approx 0.263\text{ mA}$
   * **ESP32 MCU**: 在后 45 秒心率休眠期间，由于无高频心率计算需求，且 LIS3DH 处于硬件中断待命状态，`TaskBioMotion` 的轮询周期从 $20\text{ ms}$ (50Hz) 放宽至 **$200\text{ ms}$** (5Hz)。极低频的唤醒使主控大部分时间处于轻度睡眠。平均电流 / Avg Current: $\approx 11.85\text{ mA}$
   * **总平均电流 / Total Avg Current**: $\approx 14.36\text{ mA}$
   * **电池寿命 / Battery Life**: **约 237 小时 (9.8 天) / ~237 hours (9.8 days)**
   * **省电效率 / Energy Saved**: **约 71.6% / ~71.6%**

---

### 8.2 智能混合节能机制 / Intelligent Hybrid Power-Saving Mechanism

#### 1. 开机延时演示模式 / Boot-Delay Demo Mode
设备开机前 30 分钟（`millis() < 1,800,000`）强行保持演示模式。传感器全速采样，前端界面实时刷新，以便技术团队进行现场展示与功能调试。
For the first 30 minutes of uptime, the device is locked in Demo Mode with continuous high-frequency sampling to facilitate live display and commissioning.

#### 2. 周期性间歇休眠 / Duty-Cycled Sampling
开机超过 30 分钟且没有发生跌倒警报时，系统以 60 秒为大周期自动循环：
* **心率采样窗口**：前 15 秒工作，后 45 秒关闭 MAX30102 激光发射，将任务延迟周期挂起至 $200\text{ ms}$。
* **温度采样窗口**：前 10 秒（环境任务 10s 延时）内执行一次传感器读取，后 50 秒直接跳过 I2C 传输，复用上一周期的内存值。
* **数据连续性保证**：在传感器休眠期间，LoRa 发送包会使用**休眠前锁存的最后一次有效平均值**，防止后端看板的趋势曲线归零或断崖。

After 30 minutes, the device enters a 60-second cycle: MAX30102 runs for 15s then shuts down; BME280/MLX90614 are read once per minute. To prevent telemetry telemetry charts from dropping to zero, uplinks use the last latched valid sensor values during sleep intervals.

#### 3. 跌倒事件触发回弹 / Fall-Triggered Switchback
当 LIS3DH 监测到瞬时冲击拉高 GPIO25 中断，进入跌倒报警状态（`STATE_FALL_DETECTED`）后：
* 系统自动记录当前时间戳 `lastFallTriggerTime = millis()`。
* **强制切回演示模式 5 分钟**：这让救援人员在突发紧急状况下，能持续获得高频更新的病患心率和核心体温数据。5 分钟无异常后，设备再次自动切回省电模式。

If a fall is confirmed (`STATE_FALL_DETECTED`), the system sets `lastFallTriggerTime = millis()`, forcing the device back to high-frequency Demo Mode for 5 minutes. This ensures dense health monitoring during an emergency before returning to low-power mode.


