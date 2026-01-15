import { useQuery } from '@tanstack/react-query';
import { api } from '../lib/axios';

export interface Device {
    id: string; // or number, aligning with backend
    name: string;
    type: 'ESP32' | 'OTHER';
    status: 'ONLINE' | 'OFFLINE';
    lastSeen?: string;
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
