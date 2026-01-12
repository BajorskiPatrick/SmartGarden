import { useState } from 'react';
import { Droplets, Settings, RotateCcw } from 'lucide-react';
import { api } from '../../lib/axios';
import { useMutation } from '@tanstack/react-query';

interface ControlPanelProps {
  macAddress: string;
}

export function ControlPanel({ macAddress }: ControlPanelProps) {
  const [duration, setDuration] = useState(5);

  const waterMutation = useMutation({
    mutationFn: async () => {
      await api.post(`/devices/${macAddress}/water?duration=${duration}`);
    },
    onSuccess: () => {
      alert('Watering started!');
    },
  });

  return (
    <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-6">
      <h3 className="text-lg font-semibold mb-6 text-gray-900 dark:text-white flex items-center gap-2">
        <Settings className="w-5 h-5" />
        Controls
      </h3>

      <div className="space-y-6">
        <div>
          <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Manual Watering (seconds)
          </label>
          <div className="flex gap-4">
            <input
              type="range"
              min="1"
              max="30"
              value={duration}
              onChange={(e) => setDuration(Number(e.target.value))}
              className="flex-1 h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700 accent-green-600"
            />
            <span className="w-12 text-center font-mono text-gray-900 dark:text-white">
              {duration}s
            </span>
          </div>
          <button
            onClick={() => waterMutation.mutate()}
            disabled={waterMutation.isPending}
            className="mt-4 w-full bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white px-4 py-2 rounded-lg transition-colors flex items-center justify-center gap-2"
          >
            {waterMutation.isPending ? (
              'Sending...'
            ) : (
              <>
                <Droplets className="w-4 h-4" />
                Water Now
              </>
            )}
          </button>
        </div>

        <div className="border-t border-gray-200 dark:border-gray-700 pt-6">
          <h4 className="text-sm font-medium text-gray-900 dark:text-white mb-4">Danger Zone</h4>
          <button
            className="w-full border border-red-300 text-red-600 hover:bg-red-50 dark:border-red-800 dark:text-red-400 dark:hover:bg-red-900/20 px-4 py-2 rounded-lg transition-colors flex items-center justify-center gap-2 text-sm"
            onClick={() => {
                if(confirm('Are you sure you want to reset settings?')) {
                    api.post(`/devices/${macAddress}/settings/reset`);
                }
            }}
          >
            <RotateCcw className="w-4 h-4" />
            Reset to Factory Settings
          </button>
        </div>
      </div>
    </div>
  );
}
