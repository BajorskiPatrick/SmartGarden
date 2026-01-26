
import { useParams, useNavigate } from 'react-router-dom';
import { useQuery } from '@tanstack/react-query';
import { api } from '../../lib/axios';
import type { Device, Measurement } from '../../types';
import { TelemetryChart } from './TelemetryChart';
import { ControlPanel } from './ControlPanel';
import { ArrowLeft, Loader2, Wifi, WifiOff } from 'lucide-react';
import { cn } from '../../lib/utils';
import { useTelemetryWebSocket } from '../../hooks/useTelemetryWebSocket';
import { useEffect, useState } from 'react';

export function DeviceDetailsPage() {
  const { mac } = useParams<{ mac: string }>();
  const navigate = useNavigate();

  const { data: device, isLoading: isLoadingDevice } = useQuery<Device>({
    queryKey: ['device', mac],
    queryFn: async () => {
      const response = await api.get(`/devices/${mac}`);
      return response.data;
    },
    enabled: !!mac,
  });

  const { data: telemetry } = useQuery<Measurement[]>({
    queryKey: ['telemetry', mac],
    queryFn: async () => {
      const response = await api.get(`/devices/${mac}/telemetry?size=50`);
      return response.data.content;
    },
    enabled: !!mac,
    refetchInterval: false,
  });

  const liveMeasurement = useTelemetryWebSocket(mac || '');
  const [displayData, setDisplayData] = useState<Measurement[]>([]);

  useEffect(() => {
    if (telemetry) {
      setDisplayData(telemetry);
    }
  }, [telemetry]);

  useEffect(() => {
    if (liveMeasurement) {
      setDisplayData(prev => {
        const newMeasurement = {
          ...liveMeasurement,
          device: { macAddress: mac },
        } as unknown as Measurement;

        return [newMeasurement, ...prev];
      });
    }
  }, [liveMeasurement, mac]);

  if (isLoadingDevice || !device) {
    return (
      <div className="flex justify-center items-center h-64">
        <Loader2 className="w-8 h-8 animate-spin text-green-600" />
      </div>
    );
  }

  const isOnline = device.online ?? false;

  return (
    <div className="space-y-6">
      <button
        onClick={() => navigate('/')}
        className="flex items-center text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 transition-colors"
      >
        <ArrowLeft className="w-4 h-4 mr-1" />
        Back to Dashboard
      </button>

      <div className="flex justify-between items-start">
        <div>
          <h1 className="text-3xl font-bold text-gray-900 dark:text-white mb-2">
            {device.friendlyName || 'Unnamed Device'}
          </h1>
          <p className="text-gray-500 font-mono">{device.macAddress}</p>
        </div>
        <div className={cn(
          "flex items-center gap-2 px-3 py-1 rounded-full text-sm font-medium",
          isOnline ? "bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400" : "bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400"
        )}>
          {isOnline ? <Wifi className="w-4 h-4" /> : <WifiOff className="w-4 h-4" />}
          {isOnline ? 'Online' : 'Offline'}
        </div>
      </div>

      <div className="mb-6">
        <ControlPanel macAddress={device.macAddress} />
      </div>

      {/* Telemetry Charts - Full Width */}
      <TelemetryChart data={displayData} />
    </div>
  );
}
