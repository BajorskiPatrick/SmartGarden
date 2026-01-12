import { useState, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../../lib/axios';
import type { DeviceSettings, PlantProfile } from '../../types';
import { X, Save, Loader2, Sprout, Plus, Trash2, Settings } from 'lucide-react';
import { PLANT_PROFILES } from '../../lib/plantProfiles';

interface DeviceSettingsDialogProps {
  macAddress: string;
  isOpen: boolean;
  onClose: () => void;
  currentDeviceName?: string;
}

export function DeviceSettingsDialog({ macAddress, isOpen, onClose, currentDeviceName }: DeviceSettingsDialogProps) {
  const queryClient = useQueryClient();
  const [formData, setFormData] = useState<Partial<DeviceSettings>>({});
  const [deviceName, setDeviceName] = useState(currentDeviceName || '');
  const [selectedProfileId, setSelectedProfileId] = useState<string>('');
  const [activeTab, setActiveTab] = useState<'presets' | 'custom'>('presets');
  const [isCreatingProfile, setIsCreatingProfile] = useState(false);
  const [newProfileName, setNewProfileName] = useState('');

  // Fetch Device Settings
  const { data: settings, isLoading: isLoadingSettings } = useQuery<DeviceSettings>({
    queryKey: ['settings', macAddress],
    queryFn: async () => {
      const response = await api.get(`/devices/${macAddress}/settings`);
      return response.data;
    },
    enabled: isOpen,
  });

  // Fetch Custom User Profiles
  const { data: userProfiles } = useQuery<PlantProfile[]>({
    queryKey: ['profiles'],
    queryFn: async () => {
      const response = await api.get('/profiles');
      return response.data;
    },
    enabled: isOpen,
  });
  
  // Sync form data
  useEffect(() => {
    if (settings) {
      setFormData(settings);
      // Try to match active profile
      if (settings.active_profile_name) {
          // Check presets
          const preset = PLANT_PROFILES.find(p => p.name === settings.active_profile_name);
          if (preset) {
              setSelectedProfileId(preset.id);
              setActiveTab('presets');
          } else {
              // Check user profiles
              const custom = userProfiles?.find(p => p.name === settings.active_profile_name);
              if (custom && custom.id) {
                  setSelectedProfileId(custom.id);
                  setActiveTab('custom');
              }
          }
      }
    }
    if (currentDeviceName) {
        setDeviceName(currentDeviceName);
    }
  }, [settings, currentDeviceName, userProfiles]);

  const updateSettingsMutation = useMutation({
    mutationFn: async (newSettings: Partial<DeviceSettings>) => {
      await api.post(`/devices/${macAddress}/settings`, newSettings);
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['settings', macAddress] });
      queryClient.invalidateQueries({ queryKey: ['devices'] }); // refresh card names
      onClose();
    },
  });

  const renameDeviceMutation = useMutation({
      mutationFn: async (name: string) => {
          await api.patch(`/devices/${macAddress}`, { friendlyName: name });
      }
  });

  const createProfileMutation = useMutation({
      mutationFn: async (profile: PlantProfile) => {
          await api.post('/profiles', profile);
      },
      onSuccess: () => {
          queryClient.invalidateQueries({ queryKey: ['profiles'] });
          setIsCreatingProfile(false);
          setNewProfileName('');
      }
  });

  const deleteProfileMutation = useMutation({
      mutationFn: async (id: string) => {
          await api.delete(`/profiles/${id}`);
      },
      onSuccess: () => {
          queryClient.invalidateQueries({ queryKey: ['profiles'] });
      }
  });

  const handleChange = (key: keyof DeviceSettings, value: string) => {
    const numValue = value === '' ? null : Number(value);
    setFormData((prev) => ({ ...prev, [key]: numValue }));
    // If user edits manually, clear profile selection (it's now custom)
    // Actually, keep it selected but maybe indicate "modified"? simplifying: don't clear for now or clear.
    // Let's clear to indicate divergence
    // setSelectedProfileId('');
  };

  const handleProfileSelect = (profile: PlantProfile) => {
    if (profile.id) setSelectedProfileId(profile.id);
    setFormData(prev => ({ ...prev, ...profile.settings, active_profile_name: profile.name }));
  };

  const handleSave = async () => {
    // 1. Rename Device if changed
    if (deviceName !== currentDeviceName) {
        await renameDeviceMutation.mutateAsync(deviceName);
    }
    // 2. Update Settings
    updateSettingsMutation.mutate(formData);
  };

  const handleCreateProfile = () => {
      if (!newProfileName.trim()) return;
      createProfileMutation.mutate({
          name: newProfileName,
          settings: formData,
          description: 'Custom user profile'
      });
  };

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/50 backdrop-blur-sm">
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl w-full max-w-3xl max-h-[90vh] overflow-hidden flex flex-col">
        {/* Header */}
        <div className="flex items-center justify-between p-4 border-b border-gray-200 dark:border-gray-700">
          <h2 className="text-xl font-semibold text-gray-900 dark:text-white flex items-center gap-2">
            <Settings className="w-5 h-5 text-gray-500" />
            Device Configuration
          </h2>
          <button onClick={onClose} className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-full transition-colors">
            <X className="w-6 h-6 text-gray-500" />
          </button>
        </div>

        {/* Content */}
        <div className="flex-1 overflow-y-auto p-6">
          {isLoadingSettings ? (
            <div className="flex justify-center py-12">
              <Loader2 className="w-8 h-8 animate-spin text-green-500" />
            </div>
          ) : (
            <div className="space-y-8">
              
              {/* Device Rename */}
              <section className="space-y-3">
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300">Device Name</label>
                  <div className="flex gap-2">
                      <input 
                         type="text" 
                         value={deviceName} 
                         onChange={(e) => setDeviceName(e.target.value)}
                         className="flex-1 px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                         placeholder="e.g. Living Room Window"
                      />
                  </div>
              </section>

              {/* Profiles Section */}
              <section className="bg-gray-50 dark:bg-gray-900/50 p-4 rounded-xl border border-gray-100 dark:border-gray-800">
                <div className="flex items-center justify-between mb-4">
                    <h3 className="text-sm font-bold text-gray-500 uppercase tracking-wider flex items-center gap-2">
                        <Sprout className="w-4 h-4" /> Plant Profile
                    </h3>
                    <div className="flex bg-gray-200 dark:bg-gray-700 rounded-lg p-1">
                        <button 
                            onClick={() => setActiveTab('presets')}
                            className={`px-3 py-1 text-xs font-medium rounded-md transition-all ${activeTab === 'presets' ? 'bg-white dark:bg-gray-600 shadow text-gray-900 dark:text-white' : 'text-gray-500 hover:text-gray-900 dark:text-gray-400'}`}
                        >
                            Presets
                        </button>
                        <button 
                            onClick={() => setActiveTab('custom')}
                            className={`px-3 py-1 text-xs font-medium rounded-md transition-all ${activeTab === 'custom' ? 'bg-white dark:bg-gray-600 shadow text-gray-900 dark:text-white' : 'text-gray-500 hover:text-gray-900 dark:text-gray-400'}`}
                        >
                            My Profiles
                        </button>
                    </div>
                </div>

                {/* Profile List */}
                <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 gap-2 max-h-40 overflow-y-auto pr-2 custom-scrollbar">
                    {activeTab === 'presets' ? (
                        PLANT_PROFILES.map(profile => (
                            <button
                                key={profile.id}
                                onClick={() => handleProfileSelect(profile)}
                                className={`text-left px-3 py-2 rounded-lg text-sm border transition-all ${selectedProfileId === profile.id ? 'bg-green-100 border-green-500 text-green-800 dark:bg-green-900/40 dark:text-green-300' : 'bg-white dark:bg-gray-800 border-gray-200 dark:border-gray-700 hover:border-green-300 dark:hover:border-green-700'}`}
                            >
                                <div className="font-medium truncate">{profile.name}</div>
                            </button>
                        ))
                    ) : (
                        <>
                           {userProfiles?.map(profile => (
                                <div key={profile.id} className="relative group">
                                    <button
                                        onClick={() => handleProfileSelect(profile)}
                                        className={`w-full text-left px-3 py-2 rounded-lg text-sm border transition-all ${selectedProfileId === profile.id ? 'bg-green-100 border-green-500 text-green-800 dark:bg-green-900/40 dark:text-green-300' : 'bg-white dark:bg-gray-800 border-gray-200 dark:border-gray-700 hover:border-green-300'}`}
                                    >
                                        <div className="font-medium truncate">{profile.name}</div>
                                    </button>
                                    <button 
                                        onClick={(e) => { e.stopPropagation(); deleteProfileMutation.mutate(profile.id!); }}
                                        className="absolute top-1 right-1 p-1 bg-red-100 text-red-600 rounded opacity-0 group-hover:opacity-100 transition-opacity hover:bg-red-200"
                                    >
                                        <Trash2 className="w-3 h-3" />
                                    </button>
                                </div>
                            ))}
                            <button 
                                onClick={() => setIsCreatingProfile(true)}
                                className="flex items-center justify-center gap-1 px-3 py-2 rounded-lg text-sm border border-dashed border-gray-300 text-gray-500 hover:bg-gray-50 dark:border-gray-600 dark:hover:bg-gray-800 transition-all"
                            >
                                <Plus className="w-4 h-4" /> New
                            </button>
                        </>
                    )}
                </div>
                
                {/* Active Profile Info */}
                {selectedProfileId && (
                     <div className="mt-3 text-xs text-gray-500 dark:text-gray-400 bg-white dark:bg-gray-800 p-2 rounded border border-gray-100 dark:border-gray-700">
                        Selected: <span className="font-semibold text-green-600">{activeTab === 'presets' ? PLANT_PROFILES.find(p => p.id === selectedProfileId)?.name : userProfiles?.find(p => p.id === selectedProfileId)?.name}</span>
                     </div>
                )}

                {/* Create Profile Dialog Overlay */}
                {isCreatingProfile && (
                     <div className="mt-4 p-3 bg-white dark:bg-gray-800 rounded-lg border border-green-200 dark:border-green-800 animate-in fade-in slide-in-from-top-2">
                        <label className="block text-xs font-semibold uppercase text-gray-500 mb-1">New Profile Name</label>
                        <div className="flex gap-2">
                            <input 
                                type="text" 
                                value={newProfileName}
                                onChange={e => setNewProfileName(e.target.value)}
                                className="flex-1 px-2 py-1 text-sm border rounded dark:bg-gray-700 dark:border-gray-600"
                                placeholder="My Custom Profile"
                            />
                            <button 
                                onClick={handleCreateProfile}
                                disabled={createProfileMutation.isPending}
                                className="px-3 py-1 bg-green-600 text-white text-sm rounded hover:bg-green-700"
                            >
                                Save
                            </button>
                            <button 
                                onClick={() => setIsCreatingProfile(false)}
                                className="px-3 py-1 bg-gray-200 text-gray-700 text-sm rounded hover:bg-gray-300"
                            >
                                Cancel
                            </button>
                        </div>
                        <p className="text-xs text-gray-400 mt-1">Saves current form values as a new profile.</p>
                     </div>
                )}
              </section>

              {/* General Settings */}
              <section>
                <h3 className="text-sm font-uppercase text-gray-500 font-bold mb-3 tracking-wider border-b pb-1">GENERAL</h3>
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-6">
                  <div>
                    <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                      Start interval (sec)
                    </label>
                    <input
                      type="number"
                      className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                      value={formData.measurement_interval_sec ?? ''}
                      onChange={(e) => handleChange('measurement_interval_sec', e.target.value)}
                    />
                  </div>
                  <div>
                    <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                      Watering Duration (sec)
                    </label>
                    <input
                      type="number"
                      className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                      value={formData.watering_duration_sec ?? ''}
                      onChange={(e) => handleChange('watering_duration_sec', e.target.value)}
                    />
                  </div>
                </div>
              </section>

              {/* Thresholds */}
              <section>
                 <h3 className="text-sm font-uppercase text-gray-500 font-bold mb-3 tracking-wider border-b pb-1">THRESHOLDS (Min / Max)</h3>
                 
                 <div className="space-y-4">
                    {/* Temperature */}
                    <div className="flex items-center gap-4">
                         <div className="w-24 text-sm font-medium text-gray-700 dark:text-gray-300 flex items-center gap-2">
                            Temp (°C)
                         </div>
                         <div className="flex-1 flex gap-2">
                             <input
                                type="number"
                                placeholder="Min"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.temp_min ?? ''}
                                onChange={(e) => handleChange('temp_min', e.target.value)}
                             />
                             <input
                                type="number"
                                placeholder="Max"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.temp_max ?? ''}
                                onChange={(e) => handleChange('temp_max', e.target.value)}
                             />
                         </div>
                    </div>

                    {/* Humidity */}
                    <div className="flex items-center gap-4">
                         <div className="w-24 text-sm font-medium text-gray-700 dark:text-gray-300">Humidity (%)</div>
                         <div className="flex-1 flex gap-2">
                             <input
                                type="number"
                                placeholder="Min"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.hum_min ?? ''}
                                onChange={(e) => handleChange('hum_min', e.target.value)}
                             />
                             <input
                                type="number"
                                placeholder="Max"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.hum_max ?? ''}
                                onChange={(e) => handleChange('hum_max', e.target.value)}
                             />
                         </div>
                    </div>

                    {/* Soil */}
                    <div className="flex items-center gap-4">
                         <div className="w-24 text-sm font-medium text-gray-700 dark:text-gray-300">Soil (%)</div>
                         <div className="flex-1 flex gap-2">
                             <input
                                type="number"
                                placeholder="Min"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.soil_min ?? ''}
                                onChange={(e) => handleChange('soil_min', e.target.value)}
                             />
                             <input
                                type="number"
                                placeholder="Max"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.soil_max ?? ''}
                                onChange={(e) => handleChange('soil_max', e.target.value)}
                             />
                         </div>
                    </div>

                    {/* Light */}
                    <div className="flex items-center gap-4">
                         <div className="w-24 text-sm font-medium text-gray-700 dark:text-gray-300">Light (lx)</div>
                         <div className="flex-1 flex gap-2">
                             <input
                                type="number"
                                placeholder="Min"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.light_min ?? ''}
                                onChange={(e) => handleChange('light_min', e.target.value)}
                             />
                             <input
                                type="number"
                                placeholder="Max"
                                className="w-full px-3 py-2 border rounded-lg dark:bg-gray-700 dark:border-gray-600 dark:text-white focus:ring-2 focus:ring-green-500 outline-none"
                                value={formData.light_max ?? ''}
                                onChange={(e) => handleChange('light_max', e.target.value)}
                             />
                         </div>
                    </div>
                 </div>
              </section>

            </div>
          )}
        </div>

        {/* Footer */}
        <div className="p-4 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-3 bg-gray-50 dark:bg-gray-800/50">
          <button
            onClick={onClose}
            className="px-4 py-2 text-gray-700 dark:text-gray-300 font-medium hover:bg-gray-200 dark:hover:bg-gray-700 rounded-lg transition-colors"
          >
            Cancel
          </button>
          <button
            onClick={handleSave}
            disabled={updateSettingsMutation.isPending || renameDeviceMutation.isPending}
            className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white font-medium rounded-lg flex items-center gap-2 transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
          >
            {updateSettingsMutation.isPending ? (
               <Loader2 className="w-4 h-4 animate-spin" />
            ) : (
               <Save className="w-4 h-4" />
            )}
            Save Configuration
          </button>
        </div>
      </div>
    </div>
  );
}

