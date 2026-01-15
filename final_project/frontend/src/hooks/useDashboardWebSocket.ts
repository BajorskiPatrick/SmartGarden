import { useEffect, useRef } from 'react';
import { Client } from '@stomp/stompjs';
import { useAuth } from '../context/AuthContext';
import { useQueryClient } from '@tanstack/react-query';
import type { Device } from '../types';

interface TelemetryUpdate {
    macAddress: string;
    timestamp: string;
    soilMoisture?: number;
    temperature?: number;
    humidity?: number;
    pressure?: number;
    lightLux?: number;
    waterTankOk?: boolean;
}

export function useDashboardWebSocket() {
    const { token, username } = useAuth();
    const queryClient = useQueryClient();
    const clientRef = useRef<Client | null>(null);

    useEffect(() => {
        if (!token || !username) return;

        // Use env var if available, otherwise derive dynamically from window location (for production/LAN)
        const wsUrl = import.meta.env.VITE_WS_URL ||
            `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.hostname}:8080/ws`;

        const client = new Client({
            brokerURL: wsUrl,
            connectHeaders: {
                Authorization: `Bearer ${token}`,
            },
            reconnectDelay: 5000, // Auto reconnect
            onConnect: () => {
                console.log('Dashboard WS Connected');

                // Subscribe to User Telemetry Stream
                client.subscribe(`/topic/user/${username}/telemetry`, (message) => {
                    try {
                        const data = JSON.parse(message.body) as TelemetryUpdate;

                        // Update React Query Cache for 'devices' list
                        queryClient.setQueryData<Device[]>(['devices'], (oldDevices) => {
                            if (!oldDevices) return oldDevices;

                            return oldDevices.map(device => {
                                if (device.macAddress === data.macAddress) {
                                    // Update device with new values
                                    return {
                                        ...device,
                                        lastMeasurementTime: data.timestamp,
                                        // Update sensor values if present in payload
                                        ...(data.soilMoisture !== undefined && { soilMoisture: data.soilMoisture }),
                                        ...(data.temperature !== undefined && { temperature: data.temperature }),
                                        ...(data.humidity !== undefined && { humidity: data.humidity }),
                                        ...(data.lightLux !== undefined && { lightLux: data.lightLux }),
                                        ...(data.waterTankOk !== undefined && { waterTankOk: data.waterTankOk }),
                                    };
                                }
                                return device;
                            });
                        });

                    } catch (e) {
                        console.error('Failed to parse Dashboard WS message', e);
                    }
                });
            },
            onStompError: (frame) => {
                console.error('Broker reported error: ' + frame.headers['message']);
            },
        });

        client.activate();
        clientRef.current = client;

        return () => {
            client.deactivate();
        };
    }, [token, username, queryClient]);
}
