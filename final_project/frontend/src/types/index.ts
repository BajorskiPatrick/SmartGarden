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
    status: 'ONLINE' | 'OFFLINE'; // Keep for back-compat if needed, but prefer online
    online: boolean; // Add this
    lastSeen: string;
    // Optional latest measurement fields from DeviceSummaryDto
    temperature?: number;
    humidity?: number;
    soilMoisture?: number;
    lightLux?: number;
    waterTankOk?: boolean;
    lastMeasurementTime?: string;
    activeProfileName?: string;
}

export interface Measurement {
    id: number;
    timestamp: string;
    temperature: number | null;
    humidity: number | null;
    soilMoisture: number | null;
    pressure: number | null;
    lightLux: number | null;
    waterTankOk: boolean | null;
}

export interface DeviceSettings {
    temp_min: number;
    temp_max: number;
    hum_min: number;
    hum_max: number;
    soil_min: number;
    soil_max: number;
    light_min: number;
    light_max: number;
    watering_duration_sec: number;
    measurement_interval_sec: number;
    active_profile_name?: string;
}

export interface Alert {
    id?: string;
    message: string;
    severity: string; // 'INFO', 'WARNING', 'CRITICAL'
    timestamp: string;
    deviceMac: string;
}

export interface PlantProfile {
    id?: string; // Optional for new creations
    userId?: string;
    name: string;
    description?: string;
    settings: Partial<DeviceSettings>;
}
