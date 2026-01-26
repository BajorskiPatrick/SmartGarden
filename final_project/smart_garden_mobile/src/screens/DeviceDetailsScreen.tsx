import React, { useState } from 'react';
import { View, Text, TouchableOpacity, ScrollView, ActivityIndicator, Alert, RefreshControl, Modal, FlatList, TextInput } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useRoute, useNavigation } from '@react-navigation/native';
import { ArrowLeft, Thermometer, Droplets, Sun, Activity, Trash2, Sprout, ChevronRight, X, Waves, AlertTriangle } from 'lucide-react-native';
import { PLANT_PROFILES } from '../lib/plantProfiles';
import { Device, PlantProfile, DeviceSettings } from '../types';
import { api } from '../lib/axios';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';


export default function DeviceDetailsScreen() {
    const route = useRoute();
    const navigation = useNavigation();
    const { device: initialDevice } = route.params as { device: Device };
    const mac = initialDevice?.macAddress;
    const queryClient = useQueryClient();

    const [waterDuration, setWaterDuration] = useState(5);
    const [isProfileModalOpen, setIsProfileModalOpen] = useState(false);
    const [isRenameModalOpen, setIsRenameModalOpen] = useState(false);
    const [newName, setNewName] = useState(initialDevice?.friendlyName || "");

    // Fetch latest data (DeviceInfo + Latest Telemetry)
    const { data: device, isLoading, refetch } = useQuery({
        queryKey: ['device', mac],
        queryFn: async () => {
            // Parallel fetch
            const [deviceRes, telemetryRes] = await Promise.all([
                api.get(`/devices/${mac}`),
                api.get(`/devices/${mac}/telemetry?size=1`)
            ]);
            
            const deviceData = deviceRes.data;
            const latestTelemetry = telemetryRes.data.content?.[0]; // Spring Data Page structure
            
            // Merge telemetry into device object if available
            if (latestTelemetry) {
                return {
                    ...deviceData,
                    temperature: latestTelemetry.temperature,
                    humidity: latestTelemetry.humidity,
                    soilMoisture: latestTelemetry.soilMoisture,
                    lightLux: latestTelemetry.lightLux,
                    waterTankOk: latestTelemetry.waterTankOk,
                    lastMeasurementTime: latestTelemetry.timestamp
                };
            }
            
            return deviceData;
        },
        initialData: initialDevice,
        refetchInterval: 5000, 
    });

    // Fetch Profiles
    const { data: profiles } = useQuery<PlantProfile[]>({
        queryKey: ['profiles'],
        queryFn: async () => {
            const res = await api.get('/profiles');
            return res.data;
        }
    });

    // Update newName when device loads
    React.useEffect(() => {
        if (device?.friendlyName) {
            setNewName(device.friendlyName);
        }
    }, [device]);

    // Fetch Device Settings for Ranges
    const { data: settings } = useQuery<DeviceSettings>({
        queryKey: ['settings', mac],
        queryFn: async () => {
            const res = await api.get(`/devices/${mac}/settings`);
            return res.data;
        }
    });

    // Mutations
    const renameMutation = useMutation({
        mutationFn: async (name: string) => {
            await api.patch(`/devices/${mac}`, { friendlyName: name });
        },
        onSuccess: () => {
            setIsRenameModalOpen(false);
            Alert.alert("Success", "Device renamed successfully.");
            queryClient.invalidateQueries({ queryKey: ['device', mac] });
            queryClient.invalidateQueries({ queryKey: ['devices'] }); // Refresh dashboard list
            refetch();
        },
        onError: () => Alert.alert("Error", "Failed to rename device")
    });

    // Mutations
    const measureMutation = useMutation({
        mutationFn: async () => {
            await api.post(`/devices/${mac}/measure`);
        },
        onSuccess: () => {
            Alert.alert("Request Sent", "Command sent. Data will update shortly.");
            queryClient.invalidateQueries({ queryKey: ['device', mac] });
            setTimeout(refetch, 2000); 
        },
        onError: () => Alert.alert("Error", "Failed to request measurement")
    });

    const waterMutation = useMutation({
        mutationFn: async () => {
             // This turns the LED Blue on the ESP32 while watering
            await api.post(`/devices/${mac}/water?duration=${waterDuration}`);
        },
        onSuccess: () => {
            Alert.alert("Watering Started", `Watering for ${waterDuration} seconds.`);
        },
        onError: () => Alert.alert("Error", "Failed to start watering")
    });

    const deleteDeviceMutation = useMutation({
        mutationFn: async () => {
             // Factory reset then delete
             try {
                await api.post(`/devices/${mac}/settings`, { factory_reset: true });
             } catch (e) {
                 console.log("Reset failed, deleting anyway");
             }
             await api.delete(`/devices/${mac}`);
        },
        onSuccess: () => {
            Alert.alert("Device Deleted", "Device has been removed from your account.");
            queryClient.invalidateQueries({ queryKey: ['devices'] });
            navigation.navigate('Dashboard' as never);
        },
        onError: () => Alert.alert("Error", "Failed to delete device")
    });

    const applyProfileMutation = useMutation({
        mutationFn: async (profile: PlantProfile) => {
            const { id, userId, name, description, ...settings } = profile;
            console.log("Applying profile:", name);
            // Update settings on device
            await api.post(`/devices/${mac}/settings`, { ...settings, active_profile_name: name });
        },
        onSuccess: () => {
            setIsProfileModalOpen(false);
            Alert.alert("Profile Applied", "Device settings updated successfully.");
            refetch();
            queryClient.invalidateQueries({ queryKey: ['settings', mac] });
        },
        onError: () => Alert.alert("Error", "Failed to apply profile")
    });


    // Handlers
    const confirmDelete = () => {
        Alert.alert(
            "Delete Device",
            "Are you sure? This will reset the device and remove it from your account.",
            [
                { text: "Cancel", style: "cancel" },
                { text: "Delete", style: "destructive", onPress: () => deleteDeviceMutation.mutate() }
            ]
        );
    };

    if (!device) {
        return (
            <SafeAreaView className="flex-1 items-center justify-center">
                <Text>Device Not Found</Text>
            </SafeAreaView>
        );
    }

    const StatCard = ({ icon, label, value, unit, baseColor, min, max }: any) => {
        let statusColor = baseColor; // Default
        let warning = null;
        let isError = false;

        if (value !== undefined && value !== null && min !== undefined && max !== undefined) {
             if (value < min) {
                 statusColor = "bg-red-50 border-red-200";
                 warning = "Too Low!";
                 isError = true;
             } else if (value > max) {
                 statusColor = "bg-red-50 border-red-200";
                 warning = "Too High!";
                 isError = true;
             } else {
                 statusColor = baseColor;
                 warning = "OK";
             }
        }

        return (
            <View className={`p-4 rounded-xl shadow-sm border w-[48%] mb-4 ${isError ? 'border-red-300 bg-red-50' : 'border-gray-100 bg-white'}`}>
                <View className="flex-row justify-between">
                    <View className={`w-8 h-8 rounded-full items-center justify-center mb-2 ${baseColor.replace('bg-', 'bg-opacity-20 ')}`}>
                        {icon}
                    </View>
                    {isError && <AlertTriangle size={16} color="#ef4444" />}
                </View>
                
                <Text className="text-gray-500 text-xs uppercase">{label}</Text>
                <Text className={`text-xl font-bold ${isError ? 'text-red-600' : 'text-gray-800'}`}>
                    {value ?? '--'} <Text className="text-sm font-normal text-gray-400">{unit}</Text>
                </Text>
                
                {min !== undefined && (
                   <View className="mt-1">
                       <Text className={`text-xs font-bold ${isError ? 'text-red-500' : 'text-green-600'}`}>
                           {warning}
                       </Text>
                       <Text className="text-[10px] text-gray-400">Target: {min}-{max}</Text>
                   </View>
                )}
            </View>
        );
    };

    return (
        <SafeAreaView className="flex-1 bg-gray-50">

            {/* Header */}
            <View className="px-6 py-4 bg-white flex-row items-center border-b border-gray-100 shadow-sm">
                <TouchableOpacity onPress={() => navigation.goBack()} className="mr-4">
                    <ArrowLeft size={24} color="#374151" />
                </TouchableOpacity>
                <View className="flex-1">
                     <View className="flex-row items-center">
                        <Text className="text-lg font-bold text-gray-800 mr-2">{device.friendlyName || device.macAddress}</Text>
                        <TouchableOpacity onPress={() => setIsRenameModalOpen(true)}>
                            <Sprout size={16} color="gray" /> 
                         </TouchableOpacity>
                     </View>
                    <Text className={`text-xs ${device.online ? 'text-green-600' : 'text-gray-400'}`}>
                        {device.online ? '● Online' : '○ Offline'}
                    </Text>
                </View>
            </View>

            <ScrollView 
                className="flex-1 p-6"
                refreshControl={<RefreshControl refreshing={isLoading} onRefresh={refetch} />}
            >
                 {/* Status Overview */} 
                 <View className="flex-row flex-wrap justify-between">
                    <StatCard 
                        icon={<Thermometer size={18} color="#ef4444" />}
                        label="Temperature"
                        value={device.temperature}
                        unit="°C"
                        baseColor="bg-red-50"
                        min={settings?.temp_min}
                        max={settings?.temp_max}
                    />
                    <StatCard 
                        icon={<Droplets size={18} color="#3b82f6" />}
                        label="Soil Moisture"
                        value={device.soilMoisture}
                        unit="%"
                        baseColor="bg-blue-50"
                        min={settings?.soil_min}
                        max={settings?.soil_max}
                    />
                    <StatCard 
                        icon={<Sun size={18} color="#eab308" />}
                        label="Light Level"
                        value={device.lightLux}
                        unit="lux"
                        baseColor="bg-yellow-50"
                        min={settings?.light_min}
                        max={settings?.light_max}
                    />
                    <StatCard 
                        icon={<Activity size={18} color="#a855f7" />}
                        label="Humidity"
                        value={device.humidity}
                        unit="%"
                        baseColor="bg-purple-50"
                        min={settings?.hum_min}
                        max={settings?.hum_max}
                    />
                </View>

                 {/* Water Tank Status */}
                 <View className={`p-4 rounded-xl border mb-4 flex-row items-center ${device.waterTankOk ? 'bg-blue-50 border-blue-100' : 'bg-red-50 border-red-100'}`}>
                    <Waves size={24} color={device.waterTankOk ? '#3b82f6' : '#ef4444'} className="mr-3" />
                    <View>
                        <Text className="text-gray-500 text-xs uppercase">Water Tank</Text>
                        <Text className={`text-lg font-bold ${device.waterTankOk ? 'text-blue-700' : 'text-red-600'}`}>
                            {device.waterTankOk ? 'Water Level OK' : 'Water Level LOW!'}
                        </Text>
                        {!device.waterTankOk && (
                            <Text className="text-red-800 text-xs mt-1">Please refill the water tank.</Text>
                        )}
                    </View>
                 </View>

                {/* Plant Profile Selection */}
                 <View className="bg-white p-4 rounded-xl border border-gray-100 mt-2">
                    <Text className="font-bold text-gray-800 mb-2">Plant Profile</Text>
                    <TouchableOpacity 
                        className="flex-row items-center justify-between bg-gray-50 p-3 rounded-lg border border-gray-200"
                        onPress={() => setIsProfileModalOpen(true)}
                    >
                        <View className="flex-row items-center">
                            <Sprout size={20} color="green" className="mr-2" />
                            <Text className="text-gray-700 font-medium">
                                {device.activeProfileName || "No Profile Selected"}
                            </Text>
                        </View>
                        <ChevronRight size={20} color="gray" />
                    </TouchableOpacity>
                </View>


                {/* Actions */}
                <View className="bg-white p-4 rounded-xl border border-gray-100 mt-4">
                    <Text className="font-bold text-gray-800 mb-4">Manual Controls</Text>
                    
                    {/* Measurement */}
                    <TouchableOpacity 
                        className={`bg-blue-600 p-4 rounded-xl flex-row items-center justify-center mb-4 ${measureMutation.isPending ? 'opacity-50' : ''}`}
                        onPress={() => measureMutation.mutate()}
                        disabled={measureMutation.isPending}
                    >
                        {measureMutation.isPending ? (
                            <ActivityIndicator color="white" />
                        ) : (
                            <>
                            <Activity size={20} color="white" className="mr-2" />
                            <Text className="text-white font-bold ml-2">Measure Now</Text>
                            </>
                        )}
                    </TouchableOpacity>

                     {/* Watering */}
                    <View className="border-t border-gray-100 pt-4">
                        <Text className="text-sm font-medium text-gray-600 mb-2">Water for {waterDuration} seconds</Text>
                        <View className="flex-row justify-between mb-3">
                            {[5, 10, 20, 30].map(sec => (
                                <TouchableOpacity 
                                    key={sec}
                                    onPress={() => setWaterDuration(sec)}
                                    className={`px-4 py-2 rounded-lg ${waterDuration === sec ? 'bg-green-100 border border-green-500' : 'bg-gray-100 border border-transparent'}`}
                                >
                                    <Text className={waterDuration === sec ? 'text-green-700 font-bold' : 'text-gray-600'}>{sec}s</Text>
                                </TouchableOpacity>
                            ))}
                        </View>
                        <TouchableOpacity 
                             className={`bg-green-600 p-4 rounded-xl flex-row items-center justify-center ${waterMutation.isPending ? 'opacity-50' : ''}`}
                             onPress={() => waterMutation.mutate()}
                             disabled={waterMutation.isPending}
                        >
                            {waterMutation.isPending ? (
                                <ActivityIndicator color="white" />
                            ) : (
                                <>
                                <Droplets size={20} color="white" className="mr-2" />
                                <Text className="text-white font-bold ml-2">Water Now</Text>
                                </>
                            )}
                        </TouchableOpacity>
                         <Text className="text-center text-xs text-gray-400 mt-2">Turns LED Blue</Text>
                    </View>
                </View>

                {/* Device Info */}
                <View className="bg-white p-4 rounded-xl border border-gray-100 mt-4 mb-8">
                    <Text className="font-bold text-gray-800 mb-2">Device Information</Text>
                    <Text className="text-gray-500 mt-1">MAC Address: {device.macAddress}</Text>
                    <Text className="text-gray-500 mt-1">Last Seen: {device.lastSeen ? new Date(device.lastSeen).toLocaleString() : 'Never'}</Text>
                    <Text className="text-gray-500 mt-1">Last Measured: {device.lastMeasurementTime ? new Date(device.lastMeasurementTime).toLocaleString() : 'Never'}</Text>
                </View>

                {/* Delete Zone */}
                <TouchableOpacity 
                    className="mt-6 mb-10 bg-red-50 p-4 rounded-xl border border-red-100 flex-row items-center justify-center"
                    onPress={confirmDelete}
                >
                     <Trash2 size={20} color="#dc2626" className="mr-2" />
                     <Text className="text-red-600 font-bold">Delete Device</Text>
                </TouchableOpacity>

            </ScrollView>

            {/* Profile Modal */}
            <Modal
                visible={isProfileModalOpen}
                animationType="slide"
                presentationStyle="pageSheet"
                onRequestClose={() => setIsProfileModalOpen(false)}
            >
                <SafeAreaView className="flex-1 bg-white">
                    <View className="px-6 py-4 border-b border-gray-100 flex-row justify-between items-center">
                        <Text className="text-xl font-bold text-gray-800">Select Plant Profile</Text>
                        <TouchableOpacity onPress={() => setIsProfileModalOpen(false)}>
                            <X size={24} color="gray" />
                        </TouchableOpacity>
                    </View>
                    <FlatList
                        data={[...(profiles || []), ...PLANT_PROFILES]}
                        keyExtractor={item => item.id!}
                        contentContainerStyle={{ padding: 20 }}
                        renderItem={({ item }) => (
                            <TouchableOpacity 
                                className={`p-4 mb-3 rounded-xl border ${device.activeProfileName === item.name ? 'bg-green-50 border-green-500' : 'bg-white border-gray-200'}`}
                                onPress={() => applyProfileMutation.mutate(item)}
                            >
                                <View className="flex-row justify-between items-center">
                                    <View className="flex-1">
                                        <Text className={`text-lg font-bold ${device.activeProfileName === item.name ? 'text-green-800' : 'text-gray-800'}`}>
                                            {item.name}
                                        </Text>
                                        <Text className="text-gray-500 text-sm mt-1">{item.description}</Text>
                                        {/* Show Profile Ranges */}
                                        <View className="flex-row mt-2 gap-2">
                                            <Text className="text-xs text-gray-400">🌡️ {item.temp_min}-{item.temp_max}°C</Text>
                                            <Text className="text-xs text-gray-400">💧 {item.soil_min}-{item.soil_max}%</Text>
                                        </View>
                                    </View>
                                    {device.activeProfileName === item.name && <Sprout color="green" size={20} />}
                                </View>
                            </TouchableOpacity>
                        )}
                    />
                </SafeAreaView>
            </Modal>

            {/* Rename Modal */}
            <Modal
                visible={isRenameModalOpen}
                animationType="fade"
                transparent={true}
                onRequestClose={() => setIsRenameModalOpen(false)}
            >
                <View className="flex-1 bg-black/50 justify-center items-center p-6">
                    <View className="bg-white p-6 rounded-2xl w-full max-w-sm">
                        <Text className="text-lg font-bold text-gray-800 mb-4">Rename Device</Text>
                        <View className="bg-gray-100 p-3 rounded-xl mb-4">
                             <TextInput
                                value={newName}
                                onChangeText={setNewName}
                                placeholder="Enter new name"
                                className="text-gray-800"
                                autoFocus
                             />
                        </View>
                        <View className="flex-row justify-end gap-2">
                             <TouchableOpacity 
                                onPress={() => setIsRenameModalOpen(false)}
                                className="px-4 py-2"
                             >
                                 <Text className="text-gray-500 font-bold">Cancel</Text>
                             </TouchableOpacity>
                             <TouchableOpacity 
                                onPress={() => renameMutation.mutate(newName)}
                                className="bg-green-600 px-4 py-2 rounded-lg"
                                disabled={renameMutation.isPending}
                             >
                                 <Text className="text-white font-bold">{renameMutation.isPending ? 'Saving...' : 'Save'}</Text>
                             </TouchableOpacity>
                        </View>
                    </View>
                </View>
            </Modal>
        </SafeAreaView>
    );
}
