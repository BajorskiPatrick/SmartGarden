import { useQuery } from '@tanstack/react-query';
import { api } from '../lib/axios';

export interface Device {
    macAddress: string;
    friendlyName: string;
    // type: 'ESP32' | 'OTHER'; // Not in current response, keeping generic if needed or removing
    // status: 'ONLINE' | 'OFFLINE'; // Removed, using online boolean
    online: boolean;
    lastSeen?: string;
    temperature?: number;
    humidity?: number;
    soilMoisture?: number;
    lightLux?: number;
    waterTankOk?: boolean;
    activeProfileName?: string | null;
    userId?: string;
}

export function useDevices() {
    return useQuery({
        queryKey: ['devices'],
        queryFn: async () => {
            const { data } = await api.get<Device[]>('/devices');
            return data;
        },
    });
}
