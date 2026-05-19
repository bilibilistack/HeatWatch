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
* **初始化 / Initialization**：首次开机采样时，直接将核心体温设为 $T_{skin} + 4.0^\circ\text{C}$，后续通过差分方程连续平滑过渡 / Upon startup, $T_c$ is initialized directly to $T_{skin} + 4.0^\circ\text{C}$ to avoid warm-up lag.

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

## 3. 下行静音指令处理机制 / Downlink Mute Command Processing

### 3.1 10分钟强制静音保护 / 10-Minute Force Mute Protection
当收到云端下发的 `0xF3` 警报重置指令时：
1. 系统记录当前静音启动时间戳：`muteStartTime = millis()`。
2. 标志位设为有效：`alarmMuted = true`，在此期间屏蔽声光/震动警报。
3. **静音失效检查**：在每 10 秒的评估中，如果 `millis() - muteStartTime >= 600,000 ms (10 minutes)`，系统自动将 `alarmMuted` 复位为 `false`，从而恢复报警功能，防止矿工由于误触或暂时缓解而暴露在持续性高温威胁中。

### 3.2 外部校准值保护 / External Calibration Protection
* 收到 `0xF3` **不会清空 `externalWBGT`**，设备会继续持有最近收到的环境校准基准值，直至超时（30分钟）自然失效。

When receiving the downlink `0xF3` command:
1. `muteStartTime` is recorded using `millis()`, and `alarmMuted` is set to `true` (muting physical alarms).
2. **Mute Timeout Check**: If `millis() - muteStartTime >= 10 minutes`, `alarmMuted` is automatically reset to `false`.
3. **Calibration Persistence**: `externalWBGT` is **not cleared** (remains active until its natural 30-minute timeout).
