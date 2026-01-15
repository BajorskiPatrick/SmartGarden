import { useState } from 'react';
import { Link } from 'react-router-dom';
import type { Device } from '../../types';
import { Wifi, WifiOff, Droplets, Thermometer, Sun, Settings, Sprout } from 'lucide-react';
import { cn } from '../../lib/utils';
import { useTelemetryWebSocket } from '../../hooks/useTelemetryWebSocket';
import { DeviceSettingsDialog } from './DeviceSettingsDialog';

interface DeviceCardProps {
  device: Device;
  onWater: (mac: string) => void;
}

export function DeviceCard({ device, onWater }: DeviceCardProps) {
  const [isSettingsOpen, setIsSettingsOpen] = useState(false);

  // Use device.online directly, fallback to false if undefined
  const isOnline = device.online ?? false;

  // Listen for real-time updates
  const liveData = useTelemetryWebSocket(device.macAddress);

  // Merge live data with initial data
  const temperature = liveData?.temperature ?? device.temperature;
  const soilMoisture = liveData?.soilMoisture ?? device.soilMoisture;
  const lightLux = liveData?.lightLux ?? device.lightLux;

  return (
    <>
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 overflow-hidden hover:shadow-md transition-shadow relative">
        <div className="absolute top-4 right-4 flex gap-2">
          <button
            onClick={() => setIsSettingsOpen(true)}
            className="p-1.5 text-gray-500 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors"
            title="Settings"
          >
            <Settings className="w-5 h-5" />
          </button>
        </div>

        <div className="p-6">
          <div className="flex justify-between items-start mb-4 pr-10">
            <div>
              <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-1">
                {device.friendlyName || 'Unnamed Device'}
              </h3>
              <p className="text-sm text-gray-500 font-mono">{device.macAddress}</p>
            </div>
          </div>
          <div className="mb-4">
            <div className={cn(
              "inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-medium",
              isOnline ? "bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400" : "bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400"
            )}>
              {isOnline ? <Wifi className="w-3 h-3" /> : <WifiOff className="w-3 h-3" />}
              {isOnline ? 'Online' : 'Offline'}
            </div>
          </div>

          <div className="grid grid-cols-3 gap-4 mb-6">
            <div className="text-center p-2 bg-blue-50 dark:bg-blue-900/20 rounded-lg">
              <Droplets className="w-5 h-5 mx-auto text-blue-500 mb-1" />
              <span className="text-xs text-gray-500 dark:text-gray-400">Moisture</span>
              <p className="font-semibold text-gray-900 dark:text-white">
                {soilMoisture != null ? `${soilMoisture}%` : '--%'}
              </p>
            </div>
            <div className="text-center p-2 bg-orange-50 dark:bg-orange-900/20 rounded-lg">
              <Thermometer className="w-5 h-5 mx-auto text-orange-500 mb-1" />
              <span className="text-xs text-gray-500 dark:text-gray-400">Temp</span>
              <p className="font-semibold text-gray-900 dark:text-white">
                {temperature != null ? `${temperature.toFixed(1)}°C` : '--°C'}
              </p>
            </div>
            <div className="text-center p-2 bg-yellow-50 dark:bg-yellow-900/20 rounded-lg">
              <Sun className="w-5 h-5 mx-auto text-yellow-500 mb-1" />
              <span className="text-xs text-gray-500 dark:text-gray-400">Light</span>
              <p className="font-semibold text-gray-900 dark:text-white">
                {lightLux != null ? `${lightLux.toFixed(0)} lx` : '-- lx'}
              </p>
            </div>
          </div>

          <div className="flex gap-3">
            <Link
              to={`/devices/${device.macAddress}`}
              className="flex-1 text-center px-4 py-2 border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-200 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors text-sm font-medium"
            >
              Details
            </Link>
            <button
              onClick={() => onWater(device.macAddress)}
              className="flex-1 bg-blue-600 hover:bg-blue-700 text-white px-4 py-2 rounded-lg transition-colors text-sm font-medium flex items-center justify-center gap-2"
            >
              <Droplets className="w-4 h-4" />
              Water
            </button>
          </div>

          <div className="mt-4 pt-3 border-t border-gray-100 dark:border-gray-700 flex items-center justify-center text-xs text-gray-500">
            <Sprout className="w-3 h-3 mr-1 text-green-500" />
            Profile: <span className="font-medium text-gray-700 dark:text-gray-300 ml-1">{device.activeProfileName || 'None'}</span>
          </div>
        </div>
      </div>

      <DeviceSettingsDialog
        macAddress={device.macAddress}
        isOpen={isSettingsOpen}
        onClose={() => setIsSettingsOpen(false)}
        currentDeviceName={device.friendlyName}
      />
    </>
  );
}
