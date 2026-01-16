import React, { useState } from 'react';
import { View, Text, TouchableOpacity, ScrollView, ActivityIndicator, Alert, RefreshControl } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useRoute, useNavigation } from '@react-navigation/native';
import { ArrowLeft, Thermometer, Droplets, Sun, Activity, Play } from 'lucide-react-native';
import { Device } from '../hooks/useDevices';
import { api } from '../lib/axios';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';

export default function DeviceDetailsScreen() {
    const route = useRoute();
    const navigation = useNavigation();
    const { device: initialDevice } = route.params as { device: Device };
    const mac = initialDevice?.macAddress;
    const queryClient = useQueryClient();

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
                    lastMeasurementTime: latestTelemetry.timestamp
                };
            }
            
            return deviceData;
        },
        initialData: initialDevice,
        refetchInterval: 5000, // Poll every 5 seconds for updates
    });

    const measureMutation = useMutation({
        mutationFn: async () => {
            await api.post(`/devices/${mac}/measure`);
        },
        onSuccess: () => {
            Alert.alert("Success", "Measurement requested. Data will update shortly.");
            // Invalidate query to force refresh soon, or let polling handle it
            queryClient.invalidateQueries({ queryKey: ['device', mac] });
            setTimeout(refetch, 2000); // Try to fetch a bit later
        },
        onError: (err) => {
            Alert.alert("Error", "Failed to request measurement");
        }
    });

    if (!device) {
        return (
            <SafeAreaView className="flex-1 items-center justify-center">
                <Text>Device Not Found</Text>
            </SafeAreaView>
        );
    }

    const StatCard = ({ icon, label, value, unit, color }: any) => (
        <View className="bg-white p-4 rounded-xl shadow-sm border border-gray-100 w-[48%] mb-4">
            <View className={`w-10 h-10 rounded-full items-center justify-center mb-2 ${color}`}>
                {icon}
            </View>
            <Text className="text-gray-500 text-xs uppercase">{label}</Text>
            <Text className="text-xl font-bold text-gray-800">
                {value ?? '--'} <Text className="text-sm font-normal text-gray-400">{unit}</Text>
            </Text>
        </View>
    );

    return (
        <SafeAreaView className="flex-1 bg-gray-50">
            {/* Header */}
            <View className="px-6 py-4 bg-white flex-row items-center border-b border-gray-100 shadow-sm">
                <TouchableOpacity onPress={() => navigation.goBack()} className="mr-4">
                    <ArrowLeft size={24} color="#374151" />
                </TouchableOpacity>
                <View>
                    <Text className="text-lg font-bold text-gray-800">{device.friendlyName || device.macAddress}</Text>
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
                        icon={<Thermometer size={20} color="#ef4444" />}
                        label="Temperature"
                        value={device.temperature}
                        unit="°C"
                        color="bg-red-50"
                    />
                    <StatCard 
                        icon={<Droplets size={20} color="#3b82f6" />}
                        label="Soil Moisture"
                        value={device.soilMoisture}
                        unit="%"
                        color="bg-blue-50"
                    />
                    <StatCard 
                        icon={<Sun size={20} color="#eab308" />}
                        label="Light Level"
                        value={device.lightLux}
                        unit="lux"
                        color="bg-yellow-50"
                    />
                    <StatCard 
                        icon={<Activity size={20} color="#a855f7" />}
                        label="Humidity"
                        value={device.humidity}
                        unit="%"
                        color="bg-purple-50"
                    />
                </View>

                {/* Actions */}
                <View className="bg-white p-4 rounded-xl border border-gray-100 mt-2">
                    <Text className="font-bold text-gray-800 mb-4">Device Actions</Text>
                    
                    <TouchableOpacity 
                        className={`bg-green-600 p-4 rounded-xl flex-row items-center justify-center ${measureMutation.isPending ? 'opacity-50' : ''}`}
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
                    <Text className="text-center text-xs text-gray-400 mt-2">REQUESTS NEW TELEMETRY</Text>
                </View>

                {/* Additional Info */}
                <View className="bg-white p-4 rounded-xl border border-gray-100 mt-4 mb-8">
                    <Text className="font-bold text-gray-800 mb-2">Device Information</Text>
                    <Text className="text-gray-500 mt-1">MAC Address: {device.macAddress}</Text>
                    <Text className="text-gray-500 mt-1">Last Seen: {device.lastSeen ? new Date(device.lastSeen).toLocaleString() : 'Never'}</Text>
                    <Text className="text-gray-500 mt-1">Last Measured: {device.lastMeasurementTime ? new Date(device.lastMeasurementTime).toLocaleString() : 'Never'}</Text>
                </View>
            </ScrollView>
        </SafeAreaView>
    );
}
