import React, { useState } from 'react';
import { View, Text, TextInput, TouchableOpacity, ScrollView, Alert, Switch } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useNavigation } from '@react-navigation/native';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../lib/axios';
import { ArrowLeft } from 'lucide-react-native';

export default function CreateProfileScreen() {
    const navigation = useNavigation();
    const queryClient = useQueryClient();

    const [form, setForm] = useState({
        name: '',
        description: '',
        temp_min: '18',
        temp_max: '28',
        soil_min: '30',
        soil_max: '60',
        hum_min: '40',
        hum_max: '80',
        light_min: '1000',
        light_max: '5000',
        watering_duration_sec: '5',
        measurement_interval_sec: '900'
    });

    const createMutation = useMutation({
        mutationFn: async (data: any) => {
            await api.post('/profiles', data);
        },
        onSuccess: () => {
             Alert.alert("Success", "Profile created!");
             queryClient.invalidateQueries({ queryKey: ['profiles'] });
             navigation.goBack();
        },
        onError: () => Alert.alert("Error", "Failed to create profile")
    });

    const handleSubmit = () => {
        if (!form.name) return Alert.alert("Error", "Name is required");

        // Validation: Min < Max
        if (Number(form.temp_min) > Number(form.temp_max)) return Alert.alert("Validation Error", "Temperature Min cannot be greater than Max");
        if (Number(form.soil_min) > Number(form.soil_max)) return Alert.alert("Validation Error", "Soil Moisture Min cannot be greater than Max");
        if (Number(form.hum_min) > Number(form.hum_max)) return Alert.alert("Validation Error", "Humidity Min cannot be greater than Max");
        if (Number(form.light_min) > Number(form.light_max)) return Alert.alert("Validation Error", "Light Min cannot be greater than Max");

        const payload = {
            name: form.name,
            description: form.description,
            temp_min: Number(form.temp_min),
            temp_max: Number(form.temp_max),
            soil_min: Number(form.soil_min),
            soil_max: Number(form.soil_max),
            hum_min: Number(form.hum_min),
            hum_max: Number(form.hum_max),
            light_min: Number(form.light_min),
            light_max: Number(form.light_max),
            watering_duration_sec: Number(form.watering_duration_sec),
            measurement_interval_sec: Number(form.measurement_interval_sec)
        };
        
        createMutation.mutate(payload);
    };

    const InputGroup = ({ label, fieldMin, fieldMax, unit }: { label: string, fieldMin: keyof typeof form, fieldMax: keyof typeof form, unit: string }) => (
        <View className="mb-4 bg-gray-50 p-3 rounded-xl border border-gray-100">
            <Text className="text-gray-700 font-bold mb-2">{label} ({unit})</Text>
            <View className="flex-row gap-4">
                <View className="flex-1">
                     <Text className="text-xs text-gray-500 mb-1">Min</Text>
                     <TextInput 
                        className="bg-white p-3 rounded-lg border border-gray-200"
                        keyboardType="numeric"
                        value={form[fieldMin]}
                        onChangeText={t => setForm({...form, [fieldMin]: t})}
                     />
                </View>
                <View className="flex-1">
                     <Text className="text-xs text-gray-500 mb-1">Max</Text>
                     <TextInput 
                        className="bg-white p-3 rounded-lg border border-gray-200"
                        keyboardType="numeric"
                        value={form[fieldMax]}
                        onChangeText={t => setForm({...form, [fieldMax]: t})}
                     />
                </View>
            </View>
        </View>
    );

    return (
        <SafeAreaView className="flex-1 bg-white">
            <View className="px-6 py-4 border-b border-gray-100 flex-row items-center">
                <TouchableOpacity onPress={() => navigation.goBack()} className="mr-4">
                    <ArrowLeft size={24} color="#374151" />
                </TouchableOpacity>
                <Text className="text-xl font-bold text-gray-800">New Profile</Text>
            </View>

            <ScrollView className="p-6">
                <Text className="text-sm font-bold text-gray-700 mb-1">Profile Name</Text>
                <TextInput 
                    className="bg-gray-50 p-4 rounded-xl border border-gray-200 mb-4"
                    placeholder="e.g. Tomatoes, Ferns..."
                    value={form.name}
                    onChangeText={t => setForm({...form, name: t})}
                />

                <Text className="text-sm font-bold text-gray-700 mb-1">Description</Text>
                <TextInput 
                    className="bg-gray-50 p-4 rounded-xl border border-gray-200 mb-6"
                    placeholder="Optional description"
                    value={form.description}
                    onChangeText={t => setForm({...form, description: t})}
                />

                <Text className="text-lg font-bold text-gray-800 mb-4">Target Ranges</Text>
                
                <InputGroup label="Temperature" unit="°C" fieldMin="temp_min" fieldMax="temp_max" />
                <InputGroup label="Soil Moisture" unit="%" fieldMin="soil_min" fieldMax="soil_max" />
                <InputGroup label="Humidity" unit="%" fieldMin="hum_min" fieldMax="hum_max" />
                <InputGroup label="Light" unit="lux" fieldMin="light_min" fieldMax="light_max" />

                <Text className="text-lg font-bold text-gray-800 mb-4 mt-2">Settings</Text>
                
                <View className="flex-row gap-4 mb-8">
                     <View className="flex-1">
                         <Text className="text-sm font-bold text-gray-700 mb-1">Watering (sec)</Text>
                         <TextInput 
                            className="bg-gray-50 p-4 rounded-xl border border-gray-200"
                            keyboardType="numeric"
                            value={form.watering_duration_sec}
                            onChangeText={t => setForm({...form, watering_duration_sec: t})}
                         />
                     </View>
                     <View className="flex-1">
                         <Text className="text-sm font-bold text-gray-700 mb-1">Interval (sec)</Text>
                         <TextInput 
                            className="bg-gray-50 p-4 rounded-xl border border-gray-200"
                            keyboardType="numeric"
                            value={form.measurement_interval_sec}
                            onChangeText={t => setForm({...form, measurement_interval_sec: t})}
                         />
                     </View>
                </View>

                <TouchableOpacity 
                    className="bg-green-600 p-4 rounded-xl mb-10"
                    onPress={handleSubmit}
                >
                    <Text className="text-white text-center font-bold text-lg">Create Profile</Text>
                </TouchableOpacity>

            </ScrollView>
        </SafeAreaView>
    );
}
