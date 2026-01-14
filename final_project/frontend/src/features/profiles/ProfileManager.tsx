import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../../lib/axios';
import type { PlantProfile, DeviceSettings } from '../../types';
import { Loader2, Plus, Trash2, Save } from 'lucide-react';
import { SensorSettingsForm } from '../device/SensorSettingsForm';

export function ProfileManager({ onSelect }: { onSelect?: (profile: PlantProfile) => void }) {
    const queryClient = useQueryClient();
    const [isCreating, setIsCreating] = useState(false);
    const [newProfile, setNewProfile] = useState<Partial<PlantProfile>>({
        name: '',
        description: '',
        measurement_interval_sec: 900,
        watering_duration_sec: 10,
        temp_min: 18, temp_max: 28,
        hum_min: 40, hum_max: 80,
        soil_min: 30, soil_max: 70,
        light_min: 100, light_max: 2000
    });

    const { data: profiles, isLoading } = useQuery<PlantProfile[]>({
        queryKey: ['profiles'],
        queryFn: async () => {
            const response = await api.get('/profiles');
            return response.data;
        },
    });

    const createMutation = useMutation({
        mutationFn: async (profile: Partial<PlantProfile>) => {
            const response = await api.post('/profiles', profile);
            return response.data;
        },
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['profiles'] });
            setIsCreating(false);
            setNewProfile({
                name: '',
                description: '',
                measurement_interval_sec: 900,
                watering_duration_sec: 10,
                temp_min: 18, temp_max: 28,
                hum_min: 40, hum_max: 80,
                soil_min: 30, soil_max: 70,
                light_min: 100, light_max: 2000
            });
        },
    });

    const deleteMutation = useMutation({
        mutationFn: async (id: string) => {
            await api.delete(`/profiles/${id}`);
        },
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['profiles'] });
        },
    });

    const handleSave = () => {
        if (!newProfile.name) return;
        createMutation.mutate(newProfile);
    };

    const handleSettingChange = (key: keyof DeviceSettings, value: number) => {
        setNewProfile(prev => ({
            ...prev,
            [key]: value
        }));
    };

    if (isLoading) {
        return <div className="flex justify-center p-4"><Loader2 className="animate-spin" /></div>;
    }

    return (
        <div className="space-y-4">
            <div className="flex justify-between items-center">
                <h3 className="text-lg font-medium text-gray-900 dark:text-gray-100">Saved Profiles</h3>
                {!isCreating && (
                    <button
                        onClick={() => setIsCreating(true)}
                        className="flex items-center gap-1 text-sm bg-green-600 text-white px-3 py-1.5 rounded-md hover:bg-green-700"
                    >
                        <Plus className="w-4 h-4" /> New Profile
                    </button>
                )}
            </div>

            {isCreating && (
                <div className="bg-gray-50 dark:bg-gray-800 p-4 rounded-lg border border-green-200 dark:border-green-900 space-y-3">
                    <input
                        type="text"
                        className="w-full px-3 py-2 border rounded-md dark:bg-gray-700 dark:border-gray-600"
                        placeholder="Profile Name (e.g., Tomato)"
                        value={newProfile.name}
                        onChange={e => setNewProfile({ ...newProfile, name: e.target.value })}
                    />
                    <input
                        type="text"
                        className="w-full px-3 py-2 border rounded-md dark:bg-gray-700 dark:border-gray-600"
                        placeholder="Description (optional)"
                        value={newProfile.description}
                        onChange={e => setNewProfile({ ...newProfile, description: e.target.value })}
                    />

                    <SensorSettingsForm
                        values={newProfile}
                        onChange={handleSettingChange}
                    />

                    <div className="flex justify-end gap-2 pt-2">
                        <button onClick={() => setIsCreating(false)} className="text-gray-500 hover:text-gray-700">Cancel</button>
                        <button onClick={handleSave} className="bg-green-600 text-white px-3 py-1 rounded flex items-center gap-1">
                            <Save className="w-4 h-4" /> Save
                        </button>
                    </div>
                </div>
            )}

            <div className="grid grid-cols-1 gap-2 max-h-60 overflow-y-auto">
                {profiles?.length === 0 && !isCreating && (
                    <p className="text-sm text-gray-500 italic text-center py-2">No profiles saved yet.</p>
                )}
                {profiles?.map(profile => (
                    <div key={profile.id} className="flex justify-between items-center p-3 bg-white dark:bg-gray-800 border rounded-lg shadow-sm hover:border-green-500 transition-colors cursor-pointer group"
                        onClick={() => onSelect?.(profile)}>
                        <div>
                            <p className="font-medium text-gray-900 dark:text-gray-100">{profile.name}</p>
                            <p className="text-xs text-gray-500">{profile.description}</p>
                        </div>
                        <div className="flex items-center gap-2">
                            {onSelect && <span className="text-xs text-green-600 font-semibold group-hover:block hidden">Apply</span>}
                            <button
                                onClick={(e) => { e.stopPropagation(); deleteMutation.mutate(profile.id!); }}
                                className="text-red-400 hover:text-red-600 p-1 rounded-full hover:bg-red-50 dark:hover:bg-red-900/20"
                                title="Delete Profile"
                            >
                                <Trash2 className="w-4 h-4" />
                            </button>
                        </div>
                    </div>
                ))}
            </div>
        </div>
    );
}
