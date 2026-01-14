import { Thermometer, Droplets, Activity, Sun } from 'lucide-react';
import type { DeviceSettings } from '../../types';

interface SensorSettingsFormProps {
    values: Partial<DeviceSettings>;
    onChange: (key: keyof DeviceSettings, value: number) => void;
    readOnly?: boolean;
}

export function SensorSettingsForm({ values, onChange, readOnly = false }: SensorSettingsFormProps) {
    return (
        <div className="space-y-4 p-4 bg-gray-50 dark:bg-gray-700/50 rounded-lg border border-gray-200 dark:border-gray-600">
            {/* Temperature */}
            <div className="grid grid-cols-3 gap-2 items-center">
                <div className="flex items-center gap-2 col-span-1">
                    <Thermometer className="w-4 h-4 text-red-500" />
                    <span className="text-sm font-medium">Temperature (°C)</span>
                </div>
                <input
                    type="number" placeholder="Min"
                    value={values.temp_min ?? ''}
                    onChange={e => onChange('temp_min', parseFloat(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
                <input
                    type="number" placeholder="Max"
                    value={values.temp_max ?? ''}
                    onChange={e => onChange('temp_max', parseFloat(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
            </div>

            {/* Humidity */}
            <div className="grid grid-cols-3 gap-2 items-center">
                <div className="flex items-center gap-2 col-span-1">
                    <Droplets className="w-4 h-4 text-blue-500" />
                    <span className="text-sm font-medium">Humidity (%)</span>
                </div>
                <input
                    type="number" placeholder="Min"
                    value={values.hum_min ?? ''}
                    onChange={e => onChange('hum_min', parseFloat(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
                <input
                    type="number" placeholder="Max"
                    value={values.hum_max ?? ''}
                    onChange={e => onChange('hum_max', parseFloat(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
            </div>

            {/* Soil */}
            <div className="grid grid-cols-3 gap-2 items-center">
                <div className="flex items-center gap-2 col-span-1">
                    <Activity className="w-4 h-4 text-amber-700" />
                    <span className="text-sm font-medium">Soil Moisture (%)</span>
                </div>
                <input
                    type="number" placeholder="Min"
                    value={values.soil_min ?? ''}
                    onChange={e => onChange('soil_min', parseInt(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
                <input
                    type="number" placeholder="Max"
                    value={values.soil_max ?? ''}
                    onChange={e => onChange('soil_max', parseInt(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
            </div>

            {/* Light */}
            <div className="grid grid-cols-3 gap-2 items-center">
                <div className="flex items-center gap-2 col-span-1">
                    <Sun className="w-4 h-4 text-yellow-500" />
                    <span className="text-sm font-medium">Light (Lux)</span>
                </div>
                <input
                    type="number" placeholder="Min"
                    value={values.light_min ?? ''}
                    onChange={e => onChange('light_min', parseFloat(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
                <input
                    type="number" placeholder="Max"
                    value={values.light_max ?? ''}
                    onChange={e => onChange('light_max', parseFloat(e.target.value))}
                    disabled={readOnly}
                    className="px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                />
            </div>

            {/* Timings */}
            <div className="grid grid-cols-2 gap-4 pt-2 border-t border-gray-200 dark:border-gray-600">
                <div>
                    <label className="block text-xs font-medium mb-1">Watering Duration (s)</label>
                    <input
                        type="number"
                        value={values.watering_duration_sec ?? ''}
                        onChange={e => onChange('watering_duration_sec', parseInt(e.target.value))}
                        disabled={readOnly}
                        className="w-full px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                    />
                </div>
                <div>
                    <label className="block text-xs font-medium mb-1">Measure Interval (s)</label>
                    <input
                        type="number"
                        value={values.measurement_interval_sec ?? ''}
                        onChange={e => onChange('measurement_interval_sec', parseInt(e.target.value))}
                        disabled={readOnly}
                        className="w-full px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                    />
                </div>
            </div>
        </div>
    );
}
