import { useQuery, useMutation } from '@tanstack/react-query';
import { api } from '../../lib/axios';
import type { Device } from '../../types';
import { DeviceCard } from './DeviceCard';
import { Plus, Loader2 } from 'lucide-react';
import { Link } from 'react-router-dom';
import { useDashboardWebSocket } from '../../hooks/useDashboardWebSocket';

export function DashboardPage() {
  useDashboardWebSocket(); // Start listening for updates

  const { data: devices, isLoading, error } = useQuery<Device[]>({
    queryKey: ['devices'],
    queryFn: async () => {
      const response = await api.get('/devices');
      return response.data;
    },
  });

  const waterMutation = useMutation({
    mutationFn: async (mac: string) => {
      await api.post(`/devices/${mac}/water?duration=5`);
    },
    onSuccess: () => {
      alert('Watering command sent!');
    },
  });

  return (
    <div>
      <div className="flex justify-between items-center mb-8">
        <div>
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white">My Garden</h1>
          <p className="text-gray-500 dark:text-gray-400">Manage your smart plants</p>
        </div>
        <Link
          to="/provision"
          className="bg-green-600 hover:bg-green-700 text-white px-4 py-2 rounded-lg flex items-center gap-2 transition-colors"
        >
          <Plus className="w-5 h-5" />
          Add Device
        </Link>
      </div>

      {isLoading ? (
        <div className="flex justify-center items-center h-64">
          <Loader2 className="w-8 h-8 animate-spin text-green-600" />
        </div>
      ) : error ? (
        <div className="text-center text-red-600 p-4 bg-red-50 dark:bg-red-900/20 rounded-lg">
          <p>Failed to load devices.</p>
          <p className="text-sm mt-2 opacity-80">{(error as any).response?.status === 403 ? "Please log in again." : (error as any).message}</p>
        </div>
      ) : devices?.length === 0 ? (
        <div className="text-center py-12 bg-white dark:bg-gray-800 rounded-xl border border-dashed border-gray-300 dark:border-gray-700">
          <p className="text-gray-500 dark:text-gray-400 mb-4">No devices found</p>
          <Link
            to="/provision"
            className="text-green-600 hover:text-green-500 font-medium"
          >
            Add your first device
          </Link>
        </div>
      ) : (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          {devices?.map((device) => (
            <DeviceCard
              key={device.macAddress}
              device={device}
              onWater={(mac) => waterMutation.mutate(mac)}
            />
          ))}
        </div>
      )}
    </div>
  );
}
