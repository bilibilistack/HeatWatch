export type Language = 'zh' | 'en';

export const translations = {
  zh: {
    // Header & Meta
    appTitle: '🛡️ HeatWatch',
    tbCloud: 'TB 云平台',
    lastSync: '已同步',
    syncing: '正在同步...',
    globalFallWarning: '⚠️ 紧急警告：检测到矿区有员工发生跌倒！请立即查看下方标红设备卡片。',
    
    // Intro & Filters
    siteTag: '西澳矿区一号 (WA Mining Site #1)',
    dashboardTitle: '智能热应激与工伤监测中控台',
    filterWearables: '智能穿戴设备',
    filterAll: '展示全部设备',
    
    // KPIs
    kpiTotalMiners: '受监视总人数',
    kpiWarnings: '高温预警人数',
    kpiCritical: '紧急警报人次',
    unitMiners: '人',
    unitCases: '例',
    
    // Status & Loading
    loadingDevices: '正在从 ThingsBoard Cloud 获取最新设备注册信息...',
    failedAccess: '无法访问物联网中控台',
    errorSuggestions: '排查建议：',
    errorSuggestion1: '1. 请确认您已将 frontend/.env.local 文件配置好了账号密码。',
    errorSuggestion2: '2. 请检查本地网络能否顺畅打开 thingsboard.cloud。',
    emptyDevices: '暂无符合筛选条件的物联网设备。',
    emptyDevicesTip: '请确保您的 Wearable 设备注册类型为 heatstress-group10 且已接入。',
    
    // Card preview
    cardOffline: '💤 离线',
    cardSafe: '安全 (Normal)',
    cardWarning: '⚠️ 中度风险 (Warning)',
    cardCritical: '🚨 极高风险 (Critical)',
    cardFall: '💥 跌倒警报 (Fall Detected)',
    cardHeartRate: '心率',
    cardAmbientTemp: '环境温度',
    cardWbgt: '热应激指数 (WBGT)',
    cardLastSync: '最后同步',
    cardJustNow: '刚刚',
    cardSecondsAgo: '秒前',
    cardMinutesAgo: '分钟前',
    cardOfflineWarning: '设备可能已断联',
    cardSkinAmbientTemp: '体温 / 环境温度',
    cardRiskLevel: '风险等级',
    
    // Detail Drawer
    detailHealthLevel: '当前健康与安全级别',
    detailResetAlarm: '重置警报',
    detailResetting: '正在重置...',
    detailPhysiologyTitle: '矿工生理状态',
    detailHeartRateAvg: '心率 (Avg)',
    detailSkinTemp: '体表温度',
    detailEnvironmentTitle: '作业环境指标',
    detailRelativeHumidity: '相对湿度',
    detailDiscomfortIndex: '不舒适指数 (DI)',
    detailWbgtBold: '热应激指数 (WBGT)',
    
    // Actions Detail
    actionRenameTitle: '重命名矿工姓名',
    actionRenamePlaceholder: '输入矿工姓名 (例如：张伟)',
    actionSave: '保存',
    
    actionMuteTitle: '远程消警控制中心',
    actionMuteDesc: '如果矿工产生误报（例如误触发跌倒），可从控制中心发送无线下行指令进行消音和解除警报。',
    actionMuteBtn: '下发 0xF3 遥控消警指令 (Mute Alarm)',
    actionMutingBtn: '正在发送消警指令...',
    
    actionCalibrationTitle: '设定外部传感器 WBGT (校准)',
    actionCalibrationDesc: '若作业区有高精度悬挂式干湿球温度计，可将该区域真实 WBGT 下发给矿工设备做算法校准。',
    actionCalibrationBtn: '下发 0xF2 WBGT 外部设定数据',
    actionCalibratingBtn: '正在发送校准数据...',
    
    actionHydrationTitle: '生理参数与智能喝水提醒',
    actionHydrationDesc: '根据员工的年龄和 BMI 自动对危险等级预警线做出偏移校准（若超重或年长，则激进地调低阈值进行喝水催促）。',
    actionAge: '年龄',
    actionHeight: '身高 (cm)',
    actionWeight: '体重 (kg)',
    actionBmi: 'BMI 数值',
    actionOffset: '应激指数安全线偏移',
    actionSaveParams: '保存个人参数并开启自动偏移',
    actionHydrationRating: '喝水建议评级',
    actionHydrationNeededAlert: '⚠️ 该矿工处于危险指数偏高区间！(个人WBGT饮水报警线已由 30.0°C 下调至 {threshold}°C) 建议立刻发送强制饮水震动催促！',
    actionHydrationSafeAlert: '✅ 该矿工生理参数和作业环境匹配安全，喝水频次正常。',
    actionHydrationBtn: '强制发送 0xF4 下行喝水震动指令',
    actionHydratingBtn: '正在下发喝水震动...',
    
    // Status Banner Responses
    statusParamsSaved: '个性化生理参数已成功保存！已更新预警算法。',
    statusRenameSuccess: '设备名称成功修改为: "{name}"',
    statusResetSuccess: '💥 警报重置指令 (0xF3 Downlink) 已成功下发至设备！',
    statusResetFailed: '重置失败: ',
    statusWbgtSuccess: '🌡️ 外部传感器 WBGT 设定为 {wbgt}°C (0xF2) 已发送！',
    statusWbgtFailed: '设定失败: ',
    statusHydrationSuccess: '💧 喝水提醒脉冲指令 (0xF4 Downlink) 已成功下发至设备！',
    statusHydrationFailed: '发送失败: '
  },
  en: {
    // Header & Meta
    appTitle: '🛡️ HeatWatch',
    tbCloud: 'TB Cloud',
    lastSync: 'Synced',
    syncing: 'Syncing...',
    globalFallWarning: '⚠️ EMERGENCY WARNING: Fall detected in the mining area! Please check highlighted cards below.',
    
    // Intro & Filters
    siteTag: 'WA Mining Site #1',
    dashboardTitle: 'Smart Heat Stress & Fall Detection Console',
    filterWearables: 'Smart Wearables',
    filterAll: 'All Devices',
    
    // KPIs
    kpiTotalMiners: 'Monitored Miners',
    kpiWarnings: 'Heatstress Warnings',
    kpiCritical: 'Critical Alarms',
    unitMiners: ' ',
    unitCases: ' ',
    
    // Status & Loading
    loadingDevices: 'Loading device registration from ThingsBoard Cloud...',
    failedAccess: 'Cannot access IoT Dashboard Console',
    errorSuggestions: 'Troubleshooting suggestions:',
    errorSuggestion1: '1. Please confirm that the credentials in frontend/.env.local are correctly configured.',
    errorSuggestion2: '2. Please check if your local network can smoothly connect to thingsboard.cloud.',
    emptyDevices: 'No matching IoT devices found.',
    emptyDevicesTip: 'Ensure that your wearable devices are registered under type "heatstress-group10" and active.',
    
    // Card preview
    cardOffline: '💤 Offline',
    cardSafe: 'Normal (Safe)',
    cardWarning: '⚠️ Warning Risk',
    cardCritical: '🚨 Critical Risk',
    cardFall: '💥 Fall Detected',
    cardHeartRate: 'Heart Rate',
    cardAmbientTemp: 'Ambient Temp',
    cardWbgt: 'Heat Stress (WBGT)',
    cardLastSync: 'Last sync',
    cardJustNow: 'just now',
    cardSecondsAgo: 's ago',
    cardMinutesAgo: 'm ago',
    cardOfflineWarning: 'Device might be disconnected',
    cardSkinAmbientTemp: 'Skin / Ambient Temp',
    cardRiskLevel: 'Risk Level',
    
    // Detail Drawer
    detailHealthLevel: 'Health & Safety Risk Status',
    detailResetAlarm: 'Reset Alarm',
    detailResetting: 'Resetting...',
    detailPhysiologyTitle: 'Miner Vitals',
    detailHeartRateAvg: 'Heart Rate (Avg)',
    detailSkinTemp: 'Skin Temp',
    detailEnvironmentTitle: 'Environmental Index',
    detailRelativeHumidity: 'Humidity',
    detailDiscomfortIndex: 'Discomfort Index (DI)',
    detailWbgtBold: 'Heat Index (WBGT)',
    
    // Actions Detail
    actionRenameTitle: 'Rename Miner Name',
    actionRenamePlaceholder: 'Enter miner name (e.g. John Doe)',
    actionSave: 'Save',
    
    actionMuteTitle: 'Remote Alarm Dismiss Control',
    actionMuteDesc: 'In case of false alarms (e.g., false fall triggers), you can transmit a downlink command to mute the buzzer/vibration.',
    actionMuteBtn: 'Send 0xF3 Downlink Mute Command',
    actionMutingBtn: 'Sending mute command...',
    
    actionCalibrationTitle: 'Set External Sensor WBGT (Calibration)',
    actionCalibrationDesc: 'If high-precision dry/wet bulb hygrometers are on-site, you can broadcast the calibrated WBGT value to the device.',
    actionCalibrationBtn: 'Send 0xF2 WBGT Calibration Data',
    actionCalibratingBtn: 'Sending calibration data...',
    
    actionHydrationTitle: 'Physiological Profiling & Smart Hydration',
    actionHydrationDesc: 'Calibrate risk levels dynamically based on miner Age and BMI (aggressive offset decreases safe baseline up to -2.5°C to urge hydration).',
    actionAge: 'Age',
    actionHeight: 'Height (cm)',
    actionWeight: 'Weight (kg)',
    actionBmi: 'BMI Index',
    actionOffset: 'Threshold Safety Offset',
    actionSaveParams: 'Save Parameters & Enable Offset',
    actionHydrationRating: 'Hydration Rating & Advice',
    actionHydrationNeededAlert: '⚠️ Warning: High heat-stress exposure! (Personal hydration line shifted from 30.0°C to {threshold}°C) Urge manual drinking prompt vibration!',
    actionHydrationSafeAlert: '✅ Vitals matching local environments safely. Hydration interval is in normal states.',
    actionHydrationBtn: 'Force Send 0xF4 Downlink Hydration Reminder',
    actionHydratingBtn: 'Sending vibration...',
    
    // Status Banner Responses
    statusParamsSaved: 'Personal physiological profile successfully saved and algorithmic offset applied!',
    statusRenameSuccess: 'Device display name successfully renamed to: "{name}"',
    statusResetSuccess: '💥 Alarm reset instruction (0xF3 Downlink) has been dispatched successfully!',
    statusResetFailed: 'Reset failed: ',
    statusWbgtSuccess: '🌡️ External calibrated WBGT set to {wbgt}°C (0xF2) successfully!',
    statusWbgtFailed: 'Setting failed: ',
    statusHydrationSuccess: '💧 Hydration haptic vibration reminder (0xF4 Downlink) has been triggered successfully!',
    statusHydrationFailed: 'Trigger failed: '
  }
};
