'use strict';

'use client';

import React, { useState, useEffect } from 'react';
import { 
  Users, Activity, AlertOctagon, RefreshCw, 
  MapPin, ShieldAlert, Wifi, HardDrive, Languages
} from 'lucide-react';
import { TelemetryValues } from '@/lib/thingsboard';
import { MinerCard } from '@/components/MinerCard';
import { MinerDetail } from '@/components/MinerDetail';
import { translations, Language } from '@/lib/translations';

interface Device {
  id: string;
  name: string;
  type: string;
  label: string;
}

export default function Home() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [telemetryMap, setTelemetryMap] = useState<Record<string, TelemetryValues>>({});
  const [minerNames, setMinerNames] = useState<Record<string, string>>({});
  const [selectedDeviceId, setSelectedDeviceId] = useState<string | null>(null);
  
  // Localization State
  const [lang, setLang] = useState<Language>('zh');

  // Loading and error states
  const [isLoadingDevices, setIsLoadingDevices] = useState<boolean>(true);
  const [isSyncingTelemetry, setIsSyncingTelemetry] = useState<boolean>(false);
  const [error, setError] = useState<string | null>(null);
  const [lastSyncTime, setLastSyncTime] = useState<Date | null>(null);

  // Filter: Show all devices or only HeatStress Wearables
  const [filterType, setFilterType] = useState<'wearables' | 'all'>('wearables');

  // Load language and names registry from LocalStorage on mount
  useEffect(() => {
    const savedLang = localStorage.getItem('heatwatch_lang');
    if (savedLang === 'zh' || savedLang === 'en') {
      setLang(savedLang);
    }

    const savedNames = localStorage.getItem('heatwatch_miner_renames');
    if (savedNames) {
      try {
        setMinerNames(JSON.parse(savedNames));
      } catch (e) {
        console.error('Error loading names registry from local storage:', e);
      }
    }

    // Fetch device list
    const loadDevices = async () => {
      setIsLoadingDevices(true);
      setError(null);
      try {
        const response = await fetch('/api/devices');
        const data = await response.json();
        
        if (data.success && data.devices) {
          setDevices(data.devices);
          
          // Prime telemetry map with empty values
          const initialMap: Record<string, TelemetryValues> = {};
          data.devices.forEach((dev: Device) => {
            initialMap[dev.id] = {};
          });
          setTelemetryMap(initialMap);
        } else {
          throw new Error(data.error || '无法加载设备列表');
        }
      } catch (err: any) {
        console.error('Error loading devices:', err);
        setError(err.message || '与 ThingsBoard API 连接失败，请检查 .env.local 凭据。');
      } finally {
        setIsLoadingDevices(false);
      }
    };

    loadDevices();
  }, []);

  // Telemetry Polling (every 5 seconds)
  useEffect(() => {
    if (devices.length === 0) return;

    const fetchAllTelemetry = async () => {
      setIsSyncingTelemetry(true);
      try {
        const telemetryResults = await Promise.all(
          devices.map(async (dev) => {
            try {
              const res = await fetch(`/api/devices/${dev.id}/telemetry`);
              const data = await res.json();
              if (data.success && data.telemetry) {
                return { id: dev.id, telemetry: data.telemetry };
              }
            } catch (e) {
              console.error(`Error polling telemetry for ${dev.id}:`, e);
            }
            return null;
          })
        );

        // Batch all telemetry results into a single object update
        const updatedTelemetry: Record<string, TelemetryValues> = {};
        telemetryResults.forEach((result) => {
          if (result) {
            updatedTelemetry[result.id] = result.telemetry;
          }
        });

        // Trigger EXACTLY ONE state update, eliminating redundant multi-renders
        setTelemetryMap(prev => ({
          ...prev,
          ...updatedTelemetry
        }));

        setLastSyncTime(new Date());
      } catch (err) {
        console.error('Error syncing telemetry map:', err);
      } finally {
        setIsSyncingTelemetry(false);
      }
    };

    fetchAllTelemetry();
    const intervalId = setInterval(fetchAllTelemetry, 5000);
    return () => clearInterval(intervalId);
  }, [devices]);

  const handleLangToggle = (selectedLang: Language) => {
    setLang(selectedLang);
    localStorage.setItem('heatwatch_lang', selectedLang);
  };

  const handleRenameMiner = (deviceId: string, newName: string) => {
    const updatedNames = {
      ...minerNames,
      [deviceId]: newName
    };
    setMinerNames(updatedNames);
    localStorage.setItem('heatwatch_miner_renames', JSON.stringify(updatedNames));
  };

  // Filter logic
  const filteredDevices = devices.filter(dev => {
    if (filterType === 'wearables') {
      return dev.type === 'heatstress-group10';
    }
    return true; // Show all
  });

  // Calculate Statistics
  const totalMinersCount = filteredDevices.length;
  let activeWarningCount = 0;
  let activeCriticalCount = 0;
  let activeFallCount = 0;

  filteredDevices.forEach(dev => {
    const tel = telemetryMap[dev.id];
    if (tel) {
      if (tel.fallDetected) {
        activeFallCount++;
        activeCriticalCount++;
      } else if (tel.riskLevel && tel.riskLevel >= 2) {
        activeCriticalCount++;
      } else if (tel.riskLevel === 1) {
        activeWarningCount++;
      }
    }
  });

  const selectedDevice = devices.find(d => d.id === selectedDeviceId);
  const selectedTelemetry = selectedDeviceId ? telemetryMap[selectedDeviceId] : null;

  // Active translation dictionary
  const t = translations[lang];

  return (
    <>
      {/* Premium Outback Sci-Fi Backgrounds */}
      <div className="grid-bg" />
      <div className="glow-overlay" />

      {/* Screen flash on fall alerts */}
      <div className={`page-content-container ${activeFallCount > 0 ? 'flash-active' : ''}`}>
        
        {/* Sticky Header */}
        <header className="app-header">
          <div className="app-title">
            {t.appTitle}
          </div>
          <div className="header-meta">
            {/* Language Switcher */}
            <div className="lang-switcher-wrap">
              <Languages size={15} className="lang-icon" />
              <button 
                onClick={() => handleLangToggle('zh')} 
                className={`lang-btn ${lang === 'zh' ? 'active' : ''}`}
              >
                中
              </button>
              <span className="lang-divider">|</span>
              <button 
                onClick={() => handleLangToggle('en')} 
                className={`lang-btn ${lang === 'en' ? 'active' : ''}`}
              >
                EN
              </button>
            </div>

            <div className="sync-status">
              {isSyncingTelemetry ? (
                <RefreshCw size={14} className="loader-icon color-acc" />
              ) : (
                <span className="last-sync-badge">
                  {t.lastSync}: {lastSyncTime ? lastSyncTime.toLocaleTimeString() : '...'}
                </span>
              )}
            </div>

            <div className="connection-pill online">
              <span className="online-dot pulsing" />
              <span>{t.tbCloud}</span>
            </div>
          </div>
        </header>

        {/* Global Alert Banner for Falls */}
        {activeFallCount > 0 && (
          <div className="global-emergency-banner">
            <ShieldAlert size={20} className="blink-emergency" />
            <span>{t.globalFallWarning}</span>
          </div>
        )}

        <main className="main-container">
          
          {/* 1. Project Intro & Info Header */}
          <div className="project-intro-header">
            <div className="pih-left">
              <span className="pih-tag"><MapPin size={12} /> {t.siteTag}</span>
              <h1 className="pih-title">{t.dashboardTitle}</h1>
            </div>
            
            {/* Filter Toggle Pill */}
            <div className="filter-pills-wrap">
              <button 
                onClick={() => setFilterType('wearables')} 
                className={`filter-pill ${filterType === 'wearables' ? 'pill-active' : ''}`}
              >
                {t.filterWearables} ({devices.filter(d => d.type === 'heatstress-group10').length})
              </button>
              <button 
                onClick={() => setFilterType('all')} 
                className={`filter-pill ${filterType === 'all' ? 'pill-active' : ''}`}
              >
                {t.filterAll} ({devices.length})
              </button>
            </div>
          </div>

          {/* 2. KPI Summary Cards Panel */}
          <section className="kpi-panel">
            
            <div className="kpi-card glass-card">
              <div className="kpi-icon-wrap bg-primary">
                <Users size={20} />
              </div>
              <div className="kpi-text-area">
                <span className="kpi-label">{t.kpiTotalMiners}</span>
                <span className="kpi-value num-text">{totalMinersCount} <span className="kpi-unit">{t.unitMiners}</span></span>
              </div>
            </div>

            <div className="kpi-card glass-card warning-kpi">
              <div className="kpi-icon-wrap bg-warning">
                <Activity size={20} />
              </div>
              <div className="kpi-text-area">
                <span className="kpi-label">{t.kpiWarnings}</span>
                <span className="kpi-value num-text color-warn">{activeWarningCount} <span className="kpi-unit">{t.unitMiners}</span></span>
              </div>
            </div>

            <div className="kpi-card glass-card danger-kpi">
              <div className="kpi-icon-wrap bg-danger">
                <AlertOctagon size={20} />
              </div>
              <div className="kpi-text-area">
                <span className="kpi-label">{t.kpiCritical}</span>
                <span className="kpi-value num-text color-crit">{activeCriticalCount} <span className="kpi-unit">{t.unitCases}</span></span>
              </div>
            </div>

          </section>

          {/* 3. Loader & Error states */}
          {isLoadingDevices && (
            <div className="loader-container">
              <RefreshCw className="loader-icon color-acc" size={36} />
              <p className="loader-text">{t.loadingDevices}</p>
            </div>
          )}

          {error && (
            <div className="error-card glass-card">
              <ShieldAlert size={32} className="color-crit" />
              <div className="error-details">
                <h3>{t.failedAccess}</h3>
                <p>{error}</p>
                <div className="error-suggestions">
                  <p>{t.errorSuggestions}</p>
                  <p>{t.errorSuggestion1}</p>
                  <p>{t.errorSuggestion2}</p>
                </div>
              </div>
            </div>
          )}

          {/* 4. Main Device Grid */}
          {!isLoadingDevices && !error && (
            <>
              {filteredDevices.length === 0 ? (
                <div className="empty-state-card glass-card">
                  <HardDrive size={32} />
                  <p>{t.emptyDevices}</p>
                  <span className="es-tip">{t.emptyDevicesTip}</span>
                </div>
              ) : (
                <div className="devices-grid">
                  {filteredDevices.map((dev) => {
                    const tel = telemetryMap[dev.id] || {};
                    return (
                      <MinerCard 
                        key={dev.id}
                        deviceId={dev.id}
                        deviceType={dev.type}
                        name={dev.name}
                        minerName={minerNames[dev.id] || ''}
                        telemetry={tel}
                        lang={lang}
                        onClick={() => setSelectedDeviceId(dev.id)}
                      />
                    );
                  })}
                </div>
              )}
            </>
          )}

        </main>
        
        {/* Drawer Modal Overlay */}
        {selectedDeviceId && selectedDevice && (
          <MinerDetail 
            deviceId={selectedDeviceId}
            deviceType={selectedDevice.type}
            name={selectedDevice.name}
            minerName={minerNames[selectedDeviceId] || ''}
            telemetry={selectedTelemetry || {}}
            lang={lang}
            onClose={() => setSelectedDeviceId(null)}
            onRename={(newName) => handleRenameMiner(selectedDeviceId, newName)}
          />
        )}

      </div>

      <style jsx>{`
        .page-content-container {
          min-height: 100vh;
          display: flex;
          flex-direction: column;
          z-index: 1;
        }

        .header-meta {
          display: flex;
          align-items: center;
          gap: 16px;
        }

        .lang-switcher-wrap {
          display: inline-flex;
          align-items: center;
          gap: 6px;
          background: rgba(139, 92, 26, 0.05);
          border: 1px solid var(--border-light);
          padding: 4px 8px;
          border-radius: 6px;
        }

        .lang-icon {
          color: hsl(var(--text-secondary));
          margin-right: 2px;
        }

        .lang-btn {
          background: transparent;
          border: none;
          color: hsl(var(--text-muted));
          font-size: 11px;
          font-weight: 800;
          cursor: pointer;
          transition: color 0.2s;
          padding: 2px 4px;
        }

        .lang-btn.active {
          color: hsl(var(--color-accent));
        }

        .lang-divider {
          color: var(--border-light);
          font-size: 10px;
        }

        .sync-status {
          display: flex;
          align-items: center;
        }

        .last-sync-badge {
          font-size: 11px;
          color: hsl(var(--text-secondary));
          font-weight: 700;
          background: rgba(139, 92, 26, 0.06);
          border: 1px solid var(--border-light);
          padding: 4px 8px;
          border-radius: 4px;
        }

        .global-emergency-banner {
          background: hsl(var(--color-critical));
          color: #fff;
          font-size: 13px;
          font-weight: 700;
          padding: 10px 16px;
          text-align: center;
          display: flex;
          align-items: center;
          justify-content: center;
          gap: 10px;
          z-index: 9;
          animation: slide-down-banner 0.3s cubic-bezier(0.16, 1, 0.3, 1);
        }

        @keyframes slide-down-banner {
          from { transform: translateY(-100%); }
          to { transform: translateY(0); }
        }

        .blink-emergency {
          animation: blink-anim 0.8s infinite alternate;
        }

        @keyframes blink-anim {
          from { opacity: 0.4; transform: scale(0.9); }
          to { opacity: 1; transform: scale(1.1); }
        }

        .project-intro-header {
          display: flex;
          flex-direction: column;
          gap: 14px;
          margin-bottom: 24px;
        }

        @media (min-width: 768px) {
          .project-intro-header {
            flex-direction: row;
            justify-content: space-between;
            align-items: flex-end;
          }
        }

        .pih-tag {
          font-size: 11px;
          font-weight: 800;
          color: hsl(var(--color-accent));
          background: rgba(215, 80, 40, 0.10);
          border: 2px solid rgba(215, 80, 40, 0.35);
          padding: 4px 8px;
          border-radius: 4px;
          display: inline-flex;
          align-items: center;
          gap: 6px;
          text-transform: uppercase;
          letter-spacing: 0.05em;
          margin-bottom: 8px;
        }

        .pih-title {
          font-size: 20px;
          font-weight: 700;
          color: hsl(var(--text-primary));
        }

        @media (min-width: 768px) {
          .pih-title {
            font-size: 26px;
          }
        }

        .filter-pills-wrap {
          display: flex;
          background: rgba(139, 92, 26, 0.04);
          border: 1px solid var(--border-light);
          padding: 4px;
          border-radius: 8px;
          width: fit-content;
        }

        .filter-pill {
          background: transparent;
          border: none;
          color: hsl(var(--text-secondary));
          font-size: 12px;
          font-weight: 600;
          padding: 6px 12px;
          border-radius: 6px;
          cursor: pointer;
          transition: all 0.2s;
        }

        .filter-pill.pill-active {
          background: hsl(var(--color-accent));
          color: #fff;
          box-shadow: 0 2px 8px rgba(215, 80, 40, 0.2);
        }

        .kpi-panel {
          display: grid;
          grid-template-columns: 1fr;
          gap: 12px;
          margin-bottom: 24px;
        }

        @media (min-width: 480px) {
          .kpi-panel {
            grid-template-columns: 1fr 1fr;
          }
        }

        @media (min-width: 768px) {
          .kpi-panel {
            grid-template-columns: 1fr 1fr 1fr;
            gap: 16px;
          }
        }

        .kpi-card {
          display: flex;
          align-items: center;
          gap: 16px;
          padding: 16px 20px;
        }

        .kpi-card:hover {
          transform: none; /* Keep KPIs static */
          border-color: var(--border-light);
        }

        .warning-kpi {
          border-left: 3px solid hsl(var(--color-warning));
        }

        .danger-kpi {
          border-left: 3px solid hsl(var(--color-critical));
        }

        .kpi-icon-wrap {
          width: 40px;
          height: 40px;
          border-radius: 10px;
          display: flex;
          align-items: center;
          justify-content: center;
        }

        .bg-primary {
          background: rgba(215, 80, 40, 0.08);
          color: hsl(var(--color-accent));
        }

        .bg-warning {
          background: rgba(194, 65, 12, 0.08);
          color: hsl(var(--color-warning));
        }

        .bg-danger {
          background: rgba(220, 38, 38, 0.08);
          color: hsl(var(--color-critical));
        }

        .kpi-text-area {
          display: flex;
          flex-direction: column;
          gap: 4px;
        }

        .kpi-label {
          font-size: 11px;
          font-weight: 700;
          color: hsl(var(--text-secondary));
          text-transform: uppercase;
          letter-spacing: 0.05em;
        }

        .kpi-value {
          font-size: 22px;
          font-weight: 700;
          color: hsl(var(--text-primary));
        }

        .kpi-unit {
          font-size: 12px;
          color: hsl(var(--text-secondary));
          margin-left: 2px;
          font-weight: 600;
        }

        .color-warn {
          color: hsl(var(--color-warning));
        }

        .color-crit {
          color: hsl(var(--color-critical));
        }

        .devices-grid {
          display: grid;
          grid-template-columns: 1fr;
          gap: 16px;
        }

        @media (min-width: 480px) {
          .devices-grid {
            grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
          }
        }

        @media (min-width: 1024px) {
          .devices-grid {
            gap: 20px;
          }
        }

        .loader-container {
          display: flex;
          flex-direction: column;
          align-items: center;
          justify-content: center;
          gap: 16px;
          padding: 80px 20px;
          color: hsl(var(--text-secondary));
        }

        .loader-text {
          font-size: 14px;
        }

        .loader-icon {
          animation: loader-rotate 1.5s infinite linear;
        }

        .error-card {
          border-left: 4px solid hsl(var(--color-critical));
          padding: 24px;
          display: flex;
          gap: 20px;
          background: rgba(220, 38, 38, 0.02);
        }

        .error-details h3 {
          font-size: 18px;
          font-weight: 700;
          color: hsl(var(--text-primary));
          margin-bottom: 8px;
        }

        .error-details p {
          font-size: 14px;
          color: hsl(var(--text-secondary));
          line-height: 1.5;
        }

        .error-suggestions {
          margin-top: 14px;
          display: flex;
          flex-direction: column;
          gap: 6px;
          font-size: 12px;
          color: hsl(var(--text-muted));
        }

        .empty-state-card {
          padding: 60px 20px;
          display: flex;
          flex-direction: column;
          align-items: center;
          justify-content: center;
          gap: 12px;
          color: hsl(var(--text-secondary));
          text-align: center;
        }

        .es-tip {
          font-size: 12px;
          color: hsl(var(--text-muted));
        }
      `}</style>
    </>
  );
}
