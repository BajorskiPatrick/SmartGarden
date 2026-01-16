import { useQuery } from '@tanstack/react-query';
import { api } from '../lib/axios';
import { Device } from '../types';

export function useDevices() {
    return useQuery({
        queryKey: ['devices'],
        queryFn: async () => {
            const { data } = await api.get<Device[]>('/devices');
            return data;
        },
    });
}
