import React from 'react';
import { Thermometer, Battery, AlertTriangle, User, Activity } from 'lucide-react';
import { TelemetryValues } from '@/lib/thingsboard';
import { translations, Language } from '@/lib/translations';

interface MinerCardProps {
  deviceId: string;
  deviceType: string;
  name: string; // The ThingsBoard name
  minerName: string; // The custom name (from LocalStorage)
  telemetry: TelemetryValues;
  lang: Language;
  onClick: () => void;
}

const MinerCardComponent: React.FC<MinerCardProps> = ({
  deviceId,
  name,
  minerName,
  telemetry,
  lang,
  onClick,
}) => {
  const {
    skinTemp = 0,
    ambientTemp = 0,
    HeatStressIndex = 0,
    riskLevel = 0,
    fallDetected = false,
    battery = 0,
    lastUpdateTs,
    tc = 0
  } = telemetry;

  const displayTc = tc > 0 ? tc : (skinTemp > 0 ? skinTemp + 4.0 : 37.0);

  const t = translations[lang];

  // Determine risk configuration (styles & text)
  let riskColorClass = 'safe';
  let riskLabel = t.cardSafe;
  let isAlarmPulsing = false;

  if (fallDetected) {
    riskColorClass = 'critical';
    riskLabel = t.cardFall;
    isAlarmPulsing = true;
  } else if (riskLevel >= 2) {
    riskColorClass = 'critical';
    riskLabel = t.cardCritical;
    isAlarmPulsing = true;
  } else if (riskLevel === 1) {
    riskColorClass = 'warning';
    riskLabel = t.cardWarning;
  }

  // Normalize battery to fractional scale and handle display
  const normBattery = battery > 1 ? battery / 100 : battery;
  const displayBatteryPercent = battery > 1 ? Math.round(battery) : Math.round(battery * 100);

  // Battery status color
  let batteryColor = 'var(--color-safe)';
  if (normBattery <= 0.2) batteryColor = 'var(--color-critical)';
  else if (normBattery <= 0.5) batteryColor = 'var(--color-warning)';

  // Format time since last update
  const getUpdateTimeText = () => {
    if (!lastUpdateTs) return lang === 'zh' ? '无数据' : 'No data';
    const secondsAgo = Math.floor((Date.now() - lastUpdateTs) / 1000);
    if (secondsAgo < 10) return t.cardJustNow;
    if (secondsAgo < 60) return `${secondsAgo}${t.cardSecondsAgo}`;
    const minutesAgo = Math.floor(secondsAgo / 60);
    return `${minutesAgo}${t.cardMinutesAgo}`;
  };

  const isOffline = lastUpdateTs ? (Date.now() - lastUpdateTs > 90 * 1000) : true;

  return (
    <div 
      className={`glass-card card-wrapper risk-${riskColorClass} ${isAlarmPulsing ? 'pulse-active' : ''} ${isOffline ? 'offline-card' : ''}`}
      onClick={onClick}
      style={{ cursor: 'pointer' }}
    >
      {/* Top Bar EUI & Battery */}
      <div className="card-top">
        <span className="device-id-label">{name}</span>
        <div className="battery-indicator" style={{ color: batteryColor }}>
          <Battery size={16} />
          <span className="num-text font-semibold">{displayBatteryPercent}%</span>
        </div>
      </div>

      {/* Miner Info */}
      <div className="miner-identity">
        <div className={`avatar-circle ${fallDetected ? 'avatar-emergency' : ''}`}>
          {fallDetected ? (
            <AlertTriangle size={20} className="emergency-fall-icon" />
          ) : (
            <User size={20} />
          )}
        </div>
        <div className="miner-text">
          <h3 className="miner-display-name">{minerName || name}</h3>
          <span className={`status-pill pill-${riskColorClass}`}>
            {isOffline ? t.cardOffline : riskLabel}
          </span>
        </div>
      </div>

      {/* Real-time Vital Metrics */}
      <div className="metrics-grid">
        {/* Metric 1: Core Temp (Tc) / Ambient Temp */}
        <div className="mini-metric">
          <div className="metric-icon-wrap bg-temp">
            <Thermometer size={16} />
          </div>
          <div className="metric-info">
            <span className="metric-label">{t.cardCoreAmbientTemp}</span>
            <span className="metric-value num-text">
              {displayTc.toFixed(1)}
              <span className="metric-unit">°C</span>
              <span style={{ margin: '0 4px', opacity: 0.5 }}>/</span>
              {ambientTemp > 0 ? ambientTemp.toFixed(1) : '--'}
              <span className="metric-unit">°C</span>
            </span>
          </div>
        </div>

        {/* Metric 2: Risk Level */}
        <div className="mini-metric">
          <div className={`metric-icon-wrap ${fallDetected ? 'bg-heart avatar-emergency' : 'bg-risk'}`}>
            {fallDetected ? (
              <AlertTriangle size={16} className="emergency-fall-icon" />
            ) : (
              <Activity size={16} />
            )}
          </div>
          <div className="metric-info">
            <span className="metric-label">{t.cardRiskLevel}</span>
            <span className={`metric-value num-text ${fallDetected ? 'color-crit font-bold' : ''}`} style={{ fontSize: '13px' }}>
              {fallDetected ? (
                <span style={{ display: 'inline-flex', alignItems: 'center', gap: '4px' }}>
                  {riskLabel} <span className="emergency-blink">⚠️</span>
                </span>
              ) : (
                isOffline ? t.cardOffline : riskLabel
              )}
            </span>
          </div>
        </div>

        {/* Metric 3: Heat Stress (WBGT) */}
        <div className="mini-metric">
          <div className="metric-icon-wrap bg-wbgt">
            <AlertTriangle size={16} />
          </div>
          <div className="metric-info">
            <span className="metric-label">{t.cardWbgt}</span>
            <span className="metric-value num-text">{HeatStressIndex > 0 ? `${HeatStressIndex.toFixed(1)}` : '--'}<span className="metric-unit">°C</span></span>
          </div>
        </div>
      </div>

      {/* Footer Sync time */}
      <div className="card-footer">
        <span className="sync-text">{t.cardLastSync}: {getUpdateTimeText()}</span>
        {isOffline && <span className="offline-warning">{t.cardOfflineWarning}</span>}
      </div>

      <style jsx>{`
        .card-wrapper {
          display: flex;
          flex-direction: column;
          gap: 16px;
          position: relative;
          overflow: hidden;
          transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
          border-left: 4px solid var(--border-glow);
        }

        .risk-safe {
          border-left-color: hsl(var(--color-safe));
        }

        .risk-warning {
          border-left-color: hsl(var(--color-warning));
          background: rgba(194, 65, 12, 0.02);
        }

        .risk-critical {
          border-left-color: hsl(var(--color-critical));
          background: rgba(220, 38, 38, 0.04);
        }

        .pulse-active {
          animation: pulse-critical 2s infinite;
        }

        .offline-card {
          opacity: 0.65;
          border-left-color: hsl(var(--text-muted));
        }

        .card-top {
          display: flex;
          justify-content: space-between;
          align-items: center;
          font-size: 11px;
        }

        .device-id-label {
          color: hsl(var(--text-secondary));
          font-weight: 600;
          font-family: monospace;
          letter-spacing: 0.05em;
        }

        .battery-indicator {
          display: flex;
          align-items: center;
          gap: 4px;
          font-size: 13px;
        }

        .miner-identity {
          display: flex;
          align-items: center;
          gap: 12px;
        }

        .avatar-circle {
          width: 42px;
          height: 42px;
          border-radius: 50%;
          background: rgba(139, 92, 26, 0.05);
          border: 1px solid var(--border-light);
          display: flex;
          align-items: center;
          justify-content: center;
          color: hsl(var(--text-secondary));
          transition: all 0.3s ease;
        }

        .avatar-emergency {
          background: rgba(220, 38, 38, 0.15) !important;
          border-color: hsl(var(--color-critical)) !important;
          animation: avatar-blink 0.8s infinite alternate !important;
        }

        .emergency-fall-icon {
          color: hsl(var(--color-critical)) !important;
        }

        .emergency-blink {
          animation: text-blink 0.8s infinite alternate;
        }

        @keyframes avatar-blink {
          from { transform: scale(1); background: rgba(220, 38, 38, 0.15); }
          to { transform: scale(1.15); background: rgba(220, 38, 38, 0.35); }
        }

        @keyframes text-blink {
          from { opacity: 0.3; }
          to { opacity: 1; }
        }

        .miner-text {
          display: flex;
          flex-direction: column;
          gap: 4px;
        }

        .miner-display-name {
          font-size: 16px;
          font-weight: 600;
          color: hsl(var(--text-primary));
        }

        .status-pill {
          display: inline-block;
          font-size: 11px;
          font-weight: 700;
          padding: 2px 8px;
          border-radius: 4px;
          width: fit-content;
          text-transform: uppercase;
        }

        .pill-safe {
          background: rgba(21, 128, 61, 0.15);
          color: hsl(var(--color-safe));
        }

        .pill-warning {
          background: rgba(194, 65, 12, 0.15);
          color: hsl(var(--color-warning));
        }

        .pill-critical {
          background: rgba(220, 38, 38, 0.15);
          color: hsl(var(--color-critical));
          animation: blink 1s infinite alternate;
        }

        @keyframes blink {
          0% { opacity: 0.8; }
          100% { opacity: 1; }
        }

        .metrics-grid {
          display: grid;
          grid-template-columns: 1fr;
          gap: 12px;
          padding: 8px 0;
          border-top: 1px dashed var(--border-light);
          border-bottom: 1px dashed var(--border-light);
        }

        .mini-metric {
          display: flex;
          align-items: center;
          gap: 10px;
        }

        .metric-icon-wrap {
          width: 32px;
          height: 32px;
          border-radius: 8px;
          display: flex;
          align-items: center;
          justify-content: center;
        }

        .bg-heart {
          background: rgba(220, 38, 38, 0.08);
          color: hsl(var(--color-critical));
        }

        .bg-risk {
          background: rgba(139, 92, 26, 0.06);
          color: hsl(var(--text-secondary));
        }

        .color-crit {
          color: hsl(var(--color-critical)) !important;
        }

        .bg-temp {
          background: rgba(215, 80, 40, 0.08);
          color: hsl(var(--color-accent));
        }

        .bg-wbgt {
          background: rgba(194, 65, 12, 0.08);
          color: hsl(var(--color-warning));
        }

        .heart-beat-icon {
          animation: heartbeat 1s infinite alternate;
        }

        @keyframes heartbeat {
          0% { transform: scale(0.95); }
          100% { transform: scale(1.1); }
        }

        .metric-info {
          display: flex;
          flex-direction: column;
        }

        .metric-label {
          font-size: 11px;
          font-weight: 600;
          color: hsl(var(--text-secondary));
          text-transform: uppercase;
          letter-spacing: 0.02em;
        }

        .metric-value {
          font-size: 15px;
          font-weight: 700;
          color: hsl(var(--text-primary));
        }

        .metric-unit {
          font-size: 10px;
          color: hsl(var(--text-secondary));
          margin-left: 2px;
        }

        .card-footer {
          display: flex;
          justify-content: space-between;
          align-items: center;
          font-size: 11px;
          color: hsl(var(--text-secondary));
          font-weight: 600;
        }

        .offline-warning {
          color: hsl(var(--color-critical));
          font-weight: 600;
        }
      `}</style>
    </div>
  );
};

export const MinerCard = React.memo(MinerCardComponent, (prevProps, nextProps) => {
  return (
    prevProps.deviceId === nextProps.deviceId &&
    prevProps.deviceType === nextProps.deviceType &&
    prevProps.name === nextProps.name &&
    prevProps.minerName === nextProps.minerName &&
    prevProps.lang === nextProps.lang &&
    JSON.stringify(prevProps.telemetry) === JSON.stringify(nextProps.telemetry)
  );
});
