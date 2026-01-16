export interface Device {
    macAddress: string;
    friendlyName: string;
    online: boolean;
    lastSeen?: string;
    temperature?: number;
    humidity?: number;
    soilMoisture?: number;
    lightLux?: number;
    waterTankOk?: boolean;
    activeProfileName?: string | null;
    userId?: string;
    // Merged telemetry fields
    lastMeasurementTime?: string;
}

export interface PlantProfile {
    id?: string;
    userId?: string;
    name: string;
    description: string;
    soil_min: number;
    soil_max: number;
    temp_min: number;
    temp_max: number;
    hum_min: number;
    hum_max: number;
    light_min: number;
    light_max: number;
    watering_duration_sec: number;
    measurement_interval_sec: number;
}
