import { useState } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import { Wifi, Check, ArrowRight, Loader2, Bluetooth, Settings, Sprout } from 'lucide-react';
import { Link, useNavigate } from 'react-router-dom';
import { api } from '../../lib/axios';
import { useAuth } from '../../context/AuthContext';
import { useBleProvisioning } from '../../hooks/useBleProvisioning';
import { PLANT_PROFILES } from '../../lib/plantProfiles';
import { ProfileManager } from '../profiles/ProfileManager';
import { SensorSettingsForm } from './SensorSettingsForm';
import type { DeviceSettings, PlantProfile } from '../../types';

interface MqttCredentials {
  mqtt_login: string;
  mqtt_password: string;
  user_id: string;
  broker_url: string;
}

const DEFAULT_SETTINGS: DeviceSettings = {
  temp_min: 0,
  temp_max: 50,
  hum_min: 0,
  hum_max: 100,
  soil_min: 0,
  soil_max: 100,
  light_min: 0,
  light_max: 100000,
  watering_duration_sec: 5,
  measurement_interval_sec: 60,
  active_profile_name: undefined
};

export function ProvisionPage() {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const [wifiSsid, setWifiSsid] = useState('');
  const [wifiPass, setWifiPass] = useState('');
  const [stationName, setStationName] = useState('');

  // Settings State
  const [settings, setSettings] = useState<DeviceSettings>(DEFAULT_SETTINGS);
  const [showSettings, setShowSettings] = useState(false);
  const [activeProfileTab, setActiveProfileTab] = useState<'presets' | 'custom'>('presets');

  const [statusMessage, setStatusMessage] = useState('');

  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [connectedMac, setConnectedMac] = useState<string | null>(null);

  const { token } = useAuth();
  const { connectAndGetMac, waitForAuth, provisionDevice, isProvisioning, progress } = useBleProvisioning();

  const updateSetting = (key: keyof DeviceSettings, value: number) => {
    setSettings(prev => ({ ...prev, [key]: value, active_profile_name: undefined })); // Clear profile name on manual edit
  };

  const applyProfile = (profile: PlantProfile) => {
    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    const { id, userId, name, description, ...profileSettings } = profile;
    setSettings(prev => ({
      ...prev,
      ...profileSettings,
      active_profile_name: profile.name
    }));
    setShowSettings(true); // Show settings so user can verify
  };

  return (
    <div className="max-w-4xl mx-auto py-8 px-4">
      <div className="text-center mb-12">
        <h1 className="text-3xl font-bold text-gray-900 dark:text-white mb-4">
          Add New Device
        </h1>
        <p className="text-lg text-gray-600 dark:text-gray-400">
          Enter your WiFi details, configure your station, and connect.
        </p>
      </div>

      <div className="max-w-2xl mx-auto bg-white dark:bg-gray-800 p-8 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">

        {/* Status & Errors */}
        {error && (
          <div className="mb-6 p-4 bg-red-50 dark:bg-red-900/20 text-red-600 dark:text-red-400 rounded-lg text-sm border border-red-200 dark:border-red-800 flex items-start gap-2">
            <span>⚠️</span>
            <span>{error}</span>
          </div>
        )}

        {(isProvisioning || isLoading) && (
          <div className="mb-6 space-y-2">
            <div className="flex items-center justify-center text-blue-600 dark:text-blue-400">
              <Loader2 className="w-8 h-8 animate-spin" />
            </div>
            <p className="text-center text-sm font-medium text-gray-600 dark:text-gray-300">
              {progress || statusMessage}
            </p>
          </div>
        )}

        {/* Step 1: Connect Button */}
        {!connectedMac && (
          <div className="mb-8">
            <button
              onClick={async () => {
                setError(null);
                try {
                  const mac = await connectAndGetMac();
                  setConnectedMac(mac);
                } catch (err: any) {
                  setError(err.message || 'Connection failed');
                }
              }}
              disabled={isProvisioning || isLoading}
              className="w-full bg-blue-600 hover:bg-blue-700 disabled:bg-gray-400 text-white py-4 rounded-xl font-bold text-lg shadow-lg hover:shadow-xl transition-all flex justify-center items-center gap-2"
            >
              <Bluetooth className="w-6 h-6" />
              {isProvisioning ? 'Connecting...' : 'Step 1: Connect Device'}
            </button>
            <p className="mt-2 text-sm text-gray-500 text-center">
              Make sure your device is powered on and in range.
            </p>
          </div>
        )}

        {/* Step 2: Configuration (Visible only after connection) */}
        {connectedMac && (
          <div className="space-y-6 animate-in fade-in slide-in-from-bottom-4 duration-500">
            <div className="p-4 bg-blue-50 dark:bg-blue-900/20 text-blue-700 dark:text-blue-300 rounded-lg flex items-center gap-3">
              <Bluetooth className="w-5 h-5" />
              <div className="flex-1">
                <div className="font-semibold">Connected to Device</div>
                <div className="text-sm opacity-75">MAC: {connectedMac}</div>
              </div>
              <button
                onClick={() => setConnectedMac(null)}
                className="text-xs hover:underline opacity-75 hover:opacity-100"
              >
                Disconnect
              </button>
            </div>

            {/* General Info */}
            <div className="grid gap-4 md:grid-cols-2">
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">Station Name</label>
                <input
                  type="text"
                  value={stationName}
                  onChange={(e) => setStationName(e.target.value)}
                  placeholder="e.g. Living Room"
                  disabled={isLoading || isProvisioning}
                  className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-green-500 dark:bg-gray-700 dark:text-white disabled:opacity-50"
                />
              </div>
            </div>

            {/* WiFi Inputs */}
            <h3 className="text-md font-semibold text-gray-900 dark:text-white flex items-center gap-2 mt-4">
              <Wifi className="w-4 h-4" /> WiFi Connection
            </h3>
            <div className="grid gap-4 md:grid-cols-2">
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">SSID</label>
                <input
                  type="text"
                  value={wifiSsid}
                  onChange={(e) => setWifiSsid(e.target.value)}
                  placeholder="Home_WiFi"
                  disabled={isLoading || isProvisioning}
                  className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-green-500 dark:bg-gray-700 dark:text-white disabled:opacity-50"
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">Password</label>
                <input
                  type="password"
                  value={wifiPass}
                  onChange={(e) => setWifiPass(e.target.value)}
                  disabled={isLoading || isProvisioning}
                  className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-green-500 dark:bg-gray-700 dark:text-white disabled:opacity-50"
                />
              </div>
            </div>

            {/* Profile Selection */}
            <div className="bg-gray-50 dark:bg-gray-900/50 p-4 rounded-xl border border-gray-100 dark:border-gray-800">
              <div className="flex items-center justify-between mb-4">
                <h3 className="text-sm font-bold text-gray-500 uppercase tracking-wider flex items-center gap-2">
                  <Sprout className="w-4 h-4" /> Load Profile
                </h3>
                <div className="flex bg-gray-200 dark:bg-gray-700 rounded-lg p-1">
                  <button
                    onClick={() => setActiveProfileTab('presets')}
                    className={`px-3 py-1 text-xs font-medium rounded-md transition-all ${activeProfileTab === 'presets' ? 'bg-white dark:bg-gray-600 shadow text-gray-900 dark:text-white' : 'text-gray-500 hover:text-gray-900 dark:text-gray-400'}`}
                  >
                    Presets
                  </button>
                  <button
                    onClick={() => setActiveProfileTab('custom')}
                    className={`px-3 py-1 text-xs font-medium rounded-md transition-all ${activeProfileTab === 'custom' ? 'bg-white dark:bg-gray-600 shadow text-gray-900 dark:text-white' : 'text-gray-500 hover:text-gray-900 dark:text-gray-400'}`}
                  >
                    My Profiles
                  </button>
                </div>
              </div>

              {activeProfileTab === 'presets' ? (
                <div className="grid grid-cols-2 sm:grid-cols-4 gap-2">
                  <button
                    onClick={() => {
                      setSettings(DEFAULT_SETTINGS);
                    }}
                    className={`text-left px-3 py-2 rounded-lg text-sm border transition-all ${!settings.active_profile_name ? 'bg-gray-100 border-gray-500 text-gray-800 dark:bg-gray-700 dark:text-gray-300' : 'bg-white dark:bg-gray-800 border-gray-200 dark:border-gray-700 hover:border-gray-400'}`}
                  >
                    <div className="font-medium truncate">No Profile</div>
                    <div className="text-[10px] text-gray-500">Default (Soil &gt; 0%)</div>
                  </button>
                  {PLANT_PROFILES.map(profile => (
                    <button
                      key={profile.id}
                      onClick={() => applyProfile(profile)}
                      className={`text-left px-3 py-2 rounded-lg text-sm border transition-all ${settings.active_profile_name === profile.name ? 'bg-green-100 border-green-500 text-green-800 dark:bg-green-900/40 dark:text-green-300' : 'bg-white dark:bg-gray-800 border-gray-200 dark:border-gray-700 hover:border-green-300'}`}
                    >
                      <div className="font-medium truncate">{profile.name}</div>
                    </button>
                  ))}
                </div>
              ) : (
                <ProfileManager onSelect={applyProfile} />
              )}
              {settings.active_profile_name && (
                <div className="mt-2 text-xs text-green-600 dark:text-green-400 font-medium">
                  Active Profile: {settings.active_profile_name}
                </div>
              )}
            </div>

            {/* Advanced Settings Checkbox */}
            <div>
              <button
                type="button"
                onClick={() => setShowSettings(!showSettings)}
                className="flex items-center gap-2 text-sm font-medium text-blue-600 dark:text-blue-400 hover:text-blue-700"
              >
                <Settings className="w-4 h-4" />
                {showSettings ? 'Hide Sensor Settings' : 'Show Sensor Settings & Verify'}
              </button>
            </div>

            {/* Sensor Settings Form */}
            {showSettings && (
              <SensorSettingsForm
                values={settings}
                onChange={updateSetting}
              />
            )}

            {/* Provision Action */}
            <button
              onClick={async () => {
                setError(null);
                setIsLoading(true);
                setStatusMessage('Generating credentials...');

                // Capture start time to ensure we only accept NEW connections
                // We use server timestamp (lastSeen) delta instead of client clock to avoid skew
                let initialLastSeen: number | null = null;
                try {
                  const preCheck = await api.get(`/devices/${connectedMac}`);
                  if (preCheck.data?.lastSeen) {
                    initialLastSeen = new Date(preCheck.data.lastSeen).getTime();
                  }
                } catch (e) { /* ignore 404 */ }

                if (!wifiSsid) {
                  setError("WiFi SSID is required.");
                  setIsLoading(false);
                  return;
                }

                if (!token) {
                  setError("You are not logged in. Please log in first.");
                  setIsLoading(false);
                  return;
                }

                if (!stationName) {
                  setError("Station Name is required.");
                  setIsLoading(false);
                  return;
                }

                try {
                  // 1. Fetch Credentials from API
                  const response = await api.post<MqttCredentials>(`/provisioning/device?mac=${connectedMac}`);
                  const creds = response.data;

                  setStatusMessage('Credentials generated.');

                  // 2. Wait for Physical Auth
                  setStatusMessage('Please press the BOOT button on the device to confirm setup...');
                  await waitForAuth();

                  setStatusMessage('Device Authorized! Configuring WiFi...');

                  // 3. Write Configuration (WiFi Only)
                  await provisionDevice({
                    ssid: wifiSsid,
                    pass: wifiPass,
                    mqtt_login: creds.mqtt_login,
                    mqtt_pass: creds.mqtt_password,
                    user_id: creds.user_id,
                    broker_url: creds.broker_url
                  });

                  setStatusMessage('WiFi Configured. Waiting for device to come online...');

                  // 4. Poll for Online Status (Max 30s)
                  let online = false;
                  for (let i = 0; i < 15; i++) { // 15 * 2s = 30s
                    await new Promise(r => setTimeout(r, 2000));
                    try {
                      // Force fresh fetch? No invalidation needed for direct GET usually
                      const devRes = await api.get(`/devices/${connectedMac}`);
                      const devData = devRes.data;

                      if (devData && devData.online) {
                        // Check if this is a fresh connection
                        // Check if this is a fresh connection (timestamp update)
                        if (devData.lastSeen) {
                          const currentLastSeen = new Date(devData.lastSeen).getTime();

                          // If we had a previous record, ensure it updated
                          if (initialLastSeen !== null) {
                            if (currentLastSeen > initialLastSeen) {
                              online = true;
                              break;
                            }
                          } else {
                            // No previous record, and now we have one -> Online
                            online = true;
                            break;
                          }
                        } else {
                          // Fallback if backend doesn't provide lastSeen yet (shouldn't happen)
                          // Consider online if no lastSeen? Or assume stale? 
                          // Safer to assume online if backend says so but lacks timestamp (legacy compat)
                          online = true;
                          break;
                        }
                      }
                    } catch (e) {
                      // Ignore 404/error while booting
                    }
                  }

                  if (!online) {
                    // Specific error handling for timeout
                    const timeoutError = new Error("Connection Timeout");
                    (timeoutError as any).isTimeout = true;
                    throw timeoutError;
                  }

                  setStatusMessage('Device Online. Applying Settings...');

                  // 5. Update Station Name (Backend)
                  await api.patch(`/devices/${connectedMac}`, { friendlyName: stationName });

                  // 6. Update Sensor Settings (MQTT via Backend)
                  await api.post(`/devices/${connectedMac}/settings`, settings);

                  setStatusMessage('Setup Complete! Redirecting...');
                  setConnectedMac(null);
                  setWifiSsid('');
                  setWifiPass('');

                  // Invalidate dashboard query to show new device
                  await queryClient.invalidateQueries({ queryKey: ['devices'] });

                  setTimeout(() => {
                    navigate('/');
                  }, 1500);

                } catch (err: any) {
                  console.error("Provisioning sequence error:", err);
                  setIsLoading(false);

                  if (err.isTimeout || err.message === "Connection Timeout") {
                    // CRITICAL: Reset connection state. Device rebooted.
                    setConnectedMac(null);
                    setError("Device rebooted but failed to connect to WiFi. Please connect to the device again and check your password.");
                  } else {
                    let msg = err.message || 'Unknown error';
                    if (err.response) {
                      msg = err.response.data?.message || `Server error ${err.response.status}`;
                    }
                    setError(msg);
                  }
                }
              }}
              disabled={!wifiSsid || !stationName || isLoading || isProvisioning}
              className="w-full bg-green-600 hover:bg-green-700 disabled:bg-gray-400 text-white py-4 rounded-xl font-bold text-lg shadow-lg hover:shadow-xl transition-all flex justify-center items-center gap-2"
            >
              {isLoading || isProvisioning ? <Loader2 className="w-6 h-6 animate-spin" /> : <Check className="w-6 h-6" />}
              {isLoading || isProvisioning ? 'Configuring...' : 'Step 2: Configure & Save'}
            </button>

            {/* Retry Options on Error */}
            {error && (
              <div className="flex gap-4 mt-4 animate-in fade-in slide-in-from-top-2">
                <button
                  onClick={() => {
                    setError(null);
                    setStatusMessage('');
                    // Optional: Reset WiFi inputs if we suspect they are wrong?
                    // For now, keep them so user can edit.
                  }}
                  className="flex-1 px-4 py-3 bg-blue-100 text-blue-700 font-semibold rounded-lg hover:bg-blue-200 transition-colors"
                >
                  Try Provisioning Again
                </button>
                <button
                  onClick={() => navigate('/')}
                  className="flex-1 px-4 py-3 bg-gray-100 text-gray-700 font-semibold rounded-lg hover:bg-gray-200 transition-colors"
                >
                  Go to Dashboard
                </button>
              </div>
            )
            }
          </div>
        )}
      </div>

      <div className="mt-8 text-center">
        <Link to="/" className="text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-300 font-medium inline-flex items-center gap-2">
          <ArrowRight className="w-4 h-4 rotate-180" /> Back to Dashboard
        </Link>
      </div>
    </div>
  );
}
