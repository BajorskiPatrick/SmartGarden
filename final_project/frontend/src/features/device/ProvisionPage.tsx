import { useState } from 'react';
import { Wifi, Smartphone, Check, ArrowRight, Loader2, Copy, Bluetooth } from 'lucide-react';
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
  const [step, setStep] = useState(1);
  const [macAddress, setMacAddress] = useState('');
  const [wifiSsid, setWifiSsid] = useState('');
  const [wifiPass, setWifiPass] = useState('');
  
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [credentials, setCredentials] = useState<MqttCredentials | null>(null);

  const { token } = useAuth();
  const { provisionDevice, isProvisioning, provisioningError, progress } = useBleProvisioning();

  const handleGenerateCredentials = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);
    setIsLoading(true);

    if (!token) {
        setError("You are not logged in. Please log in first.");
        setIsLoading(false);
        return;
    }

    try {
      const response = await api.post<MqttCredentials>(`/provisioning/device?mac=${macAddress}`);
      setCredentials(response.data);
      setStep(2); // Move to next step (display credentials)
    } catch (err: any) {
      console.error("Provisioning error:", err);
      const status = err.response?.status;
      const msg = err.response?.data?.message || err.message;
      
      if (status === 403 || status === 401) {
          setError(`Session expired or unauthorized (${status}). Please log in again.`);
      } else {
          setError(`Error ${status || 'unknown'}: ${msg}`);
      }
    } finally {
      setIsLoading(false);
    }
  };



  const handleBleProvision = async () => {
      if (!credentials || !wifiSsid) return;
      
      await provisionDevice({
          ssid: wifiSsid,
          pass: wifiPass,
          mqtt_login: credentials.mqtt_login,
          mqtt_pass: credentials.mqtt_password,
          user_id: credentials.user_id,
          broker_url: credentials.broker_url
      });
  };

  return (
    <div className="max-w-4xl mx-auto py-8 px-4">
      <div className="text-center mb-12">
        <h1 className="text-3xl font-bold text-gray-900 dark:text-white mb-4">
          Add New Device
        </h1>
        <p className="text-lg text-gray-600 dark:text-gray-400">
          Two ways to connect: Automated (Bluetooth) or Manual.
        </p>
      </div>

      {step === 1 && (
        <div className="max-w-md mx-auto bg-white dark:bg-gray-800 p-8 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">
           <h2 className="text-xl font-semibold mb-4 text-gray-900 dark:text-white">Step 1: Get Credentials & WiFi</h2>
           
           <form onSubmit={handleGenerateCredentials}>
             <div className="mb-4">
               <label htmlFor="mac" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">Device MAC Address</label>
               <input
                 type="text"
                 id="mac"
                 value={macAddress}
                 onChange={(e) => setMacAddress(e.target.value.toUpperCase())}
                 placeholder="78EE4C..."
                 className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-green-500 dark:bg-gray-700 dark:text-white"
                 required
               />
             </div>
             
             <div className="mb-4">
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">Your WiFi SSID</label>
                <input
                    type="text"
                    value={wifiSsid}
                    onChange={(e) => setWifiSsid(e.target.value)}
                    placeholder="Home_WiFi"
                    className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-green-500 dark:bg-gray-700 dark:text-white"
                />
                <p className="text-xs text-gray-500 mt-1">Required for Bluetooth provisioning</p>
             </div>

             <div className="mb-6">
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">Your WiFi Password</label>
                <input
                    type="password"
                    value={wifiPass}
                    onChange={(e) => setWifiPass(e.target.value)}
                    className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-green-500 dark:bg-gray-700 dark:text-white"
                />
             </div>
             
             {error && <div className="text-red-500 text-sm mb-4">{error}</div>}
             
             <button
               type="submit"
               disabled={isLoading}
               className="w-full bg-green-600 hover:bg-green-700 text-white py-2 rounded-lg font-medium transition-colors flex justify-center items-center"
             >
               {isLoading ? <Loader2 className="w-5 h-5 animate-spin" /> : 'Generate Credentials'}
             </button>
           </form>
        </div>
      )}

      {step === 2 && credentials && (
        <div className="space-y-8">
            
          {/* Bluetooth Section */}
          <div className="bg-blue-50 dark:bg-blue-900/20 p-6 rounded-xl border border-blue-200 dark:border-blue-800 max-w-2xl mx-auto text-center">
             <div className="flex justify-center mb-4">
                <div className="w-16 h-16 bg-blue-100 dark:bg-blue-800 rounded-full flex items-center justify-center text-blue-600 dark:text-blue-300">
                    <Bluetooth className="w-8 h-8" />
                </div>
             </div>
             <h2 className="text-2xl font-bold text-blue-900 dark:text-blue-100 mb-2">Automated Setup</h2>
             <p className="text-blue-700 dark:text-blue-300 mb-6">
                Make sure your device is plugged in (Blue LED blinking).<br/>
                Click below to select your device and automatically configure it.
             </p>
             
             {provisioningError && (
                 <div className="mb-4 text-red-600 dark:text-red-400 bg-red-50 dark:bg-red-900/20 p-3 rounded">
                     {provisioningError}
                 </div>
             )}
             
             {progress && (
                 <div className="mb-4 text-blue-800 dark:text-blue-300 font-medium animate-pulse">
                     {progress}
                 </div>
             )}

             <button 
                onClick={handleBleProvision}
                disabled={isProvisioning || !wifiSsid}
                className="px-8 py-3 bg-blue-600 hover:bg-blue-700 text-white rounded-xl font-bold text-lg shadow-lg hover:shadow-xl transition-all disabled:opacity-50 disabled:cursor-not-allowed"
             >
                {isProvisioning ? <Loader2 className="w-6 h-6 animate-spin" /> : 'Connect & Provision'}
             </button>
             
             {!wifiSsid && <p className="text-xs text-red-500 mt-2">WiFi SSID required (Go back to Step 1)</p>}
          </div>

          <div className="relative flex py-5 items-center">
              <div className="flex-grow border-t border-gray-300 dark:border-gray-700"></div>
              <span className="flex-shrink-0 mx-4 text-gray-400 uppercase text-sm">Or Manual Setup</span>
              <div className="flex-grow border-t border-gray-300 dark:border-gray-700"></div>
          </div>

          {/* Manual Credentials Display */}
          <div className="bg-gray-50 dark:bg-gray-800 p-6 rounded-xl border border-gray-200 dark:border-gray-700 max-w-2xl mx-auto opacity-75 hover:opacity-100 transition-opacity">
            <h2 className="text-lg font-bold text-gray-800 dark:text-gray-300 mb-4 flex items-center gap-2">
              <Check className="w-5 h-5 text-green-500" /> Manual Credentials
            </h2>
            <div className="grid gap-2">
               <div className="bg-white dark:bg-gray-900 p-3 rounded flex justify-between items-center text-sm">
                 <span className="font-mono">{credentials.mqtt_login}</span>
                 <button onClick={() => navigator.clipboard.writeText(credentials.mqtt_login)}><Copy className="w-4 h-4 text-gray-400" /></button>
               </div>
               <div className="bg-white dark:bg-gray-900 p-3 rounded flex justify-between items-center text-sm">
                 <span className="font-mono">{credentials.mqtt_password}</span>
                 <button onClick={() => navigator.clipboard.writeText(credentials.mqtt_password)}><Copy className="w-4 h-4 text-gray-400" /></button>
               </div>
               <div className="bg-white dark:bg-gray-900 p-3 rounded flex justify-between items-center text-sm">
                 <span className="font-mono">{credentials.user_id}</span>
                 <button onClick={() => navigator.clipboard.writeText(credentials.user_id)}><Copy className="w-4 h-4 text-gray-400" /></button>
               </div>
               <div className="bg-white dark:bg-gray-900 p-3 rounded flex justify-between items-center text-sm">
                 <span className="font-mono">{credentials.broker_url}</span>
                 <button onClick={() => navigator.clipboard.writeText(credentials.broker_url)}><Copy className="w-4 h-4 text-gray-400" /></button>
               </div>
            </div>
          </div>
          
          <div className="grid gap-8 md:grid-cols-2 lg:grid-cols-3 max-w-5xl mx-auto">
             {/* Instructions for physical setup */}
             <div className="bg-white dark:bg-gray-800 p-6 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">
               <div className="w-10 h-10 bg-blue-100 dark:bg-blue-900/30 rounded-full flex items-center justify-center mb-3 text-blue-600">
                 <Wifi className="w-5 h-5" />
               </div>
               <h3 className="font-semibold mb-2">1. Connect to Device WiFi</h3>
               <p className="text-sm text-gray-600 dark:text-gray-400">
                 Connect your phone/laptop to the WiFi network <strong>SmartGarden-Prov</strong>.
               </p>
             </div>
             
             <div className="bg-white dark:bg-gray-800 p-6 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">
               <div className="w-10 h-10 bg-purple-100 dark:bg-purple-900/30 rounded-full flex items-center justify-center mb-3 text-purple-600">
                 <Smartphone className="w-5 h-5" />
               </div>
               <h3 className="font-semibold mb-2">2. Open Configuration</h3>
               <p className="text-sm text-gray-600 dark:text-gray-400">
                 Open your browser and navigate to <strong>http://192.168.4.1</strong>.
               </p>
             </div>
             
             <div className="bg-white dark:bg-gray-800 p-6 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">
               <div className="w-10 h-10 bg-green-100 dark:bg-green-900/30 rounded-full flex items-center justify-center mb-3 text-green-600">
                 <Check className="w-5 h-5" />
               </div>
               <h3 className="font-semibold mb-2">3. Enter Credentials</h3>
               <p className="text-sm text-gray-600 dark:text-gray-400">
                 Copy the generated Login, Password, and User ID into the device configuration page.
               </p>
             </div>
          </div>
        </div>
      )}
       
      <div className="mt-8 text-center flex justify-center gap-4">
        {step === 2 && (
            <button 
                onClick={() => setStep(1)}
                className="text-gray-500 hover:text-gray-700 underline text-sm"
            >
                Generate for another device
            </button>
        )}
        <Link to="/" className="text-green-600 hover:text-green-700 font-medium inline-flex items-center gap-2">
          Back to Dashboard <ArrowRight className="w-4 h-4" />
        </Link>
      </div>
    </div>
  );
}
