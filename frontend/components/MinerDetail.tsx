import React, { useState, useEffect } from 'react';
import { 
  X, Heart, Thermometer, AlertTriangle, Droplet, 
  Settings, Save, RefreshCw, VolumeX, ShieldAlert, Award, TrendingUp, Battery
} from 'lucide-react';
import { TelemetryValues } from '@/lib/thingsboard';
import { translations, Language } from '@/lib/translations';

interface MinerDetailProps {
  deviceId: string;
  deviceType: string;
  name: string; // The ThingsBoard name
  minerName: string; // The custom name
  telemetry: TelemetryValues;
  lang: Language;
  onClose: () => void;
  onRename: (newName: string) => void;
}

const MinerDetailComponent: React.FC<MinerDetailProps> = ({
  deviceId,
  deviceType,
  name,
  minerName,
  telemetry,
  lang,
  onClose,
  onRename,
}) => {
  const {
    heartRateAvg = 0,
    skinTemp = 0,
    ambientTemp = 0,
    humidity = 0,
    HeatStressIndex = 0,
    discomfortIndex = 0,
    riskLevel = 0,
    fallDetected = false,
    battery = 0,
    lastUpdateTs
  } = telemetry;

  const t = translations[lang];

  // Local state for Rename
  const [tempName, setTempName] = useState(minerName || name);

  // Normalize battery to fractional scale and handle display
  const normBattery = battery > 1 ? battery / 100 : battery;
  const displayBatteryPercent = battery > 1 ? Math.round(battery) : Math.round(battery * 100);

  // Battery status color
  let batteryColor = 'var(--color-safe)';
  if (normBattery <= 0.2) batteryColor = 'var(--color-critical)';
  else if (normBattery <= 0.5) batteryColor = 'var(--color-warning)';

  // Relative time since last received message (uplink)
  const getRelativeUpdateText = () => {
    if (!lastUpdateTs) return '';
    const secondsAgo = Math.floor((Date.now() - lastUpdateTs) / 1000);
    if (secondsAgo < 10) return `(${t.cardJustNow})`;
    if (secondsAgo < 60) return `(${secondsAgo}${t.cardSecondsAgo})`;
    const minutesAgo = Math.floor(secondsAgo / 60);
    if (lang === 'zh') {
      return `(${minutesAgo}分钟前)`;
    } else {
      return `(${minutesAgo} ${minutesAgo === 1 ? 'min' : 'mins'} ago)`;
    }
  };

  // History trend state & dynamic metric configuration
  const [history, setHistory] = useState<any[]>([]);
  const [isLoadingHistory, setIsLoadingHistory] = useState<boolean>(true);
  const [selectedMetric, setSelectedMetric] = useState<'HeatStressIndex' | 'skinTemp' | 'ambientTemp' | 'discomfortIndex' | 'heartRateAvg'>('HeatStressIndex');
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);

  const metricConfig = {
    HeatStressIndex: {
      btnLabel: lang === 'zh' ? '热应激 (WBGT)' : 'Heat Stress',
      minY: 10,
      maxY: 45,
      unit: '°C',
      color: 'hsl(var(--color-warning))',
      gradientId: 'grad-wbgt'
    },
    skinTemp: {
      btnLabel: lang === 'zh' ? '体温' : 'Skin Temp',
      minY: 32,
      maxY: 42,
      unit: '°C',
      color: 'hsl(var(--color-accent))',
      gradientId: 'grad-skin'
    },
    ambientTemp: {
      btnLabel: lang === 'zh' ? '环温' : 'Ambient Temp',
      minY: 10,
      maxY: 50,
      unit: '°C',
      color: '#c2410c',
      gradientId: 'grad-ambient'
    },
    discomfortIndex: {
      btnLabel: lang === 'zh' ? '不快指数' : 'Discomfort',
      minY: 10,
      maxY: 40,
      unit: '',
      color: '#8b5cf6',
      gradientId: 'grad-di'
    },
    heartRateAvg: {
      btnLabel: lang === 'zh' ? '心率' : 'Heart Rate',
      minY: 40,
      maxY: 180,
      unit: ' BPM',
      color: 'hsl(var(--color-critical))',
      gradientId: 'grad-hr'
    }
  };

  // Poll 30-min history from Next API
  useEffect(() => {
    let active = true;
    const fetchHistory = async () => {
      try {
        const res = await fetch(`/api/devices/${deviceId}/history`);
        const data = await res.json();
        if (active && data.success && data.history) {
          setHistory(data.history);
        }
      } catch (err) {
        console.error('Failed to fetch history:', err);
      } finally {
        if (active) setIsLoadingHistory(false);
      }
    };
    
    fetchHistory();
    const interval = setInterval(fetchHistory, 15000);
    return () => {
      active = false;
      clearInterval(interval);
    };
  }, [deviceId]);

  // Coordinate dimensions for inline SVG
  const viewBoxWidth = 500;
  const viewBoxHeight = 220;
  const paddingLeft = 45;
  const paddingRight = 15;
  const paddingTop = 20;
  const paddingBottom = 30;

  const chartWidth = viewBoxWidth - paddingLeft - paddingRight; // 440
  const chartHeight = viewBoxHeight - paddingTop - paddingBottom; // 170

  const config = metricConfig[selectedMetric];
  const minY = config.minY;
  const maxY = config.maxY;
  const unit = config.unit;
  const strokeColor = config.color;

  const points = history.map((item, index) => {
    const val = item[selectedMetric] ?? minY;
    const x = paddingLeft + (index * chartWidth) / Math.max(1, history.length - 1);
    const y = paddingTop + chartHeight - ((val - minY) / (maxY - minY)) * chartHeight;
    return {
      x,
      y: Math.max(paddingTop, Math.min(paddingTop + chartHeight, y)),
      value: val,
      timeStr: item.timeStr
    };
  });

  let strokeD = '';
  let fillD = '';
  if (points.length > 0) {
    strokeD = `M ${points[0].x} ${points[0].y} ` + points.slice(1).map(p => `L ${p.x} ${p.y}`).join(' ');
    fillD = `${strokeD} L ${points[points.length - 1].x} ${paddingTop + chartHeight} L ${points[0].x} ${paddingTop + chartHeight} Z`;
  }

  const yTicks = [0, 0.33, 0.66, 1].map(ratio => {
    const val = minY + ratio * (maxY - minY);
    const y = paddingTop + chartHeight - ratio * chartHeight;
    return {
      y,
      val: val.toFixed(selectedMetric === 'heartRateAvg' ? 0 : 1)
    };
  });

  const xTicks = [];
  if (points.length >= 2) {
    xTicks.push(points[0]);
    xTicks.push(points[Math.floor(points.length / 2)]);
    xTicks.push(points[points.length - 1]);
  }

  const handleMouseMove = (e: React.MouseEvent<SVGSVGElement>) => {
    if (history.length === 0) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const wRatio = rect.width / viewBoxWidth;
    const svgX = x / wRatio;
    
    const relativeX = svgX - paddingLeft;
    if (relativeX < 0 || relativeX > chartWidth) {
      setHoveredIndex(null);
      return;
    }
    const idx = Math.round((relativeX / chartWidth) * (history.length - 1));
    if (idx >= 0 && idx < history.length) {
      setHoveredIndex(idx);
    } else {
      setHoveredIndex(null);
    }
  };
  
  // Local state for RPC loading and status
  const [isResettingAlarm, setIsResettingAlarm] = useState(false);
  const [isSendingWbgt, setIsSendingWbgt] = useState(false);
  const [isSendingHydration, setIsSendingHydration] = useState(false);
  const [rpcStatus, setRpcStatus] = useState<{ type: 'success' | 'error' | null; msg: string }>({ type: null, msg: '' });

  // Slider state for External WBGT (default to current HeatStressIndex or 28.0)
  const [extWbgt, setExtWbgt] = useState<number>(HeatStressIndex !== 0 ? parseFloat(HeatStressIndex.toFixed(1)) : 28.0);

  // Personalized parameters (Age, Height in cm, Weight in kg)
  const [age, setAge] = useState<number>(30);
  const [height, setHeight] = useState<number>(175);
  const [weight, setWeight] = useState<number>(70);
  const [bmi, setBmi] = useState<number>(22.9);
  const [wbgtOffset, setWbgtOffset] = useState<number>(0);
  const [isParamsSaved, setIsParamsSaved] = useState<boolean>(false);

  // Load personalized parameters from LocalStorage on mount
  useEffect(() => {
    const savedParams = localStorage.getItem(`params_${deviceId}`);
    if (savedParams) {
      try {
        const parsed = JSON.parse(savedParams);
        setAge(parsed.age || 30);
        setHeight(parsed.height || 175);
        setWeight(parsed.weight || 70);
        setIsParamsSaved(true);
      } catch (e) {
        console.error('Error parsing saved params', e);
      }
    }
  }, [deviceId]);

  // Calculate BMI and Heat Stress Warning Offset whenever inputs change
  useEffect(() => {
    if (height > 0) {
      const heightInMeters = height / 100;
      const calculatedBmi = weight / (heightInMeters * heightInMeters);
      setBmi(parseFloat(calculatedBmi.toFixed(1)));

      // Offset logic based on BMI and Age:
      // High BMI (>25 = Overweight, >30 = Obese) or Age (>45) increases susceptibility to Heat Stress
      // We reduce the safety threshold offset by 1.0 - 2.5 degrees Celsius for drinking reminders
      let offset = 0;
      if (calculatedBmi >= 30) {
        offset += 1.5; // Obese: high risk
      } else if (calculatedBmi >= 25) {
        offset += 0.8; // Overweight: moderate risk
      }

      if (age >= 50) {
        offset += 1.0; // Older age: high risk
      } else if (age >= 40) {
        offset += 0.5; // Aging: mild risk
      }

      setWbgtOffset(parseFloat(offset.toFixed(1)));
    }
  }, [age, height, weight]);

  const handleSaveParams = () => {
    localStorage.setItem(`params_${deviceId}`, JSON.stringify({ age, height, weight }));
    setIsParamsSaved(true);
    showStatus('success', t.statusParamsSaved);
  };

  const handleRenameSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    onRename(tempName);
    showStatus('success', t.statusRenameSuccess.replace('{name}', tempName));
  };

  const showStatus = (type: 'success' | 'error', msg: string) => {
    setRpcStatus({ type, msg });
    setTimeout(() => {
      setRpcStatus({ type: null, msg: '' });
    }, 4000);
  };

  // RPC: Reset Device Alarm (F3 Downlink)
  const handleResetAlarm = async () => {
    setIsResettingAlarm(true);
    try {
      // 0xF3 Downlink Reset Alarm RPC call
      const response = await fetch(`/api/devices/${deviceId}/rpc`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ method: 'resetAlarm', params: { command: 0xF3 } })
      });

      const data = await response.json();
      if (data.success) {
        showStatus('success', t.statusResetSuccess);
      } else {
        throw new Error(data.error || '下发指令失败');
      }
    } catch (e: any) {
      showStatus('error', t.statusResetFailed + e.message);
    } finally {
      setIsResettingAlarm(false);
    }
  };

  // RPC: Set External WBGT (F2 Downlink)
  const handleSendWbgt = async () => {
    setIsSendingWbgt(true);
    try {
      const wbgtRaw = Math.round(extWbgt * 10);
      const response = await fetch(`/api/devices/${deviceId}/rpc`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ 
          method: 'setExternalWBGT', 
          params: { 
            command: 0xF2,
            wbgtRaw: wbgtRaw,
            wbgtFloat: extWbgt
          } 
        })
      });

      const data = await response.json();
      if (data.success) {
        showStatus('success', t.statusWbgtSuccess.replace('{wbgt}', extWbgt.toFixed(1)));
      } else {
        throw new Error(data.error || '设定失败');
      }
    } catch (e: any) {
      showStatus('error', t.statusWbgtFailed + e.message);
    } finally {
      setIsSendingWbgt(false);
    }
  };

  // RPC: Send Manual Hydration Reminder (F4 Downlink)
  const handleSendHydration = async () => {
    setIsSendingHydration(true);
    try {
      // 0xF4 Downlink Hydration Reminder RPC call
      const response = await fetch(`/api/devices/${deviceId}/rpc`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ method: 'hydrationReminder', params: { command: 0xF4 } })
      });

      const data = await response.json();
      if (data.success) {
        showStatus('success', t.statusHydrationSuccess);
      } else {
        throw new Error(data.error || '发送失败');
      }
    } catch (e: any) {
      showStatus('error', t.statusHydrationFailed + e.message);
    } finally {
      setIsSendingHydration(false);
    }
  };

  // Custom Hydration Warning logic (Aggressive threshold based on age/BMI offset)
  const baselineThreshold = 30.0;
  const currentThreshold = baselineThreshold - wbgtOffset;
  const isHydrationNeeded = HeatStressIndex >= currentThreshold;

  // Visual styling for risk status
  let riskTheme = { bg: 'rgba(21, 128, 61, 0.08)', text: 'hsl(var(--color-safe))', border: 'rgba(21, 128, 61, 0.2)', label: t.cardSafe };
  if (fallDetected) {
    riskTheme = { bg: 'rgba(220, 38, 38, 0.15)', text: 'hsl(var(--color-critical))', border: 'rgba(220, 38, 38, 0.4)', label: t.cardFall };
  } else if (riskLevel >= 2) {
    riskTheme = { bg: 'rgba(220, 38, 38, 0.15)', text: 'hsl(var(--color-critical))', border: 'rgba(220, 38, 38, 0.4)', label: t.cardCritical };
  } else if (riskLevel === 1) {
    riskTheme = { bg: 'rgba(194, 65, 12, 0.12)', text: 'hsl(var(--color-warning))', border: 'rgba(194, 65, 12, 0.3)', label: t.cardWarning };
  }

  return (
    <div className="detail-modal-overlay">
      <div className="detail-panel glass-card">
        
        {/* Header */}
        <div className="panel-header">
          <div className="panel-title-area">
            <h2 className="panel-title">{tempName} <span className="dev-name-tag">({name})</span></h2>
            <div style={{ display: 'flex', gap: '8px', alignItems: 'center', marginTop: '4px' }}>
              <span className="device-type-badge">{deviceType}</span>
              <div className="detail-battery-badge" style={{ color: batteryColor, display: 'inline-flex', alignItems: 'center', gap: '4px', fontSize: '11px', fontWeight: 'bold' }}>
                <Battery size={14} />
                <span>{displayBatteryPercent}%</span>
              </div>
            </div>
          </div>
          <button className="close-btn" onClick={onClose}>
            <X size={20} />
          </button>
        </div>

        {/* RPC Status Alert banner */}
        {rpcStatus.type && (
          <div className={`status-banner banner-${rpcStatus.type}`}>
            {rpcStatus.type === 'success' ? '✓ ' : '⚠️ '}
            {rpcStatus.msg}
          </div>
        )}

        <div className="panel-scroll-content">
          
          {/* Risk Level Banner */}
          <div className="risk-banner" style={{ backgroundColor: riskTheme.bg, color: riskTheme.text, borderColor: riskTheme.border }}>
            <ShieldAlert size={22} />
            <div className="risk-banner-text">
              <div className="rb-title">{t.detailHealthLevel}</div>
              <div className="rb-val font-semibold">{riskTheme.label}</div>
            </div>
            {fallDetected && (
              <button 
                className="reset-alarm-btn-mini" 
                onClick={handleResetAlarm} 
                disabled={isResettingAlarm}
              >
                {isResettingAlarm ? <RefreshCw size={14} className="loader-icon" /> : <VolumeX size={14} />}
                {t.detailResetAlarm}
              </button>
            )}
          </div>

          {/* Historical Trend Chart Section */}
          <div className="action-card history-chart-card">
            <div className="history-header">
              <h3 className="ac-title font-bold" style={{ fontSize: '14px', margin: 0 }}>
                <TrendingUp size={18} className="color-acc" /> {lang === 'zh' ? '30分钟历史趋势监测' : '30-Min History Trend'}
              </h3>
              {isLoadingHistory && <span className="sync-text" style={{ fontSize: '10px' }}>{t.syncing}</span>}
            </div>

            {/* Metric Segmented Buttons Selector */}
            <div className="metric-selector">
              {(Object.keys(metricConfig) as Array<keyof typeof metricConfig>).map((key) => {
                const cfg = metricConfig[key];
                const isActive = selectedMetric === key;
                return (
                  <button
                    key={key}
                    type="button"
                    onClick={() => {
                      setSelectedMetric(key);
                      setHoveredIndex(null);
                    }}
                    className={`metric-select-btn ${isActive ? 'active' : ''}`}
                    style={{
                      borderColor: isActive ? cfg.color : 'var(--border-light)',
                      color: isActive ? cfg.color : 'hsl(var(--text-secondary))',
                      background: isActive ? `${cfg.color}12` : 'transparent'
                    }}
                  >
                    {cfg.btnLabel}
                  </button>
                );
              })}
            </div>

            {/* SVG Visual Canvas */}
            <div className="chart-canvas-container">
              {history.length === 0 ? (
                <div className="chart-loading">
                  <RefreshCw size={20} className="loader-icon" />
                  <span>{lang === 'zh' ? '正在载入历史趋势...' : 'Loading history trend...'}</span>
                </div>
              ) : (
                <div style={{ position: 'relative' }}>
                  <svg
                    viewBox={`0 0 ${viewBoxWidth} ${viewBoxHeight}`}
                    className="history-svg"
                    onMouseMove={handleMouseMove}
                    onMouseLeave={() => setHoveredIndex(null)}
                  >
                    <defs>
                      <linearGradient id={config.gradientId} x1="0" y1="0" x2="0" y2="1">
                        <stop offset="0%" stopColor={strokeColor} stopOpacity="0.25" />
                        <stop offset="100%" stopColor={strokeColor} stopOpacity="0.00" />
                      </linearGradient>
                    </defs>

                    {/* Y-axis grid lines & labels */}
                    {yTicks.map((tick, i) => (
                      <g key={i}>
                        <line
                          x1={paddingLeft}
                          y1={tick.y}
                          x2={viewBoxWidth - paddingRight}
                          y2={tick.y}
                          stroke="var(--border-light)"
                          strokeWidth="1"
                          strokeDasharray="4 4"
                        />
                        <text
                          x={paddingLeft - 8}
                          y={tick.y + 3}
                          textAnchor="end"
                          fontSize="9"
                          fill="hsl(var(--text-secondary))"
                          fontFamily="monospace"
                          fontWeight="600"
                        >
                          {tick.val}
                        </text>
                      </g>
                    ))}

                    {/* Chart Paths */}
                    {fillD && (
                      <path
                        d={fillD}
                        fill={`url(#${config.gradientId})`}
                      />
                    )}
                    {strokeD && (
                      <path
                        d={strokeD}
                        fill="none"
                        stroke={strokeColor}
                        strokeWidth="2.5"
                        strokeLinecap="round"
                        strokeLinejoin="round"
                      />
                    )}

                    {/* Data Point Circles */}
                    {points.map((p, idx) => (
                      <circle
                        key={idx}
                        cx={p.x}
                        cy={p.y}
                        r="3.5"
                        fill="hsl(var(--bg-surface))"
                        stroke={strokeColor}
                        strokeWidth="2"
                        style={{ transition: 'all 0.1s ease' }}
                      />
                    ))}

                    {/* X-axis labels */}
                    {xTicks.map((tick, i) => (
                      <g key={i}>
                        <text
                          x={tick.x}
                          y={viewBoxHeight - 10}
                          textAnchor="middle"
                          fontSize="9"
                          fill="hsl(var(--text-secondary))"
                          fontFamily="monospace"
                          fontWeight="600"
                        >
                          {tick.timeStr}
                        </text>
                      </g>
                    ))}

                    {/* Hover vertical dashed line */}
                    {hoveredIndex !== null && points[hoveredIndex] && (
                      <line
                        x1={points[hoveredIndex].x}
                        y1={paddingTop}
                        x2={points[hoveredIndex].x}
                        y2={paddingTop + chartHeight}
                        stroke="hsl(var(--text-secondary))"
                        strokeWidth="1.5"
                        strokeDasharray="3 3"
                      />
                    )}
                  </svg>

                  {/* HTML-based Interactive Tooltip */}
                  {hoveredIndex !== null && points[hoveredIndex] && (
                    <div
                      className="chart-tooltip"
                      style={{
                        left: `${(points[hoveredIndex].x / viewBoxWidth) * 100}%`,
                        top: `${(points[hoveredIndex].y / viewBoxHeight) * 100}%`,
                        borderColor: strokeColor,
                      }}
                    >
                      <div className="tooltip-time">{points[hoveredIndex].timeStr}</div>
                      <div className="tooltip-value font-bold" style={{ color: strokeColor }}>
                        {points[hoveredIndex].value.toFixed(selectedMetric === 'heartRateAvg' ? 0 : 1)}
                        <span className="tooltip-unit">{unit}</span>
                      </div>
                    </div>
                  )}
                </div>
              )}
            </div>
          </div>

          {/* Vitals & Environment grid */}
          <div className="detail-grid">
            
            {/* physiological health metrics card */}
            <div className="sub-detail-card">
              <h4 className="sdc-title"><Heart size={16} className="color-crit" /> {t.detailPhysiologyTitle}</h4>
              <div className="sdc-metrics">
                <div className="sdc-metric-row">
                  <span className="smr-label">{t.detailHeartRateAvg}</span>
                  <span className="smr-val num-text color-crit">
                    {heartRateAvg > 0 ? heartRateAvg : '--'} <span className="smr-unit">BPM</span>
                  </span>
                </div>
                <div className="sdc-metric-row">
                  <span className="smr-label">{t.detailSkinTemp}</span>
                  <span className="smr-val num-text">
                    {skinTemp > 0 ? skinTemp.toFixed(1) : '--'} <span className="smr-unit">°C</span>
                  </span>
                </div>
                <div className="sdc-metric-row">
                  <span className="smr-label">{lang === 'zh' ? '设备电量' : 'Device Battery'}</span>
                  <span className="smr-val num-text" style={{ color: batteryColor }}>
                    {displayBatteryPercent}%
                  </span>
                </div>
                <div className="sdc-gauge-bar">
                  <div className="sgb-fill" style={{ 
                    width: `${Math.min(100, Math.max(0, heartRateAvg > 0 ? ((heartRateAvg - 60) / 100) * 100 : 0))}%`,
                    backgroundColor: heartRateAvg > 140 ? 'hsl(var(--color-critical))' : heartRateAvg > 100 ? 'hsl(var(--color-warning))' : 'hsl(var(--color-safe))'
                  }} />
                </div>
              </div>
            </div>

            {/* environmental index card */}
            <div className="sub-detail-card">
              <h4 className="sdc-title"><Thermometer size={16} className="color-acc" /> {t.detailEnvironmentTitle}</h4>
              <div className="sdc-metrics">
                <div className="sdc-metric-row">
                  <span className="smr-label">{t.cardAmbientTemp}</span>
                  <span className="smr-val num-text color-acc">
                    {ambientTemp > 0 ? ambientTemp.toFixed(1) : '--'} <span className="smr-unit">°C</span>
                  </span>
                </div>
                <div className="sdc-metric-row">
                  <span className="smr-label">{t.detailRelativeHumidity}</span>
                  <span className="smr-val num-text">
                    {humidity > 0 ? Math.round(humidity) : '--'} <span className="smr-unit">%</span>
                  </span>
                </div>
                <div className="sdc-metric-row">
                  <span className="smr-label">{t.detailDiscomfortIndex}</span>
                  <span className="smr-val num-text">
                    {discomfortIndex > 0 ? discomfortIndex.toFixed(1) : '--'}
                  </span>
                </div>
                <div className="sdc-metric-row border-top-dash">
                  <span className="smr-label font-semibold">{t.detailWbgtBold}</span>
                  <span className="smr-val num-text color-warn">
                    {HeatStressIndex > 0 ? HeatStressIndex.toFixed(1) : '--'} <span className="smr-unit">°C</span>
                  </span>
                </div>
              </div>
            </div>

          </div>

          {/* Hardware Connection & Message Sync Status */}
          <div className="connection-status-card" style={{ marginTop: '12px', padding: '12px', background: 'rgba(139, 92, 26, 0.03)', borderRadius: 'var(--radius-sm)', border: '1px solid var(--border-light)', display: 'flex', justifyContent: 'space-between', alignItems: 'center', fontSize: '12px' }}>
            <span style={{ color: 'hsl(var(--text-secondary))', fontWeight: '500' }}>
              📡 {lang === 'zh' ? '最后一包数据时间 (Last Message)' : 'Last Message Received'}
            </span>
            <span className="num-text font-bold" style={{ color: 'hsl(var(--text-primary))', display: 'inline-flex', alignItems: 'center', gap: '6px', flexWrap: 'wrap', justifyContent: 'flex-end' }}>
              <span>{lastUpdateTs ? new Date(lastUpdateTs).toLocaleString() : (lang === 'zh' ? '暂无数据' : 'No telemetry data')}</span>
              {lastUpdateTs && (
                <span style={{ fontSize: '11px', fontWeight: 'normal', color: 'hsl(var(--color-accent))' }}>
                  {getRelativeUpdateText()}
                </span>
              )}
            </span>
          </div>

          {/* Action Sections */}
          <div className="actions-section">

            {/* 1. Device Rename Form */}
            <div className="action-card">
              <h3 className="ac-title"><Settings size={18} /> {t.actionRenameTitle}</h3>
              <form onSubmit={handleRenameSubmit} className="rename-form">
                <input 
                  type="text" 
                  value={tempName} 
                  onChange={(e) => setTempName(e.target.value)}
                  className="ac-input"
                  placeholder={t.actionRenamePlaceholder}
                />
                <button type="submit" className="ac-btn font-semibold">
                  <Save size={16} /> {t.actionSave}
                </button>
              </form>
            </div>

            {/* 2. Device Downlink Mute / Dismiss */}
            <div className="action-card">
              <h3 className="ac-title"><VolumeX size={18} /> {t.actionMuteTitle}</h3>
              <p className="ac-desc">{t.actionMuteDesc}</p>
              <button 
                onClick={handleResetAlarm} 
                className="ac-btn btn-danger font-semibold w-full"
                disabled={isResettingAlarm}
              >
                {isResettingAlarm ? (
                  <><RefreshCw size={16} className="loader-icon" /> {t.actionMutingBtn}</>
                ) : (
                  <><VolumeX size={16} /> {t.actionMuteBtn}</>
                )}
              </button>
            </div>

            {/* 3. External WBGT Calibration Form (F2) */}
            <div className="action-card">
              <h3 className="ac-title"><AlertTriangle size={18} /> {t.actionCalibrationTitle}</h3>
              <p className="ac-desc">{t.actionCalibrationDesc}</p>
              <div className="wbgt-slider-wrap">
                <div className="slider-labels">
                  <span>-10.0°C</span>
                  <span className="slider-current num-text">{extWbgt.toFixed(1)}°C</span>
                  <span>45.0°C</span>
                </div>
                <input 
                  type="range" 
                  min="-10" 
                  max="45" 
                  step="0.1" 
                  value={extWbgt} 
                  onChange={(e) => setExtWbgt(parseFloat(e.target.value))}
                  className="wbgt-slider"
                />
              </div>
              <button 
                onClick={handleSendWbgt} 
                className="ac-btn btn-accent font-semibold w-full"
                disabled={isSendingWbgt}
              >
                {isSendingWbgt ? (
                  <><RefreshCw size={16} className="loader-icon" /> {t.actionCalibratingBtn}</>
                ) : (
                  <><AlertTriangle size={16} /> {t.actionCalibrationBtn}</>
                )}
              </button>
            </div>

            {/* 4. Personalized Settings & Aggressive Hydration (F4) */}
            <div className="action-card">
              <h3 className="ac-title"><Droplet size={18} /> {t.actionHydrationTitle}</h3>
              <p className="ac-desc">{t.actionHydrationDesc}</p>
              
              <div className="params-inputs-row">
                <div className="param-input-col">
                  <label>{t.actionAge}</label>
                  <input type="number" value={age} onChange={(e) => setAge(Math.max(1, parseInt(e.target.value) || 0))} className="ac-input-number" />
                </div>
                <div className="param-input-col">
                  <label>{t.actionHeight}</label>
                  <input type="number" value={height} onChange={(e) => setHeight(Math.max(1, parseInt(e.target.value) || 0))} className="ac-input-number" />
                </div>
                <div className="param-input-col">
                  <label>{t.actionWeight}</label>
                  <input type="number" value={weight} onChange={(e) => setWeight(Math.max(1, parseInt(e.target.value) || 0))} className="ac-input-number" />
                </div>
              </div>

              <div className="bmi-display-row">
                <div className="bdr-box">
                  <span className="bdr-label">{t.actionBmi}</span>
                  <span className="bdr-val num-text">{bmi}</span>
                </div>
                <div className="bdr-box">
                  <span className="bdr-label">{t.actionOffset}</span>
                  <span className="bdr-val num-text color-crit">-{wbgtOffset}°C</span>
                </div>
              </div>

              <button onClick={handleSaveParams} className="ac-btn w-full mb-12 font-semibold">
                <Save size={16} /> {t.actionSaveParams}
              </button>

              {/* Dynamic Warning Alert */}
              <div className={`hydration-alert ${isHydrationNeeded ? 'active-alert' : ''}`}>
                <div className="ha-title-row">
                  <Award size={18} />
                  <span>{t.actionHydrationRating}</span>
                </div>
                <p className="ha-desc">
                  {isHydrationNeeded 
                    ? t.actionHydrationNeededAlert.replace('{threshold}', (30.0 - wbgtOffset).toFixed(1))
                    : t.actionHydrationSafeAlert}
                </p>
              </div>

              <button 
                onClick={handleSendHydration} 
                className={`ac-btn btn-droplet w-full font-semibold ${isHydrationNeeded ? 'pulse-safe-glow' : ''}`}
                disabled={isSendingHydration}
              >
                {isSendingHydration ? (
                  <><RefreshCw size={16} className="loader-icon" /> {t.actionHydratingBtn}</>
                ) : (
                  <><Droplet size={16} /> {t.actionHydrationBtn}</>
                )}
              </button>
            </div>

          </div>

        </div>

      </div>

      <style jsx>{`
        .detail-modal-overlay {
          position: fixed;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          background: rgba(26, 18, 10, 0.5); /* Warm Pilbara shadow tint */
          display: flex;
          justify-content: center;
          align-items: flex-end;
          z-index: 100;
          will-change: opacity;
          animation: fade-in 0.2s cubic-bezier(0.25, 1, 0.5, 1);
        }

        @keyframes fade-in {
          from { opacity: 0; }
          to { opacity: 1; }
        }

        .detail-panel {
          width: 100%;
          max-width: 600px;
          height: 90%;
          border-radius: var(--radius-lg) var(--radius-lg) 0 0;
          display: flex;
          flex-direction: column;
          border-bottom: none;
          background: hsl(var(--bg-surface));
          box-shadow: 0 -15px 35px rgba(139, 92, 26, 0.15);
          will-change: transform;
          animation: slide-up 0.28s cubic-bezier(0.34, 1.56, 0.64, 1); /* Extremely responsive spring-out curve */
        }

        @keyframes slide-up {
          from { transform: translateY(100%); }
          to { transform: translateY(0); }
        }

        @media (min-width: 768px) {
          .detail-modal-overlay {
            align-items: center;
            padding: 20px;
          }
          .detail-panel {
            height: 85%;
            border-radius: var(--radius-md);
            border-bottom: 1px solid var(--border-light);
          }
        }

        .panel-header {
          padding: 20px 24px;
          display: flex;
          justify-content: space-between;
          align-items: center;
          border-bottom: 1px solid var(--border-light);
        }

        .panel-title-area {
          display: flex;
          flex-direction: column;
          gap: 4px;
        }

        .panel-title {
          font-size: 18px;
          font-weight: 700;
          color: hsl(var(--text-primary));
        }

        .dev-name-tag {
          font-size: 12px;
          font-family: monospace;
          color: hsl(var(--text-muted));
          font-weight: normal;
        }

        .device-type-badge {
          font-size: 10px;
          background: rgba(139, 92, 26, 0.05);
          color: hsl(var(--text-secondary));
          border: 1px solid var(--border-light);
          padding: 2px 6px;
          border-radius: 4px;
          width: fit-content;
        }

        .close-btn {
          width: 36px;
          height: 36px;
          border-radius: 50%;
          background: rgba(139, 92, 26, 0.04);
          border: 1px solid var(--border-light);
          color: hsl(var(--text-secondary));
          display: flex;
          align-items: center;
          justify-content: center;
          cursor: pointer;
          transition: all 0.2s;
        }

        .close-btn:hover {
          color: hsl(var(--text-primary));
          background: rgba(139, 92, 26, 0.08);
        }

        .status-banner {
          padding: 12px 24px;
          font-size: 13px;
          font-weight: 600;
          text-align: center;
        }

        .banner-success {
          background: rgba(21, 128, 61, 0.08);
          color: hsl(var(--color-safe));
          border-bottom: 1px solid rgba(21, 128, 61, 0.12);
        }

        .banner-error {
          background: rgba(220, 38, 38, 0.08);
          color: hsl(var(--color-critical));
          border-bottom: 1px solid rgba(220, 38, 38, 0.12);
        }

        .panel-scroll-content {
          flex: 1;
          overflow-y: auto;
          padding: 20px 24px 40px;
          display: flex;
          flex-direction: column;
          gap: 20px;
        }

        .risk-banner {
          border: 1px solid;
          border-radius: var(--radius-md);
          padding: 14px 18px;
          display: flex;
          align-items: center;
          gap: 12px;
        }

        .risk-banner-text {
          flex: 1;
        }

        .rb-title {
          font-size: 11px;
          opacity: 0.8;
          text-transform: uppercase;
          letter-spacing: 0.05em;
        }

        .rb-val {
          font-size: 15px;
          margin-top: 2px;
        }

        .reset-alarm-btn-mini {
          display: flex;
          align-items: center;
          gap: 6px;
          background: hsl(var(--color-critical));
          border: none;
          color: #fff;
          font-size: 11px;
          font-weight: 700;
          padding: 6px 12px;
          border-radius: 6px;
          cursor: pointer;
        }

        .reset-alarm-btn-mini:hover {
          background: #ff5e62;
        }

        .detail-grid {
          display: grid;
          grid-template-columns: 1fr;
          gap: 16px;
        }

        @media (min-width: 480px) {
          .detail-grid {
            grid-template-columns: 1fr 1fr;
          }
        }

        .sub-detail-card {
          background: rgba(139, 92, 26, 0.02);
          border: 1px solid var(--border-light);
          border-radius: var(--radius-md);
          padding: 16px;
          display: flex;
          flex-direction: column;
          gap: 12px;
        }

        .sdc-title {
          font-size: 13px;
          font-weight: 600;
          color: hsl(var(--text-secondary));
          display: flex;
          align-items: center;
          gap: 6px;
          text-transform: uppercase;
          letter-spacing: 0.05em;
        }

        .color-crit {
          color: hsl(var(--color-critical));
        }

        .color-acc {
          color: hsl(var(--color-accent));
        }

        .color-warn {
          color: hsl(var(--color-warning));
        }

        .sdc-metrics {
          display: flex;
          flex-direction: column;
          gap: 10px;
        }

        .sdc-metric-row {
          display: flex;
          justify-content: space-between;
          align-items: center;
        }

        .border-top-dash {
          border-top: 1px dashed var(--border-light);
          padding-top: 8px;
          margin-top: 4px;
        }

        .smr-label {
          font-size: 13px;
          color: hsl(var(--text-muted));
        }

        .smr-val {
          font-size: 18px;
          color: hsl(var(--text-primary));
        }

        .smr-unit {
          font-size: 11px;
          color: hsl(var(--text-secondary));
          margin-left: 2px;
        }

        .sdc-gauge-bar {
          width: 100%;
          height: 6px;
          background: rgba(139, 92, 26, 0.06);
          border-radius: 3px;
          overflow: hidden;
          margin-top: 4px;
        }

        .sgb-fill {
          height: 100%;
          transition: width 0.5s ease-out;
        }

        .actions-section {
          display: flex;
          flex-direction: column;
          gap: 16px;
          margin-top: 8px;
        }

        .action-card {
          background: rgba(139, 92, 26, 0.02);
          border: 1px solid var(--border-light);
          border-radius: var(--radius-md);
          padding: 20px;
          display: flex;
          flex-direction: column;
          gap: 14px;
        }

        .ac-title {
          font-size: 15px;
          font-weight: 600;
          color: hsl(var(--text-primary));
          display: flex;
          align-items: center;
          gap: 8px;
        }

        .ac-desc {
          font-size: 12px;
          color: hsl(var(--text-secondary));
          line-height: 1.5;
        }

        .rename-form {
          display: flex;
          gap: 8px;
        }

        .ac-input {
          flex: 1;
          background: rgba(255, 255, 255, 0.8);
          border: 1px solid var(--border-light);
          border-radius: var(--radius-sm);
          color: hsl(var(--text-primary));
          font-size: 13px;
          padding: 8px 12px;
        }

        .ac-input:focus {
          border-color: hsl(var(--color-accent));
        }

        .ac-btn {
          background: rgba(139, 92, 26, 0.05);
          border: 1px solid var(--border-light);
          color: hsl(var(--text-primary));
          font-size: 13px;
          padding: 8px 16px;
          border-radius: var(--radius-sm);
          cursor: pointer;
          display: flex;
          align-items: center;
          justify-content: center;
          gap: 8px;
          transition: all 0.2s;
        }

        .ac-btn:hover:not(:disabled) {
          background: rgba(139, 92, 26, 0.1);
        }

        .w-full {
          width: 100%;
        }

        .mb-12 {
          margin-bottom: 12px;
        }

        .btn-danger {
          background: rgba(220, 38, 38, 0.06);
          border-color: rgba(220, 38, 38, 0.15);
          color: hsl(var(--color-critical));
        }

        .btn-danger:hover:not(:disabled) {
          background: rgba(220, 38, 38, 0.12);
        }

        .btn-accent {
          background: rgba(215, 80, 40, 0.06);
          border-color: rgba(215, 80, 40, 0.15);
          color: hsl(var(--color-accent));
        }

        .btn-accent:hover:not(:disabled) {
          background: rgba(215, 80, 40, 0.12);
        }

        .btn-droplet {
          background: rgba(21, 128, 61, 0.06);
          border-color: rgba(21, 128, 61, 0.15);
          color: hsl(var(--color-safe));
        }

        .btn-droplet:hover:not(:disabled) {
          background: rgba(21, 128, 61, 0.12);
        }

        .wbgt-slider-wrap {
          display: flex;
          flex-direction: column;
          gap: 6px;
          padding: 8px 0;
        }

        .slider-labels {
          display: flex;
          justify-content: space-between;
          font-size: 11px;
          color: hsl(var(--text-muted));
        }

        .slider-current {
          font-size: 14px;
          font-weight: 600;
          color: hsl(var(--color-accent));
        }

        .wbgt-slider {
          -webkit-appearance: none;
          width: 100%;
          height: 6px;
          border-radius: 3px;
          background: rgba(139, 92, 26, 0.08);
          outline: none;
        }

        .wbgt-slider::-webkit-slider-thumb {
          -webkit-appearance: none;
          appearance: none;
          width: 16px;
          height: 16px;
          border-radius: 50%;
          background: hsl(var(--color-accent));
          box-shadow: 0 0 10px rgba(215, 80, 40, 0.3);
          cursor: pointer;
        }

        .params-inputs-row {
          display: grid;
          grid-template-columns: 1fr 1fr 1fr;
          gap: 10px;
          margin-top: 4px;
        }

        .param-input-col {
          display: flex;
          flex-direction: column;
          gap: 6px;
        }

        .param-input-col label {
          font-size: 11px;
          color: hsl(var(--text-muted));
        }

        .ac-input-number {
          width: 100%;
          background: rgba(255, 255, 255, 0.8);
          border: 1px solid var(--border-light);
          border-radius: var(--radius-sm);
          color: hsl(var(--text-primary));
          font-size: 13px;
          padding: 8px;
          text-align: center;
        }

        .bmi-display-row {
          display: grid;
          grid-template-columns: 1fr 1fr;
          gap: 12px;
          padding: 10px 0;
        }

        .bdr-box {
          background: rgba(139, 92, 26, 0.01);
          border: 1px solid var(--border-light);
          border-radius: var(--radius-sm);
          padding: 8px 12px;
          display: flex;
          flex-direction: column;
          align-items: center;
          gap: 4px;
        }

        .bdr-label {
          font-size: 10px;
          color: hsl(var(--text-muted));
        }

        .bdr-val {
          font-size: 16px;
          font-weight: 700;
          color: hsl(var(--text-primary));
        }

        .hydration-alert {
          border: 1px solid var(--border-light);
          background: rgba(139, 92, 26, 0.01);
          border-radius: var(--radius-sm);
          padding: 12px;
          display: flex;
          flex-direction: column;
          gap: 6px;
        }

        .active-alert {
          background: rgba(194, 65, 12, 0.06);
          border-color: rgba(194, 65, 12, 0.15);
          color: hsl(var(--color-warning));
        }

        .ha-title-row {
          display: flex;
          align-items: center;
          gap: 8px;
          font-size: 12px;
          font-weight: 700;
        }

        .ha-desc {
          font-size: 11px;
          line-height: 1.4;
          opacity: 0.9;
        }

        .pulse-safe-glow {
          box-shadow: 0 0 10px rgba(21, 128, 61, 0.2);
          animation: pulse-safe 2s infinite;
        }

        .loader-icon {
          animation: loader-rotate 1.5s infinite linear;
        }

        .history-chart-card {
          display: flex;
          flex-direction: column;
          gap: 16px;
        }

        .history-header {
          display: flex;
          justify-content: space-between;
          align-items: center;
        }

        .metric-selector {
          display: flex;
          flex-wrap: wrap;
          gap: 6px;
          border-bottom: 1px solid var(--border-light);
          padding-bottom: 12px;
        }

        .metric-select-btn {
          background: transparent;
          border: 1px solid var(--border-light);
          padding: 6px 12px;
          border-radius: var(--radius-sm);
          font-size: 11px;
          font-weight: 600;
          cursor: pointer;
          transition: all 0.2s ease;
        }

        .metric-select-btn:hover {
          background: rgba(139, 92, 26, 0.04);
        }

        .metric-select-btn.active {
          font-weight: 700;
        }

        .chart-canvas-container {
          width: 100%;
          min-height: 180px;
          position: relative;
        }

        .chart-loading {
          display: flex;
          flex-direction: column;
          align-items: center;
          justify-content: center;
          gap: 12px;
          padding: 40px 0;
          color: hsl(var(--text-secondary));
          font-size: 13px;
        }

        .history-svg {
          width: 100%;
          height: auto;
          overflow: visible;
        }

        .chart-tooltip {
          position: absolute;
          background: hsl(var(--bg-surface));
          border: 1.5px solid;
          border-radius: var(--radius-sm);
          padding: 6px 10px;
          box-shadow: 0 4px 12px rgba(139, 92, 26, 0.15);
          pointer-events: none;
          font-size: 11px;
          transform: translate(-50%, -120%);
          z-index: 10;
          display: flex;
          flex-direction: column;
          gap: 2px;
          transition: left 0.1s ease-out, top 0.1s ease-out;
        }

        .tooltip-time {
          color: hsl(var(--text-secondary));
          font-family: monospace;
          font-size: 9px;
        }

        .tooltip-value {
          font-size: 13px;
          display: flex;
          align-items: baseline;
          gap: 2px;
        }

        .tooltip-unit {
          font-size: 9px;
          font-weight: normal;
        }
      `}</style>
    </div>
  );
};

export const MinerDetail = React.memo(MinerDetailComponent, (prevProps, nextProps) => {
  return (
    prevProps.deviceId === nextProps.deviceId &&
    prevProps.deviceType === nextProps.deviceType &&
    prevProps.name === nextProps.name &&
    prevProps.minerName === nextProps.minerName &&
    prevProps.lang === nextProps.lang &&
    JSON.stringify(prevProps.telemetry) === JSON.stringify(nextProps.telemetry)
  );
});
