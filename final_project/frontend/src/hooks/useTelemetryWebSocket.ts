import { useEffect, useState, useRef } from 'react';
import { Client } from '@stomp/stompjs';
import { useAuth } from '../context/AuthContext';

interface TelemetryUpdate {
    timestamp: string;
    soilMoisture?: number;
    temperature?: number;
    humidity?: number;
    pressure?: number;
    lightLux?: number;
    waterTankOk?: boolean;
}

export function useTelemetryWebSocket(macAddress: string) {
    const { token } = useAuth();
    const [lastMeasurement, setLastMeasurement] = useState<TelemetryUpdate | null>(null);
    const clientRef = useRef<Client | null>(null);

    useEffect(() => {
        if (!macAddress || !token) return;

        const client = new Client({
            brokerURL: 'ws://localhost:8080/ws', // Need to handle production URL
            connectHeaders: {
                Authorization: `Bearer ${token}`, // Pass JWT if backend supports it in Upgrade header or CONNECT frame
            },
            debug: function (str) {
                console.log('WS Debug:', str);
            },
            onConnect: () => {
                console.log('Connected to WebSocket');
                client.subscribe(`/topic/device/${macAddress}/telemetry`, (message) => {
                    try {
                        const data = JSON.parse(message.body);
                        setLastMeasurement(data);
                    } catch (e) {
                        console.error('Failed to parse WS message', e);
                    }
                });
            },
            onStompError: (frame) => {
                console.error('Broker reported error: ' + frame.headers['message']);
                console.error('Additional details: ' + frame.body);
            },
        });

        client.activate();
        clientRef.current = client;

        return () => {
            client.deactivate();
        };
    }, [macAddress, token]);

    return lastMeasurement;
}
