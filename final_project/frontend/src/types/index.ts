export interface User {
    username: string;
}

export interface AuthResponse {
    token: string;
}

export interface Device {
    id: number;
    name: string;
    macAddress: string;
    status: 'ONLINE' | 'OFFLINE'; // inferred
    lastSeen: string;
}

export interface Measurement {
    id: number;
    timestamp: string;
    temperature: number;
    humidity: number;
    lightIntensity: number;
    soilMoisture: number;
}
