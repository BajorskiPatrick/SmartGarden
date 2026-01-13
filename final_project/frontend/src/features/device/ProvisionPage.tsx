import { useState } from 'react';
import { Wifi, Check, ArrowRight, Loader2, Bluetooth } from 'lucide-react';
import { Link } from 'react-router-dom';
import { api } from '../../lib/axios';
import { useAuth } from '../../context/AuthContext';
import { useBleProvisioning } from '../../hooks/useBleProvisioning';

interface MqttCredentials {
  mqtt_login: string;
  mqtt_password: string;
  user_id: string;
  broker_url: string;
}

export function ProvisionPage() {
  const [wifiSsid, setWifiSsid] = useState('');
  const [wifiPass, setWifiPass] = useState('');
  const [statusMessage, setStatusMessage] = useState('');

  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [connectedMac, setConnectedMac] = useState<string | null>(null);

  const { token } = useAuth();
  const { connectAndGetMac, waitForAuth, provisionDevice, isProvisioning, progress } = useBleProvisioning();

  return (
    <div className="max-w-4xl mx-auto py-8 px-4">
      <div className="text-center mb-12">
        <h1 className="text-3xl font-bold text-gray-900 dark:text-white mb-4">
          Add New Device
        </h1>
        <p className="text-lg text-gray-600 dark:text-gray-400">
          Enter your WiFi details and connect to your Smart Garden device.
        </p>
      </div>

      <div className="max-w-md mx-auto bg-white dark:bg-gray-800 p-8 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">

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

        {/* Step 2: WiFi inputs & Provision (Visible only after connection) */}
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

            {/* WiFi Inputs */}
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">WiFi SSID</label>
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
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">WiFi Password</label>
              <input
                type="password"
                value={wifiPass}
                onChange={(e) => setWifiPass(e.target.value)}
                disabled={isLoading || isProvisioning}
                className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-green-500 dark:bg-gray-700 dark:text-white disabled:opacity-50"
              />
            </div>

            {/* Provision Action */}
            <button
              onClick={async () => {
                setError(null);
                setIsLoading(true);
                setStatusMessage('Generating credentials...');

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

                try {
                  // Fetch Credentials from API
                  const response = await api.post<MqttCredentials>(`/provisioning/device?mac=${connectedMac}`);
                  const creds = response.data;
                  // setCredentials(creds); // State removed as unused

                  setStatusMessage('Credentials generated.');

                  // NEW: Wait for Physical Auth
                  setStatusMessage('Please press the BOOT button on the device to confirm setup...');
                  await waitForAuth();

                  setStatusMessage('Device Authorized! Configuring...');

                  // Write Configuration
                  await provisionDevice({
                    ssid: wifiSsid,
                    pass: wifiPass,
                    mqtt_login: creds.mqtt_login,
                    mqtt_pass: creds.mqtt_password,
                    user_id: creds.user_id,
                    broker_url: creds.broker_url
                  });

                  setStatusMessage('Setup Complete!');
                  setConnectedMac(null); // Reset for next or done
                  setWifiSsid('');
                  setWifiPass('');

                } catch (err: any) {
                  console.error("Provisioning sequence error:", err);
                  let msg = err.message || 'Unknown error';
                  if (err.response) {
                    msg = err.response.data?.message || `Server error ${err.response.status}`;
                  }
                  setError(msg);
                } finally {
                  setIsLoading(false);
                }
              }}
              disabled={!wifiSsid || isLoading || isProvisioning}
              className="w-full bg-green-600 hover:bg-green-700 disabled:bg-gray-400 text-white py-4 rounded-xl font-bold text-lg shadow-lg hover:shadow-xl transition-all flex justify-center items-center gap-2"
            >
              {isLoading || isProvisioning ? <Loader2 className="w-6 h-6 animate-spin" /> : <Check className="w-6 h-6" />}
              {isLoading || isProvisioning ? 'Configuring...' : 'Step 2: Configure Device'}
            </button>
          </div>
        )}
      </div>

      <div className="mt-8 text-center">
        <Link to="/" className="text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-300 font-medium inline-flex items-center gap-2">
          <ArrowRight className="w-4 h-4 rotate-180" /> Back to Dashboard
        </Link>
      </div>

      <div className="mt-12 grid gap-6 md:grid-cols-3 text-sm text-gray-500 dark:text-gray-400 text-center max-w-3xl mx-auto opacity-75">
        <div>
          <Wifi className="w-6 h-6 mx-auto mb-2" />
          <p>1. Enter WiFi Details</p>
        </div>
        <div>
          <Bluetooth className="w-6 h-6 mx-auto mb-2" />
          <p>2. Connect via Bluetooth</p>
        </div>
        <div>
          <Check className="w-6 h-6 mx-auto mb-2" />
          <p>3. Automatic Setup</p>
        </div>
      </div>
    </div>
  );
}
