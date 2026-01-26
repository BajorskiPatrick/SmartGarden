import React from 'react';
import { View, Text, FlatList, TouchableOpacity, Alert, RefreshControl } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useNavigation } from '@react-navigation/native';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { Plus, Trash2, Sprout } from 'lucide-react-native';
import { api } from '../lib/axios';
import { PlantProfile } from '../types';
import { PLANT_PROFILES } from '../lib/plantProfiles';

export default function ProfilesScreen() {
    const navigation = useNavigation();
    const queryClient = useQueryClient();

    const { data: profiles, isLoading, refetch } = useQuery<PlantProfile[]>({
        queryKey: ['profiles'],
        queryFn: async () => {
            const res = await api.get('/profiles');
            return res.data;
        }
    });

    const deleteMutation = useMutation({
        mutationFn: async (id: string) => {
            await api.delete(`/profiles/${id}`);
        },
        onSuccess: () => {
            Alert.alert("Success", "Profile deleted");
            queryClient.invalidateQueries({ queryKey: ['profiles'] });
        },
        onError: () => Alert.alert("Error", "Failed to delete profile")
    });

    const handleDelete = (id: string) => {
        Alert.alert("Delete Profile", "Are you sure?", [
            { text: "Cancel" },
            { text: "Delete", style: 'destructive', onPress: () => deleteMutation.mutate(id) }
        ]);
    };

    const [selectedProfile, setSelectedProfile] = React.useState<PlantProfile | null>(null);
    const [isDetailsOpen, setIsDetailsOpen] = React.useState(false);

    const openDetails = (profile: PlantProfile) => {
        setSelectedProfile(profile);
        setIsDetailsOpen(true);
    };

    const DetailItem = ({ label, value }: { label: string, value: string | number }) => (
        <View className="flex-row justify-between py-2 border-b border-gray-100">
            <Text className="text-gray-500">{label}</Text>
            <Text className="text-gray-800 font-bold">{value}</Text>
        </View>
    );

    return (
        <SafeAreaView className="flex-1 bg-gray-50">
            <View className="px-6 py-4 flex-row justify-between items-center bg-white border-b border-gray-100">
                <Text className="text-2xl font-bold text-gray-800">Plant Profiles</Text>
                <TouchableOpacity onPress={() => navigation.navigate('CreateProfile' as never)}>
                    <Plus size={24} color="#16a34a" />
                </TouchableOpacity>
            </View>

            <FlatList
                data={[...(profiles || []), ...PLANT_PROFILES]}
                keyExtractor={item => item.id || Math.random().toString()}
                contentContainerStyle={{ padding: 20 }}
                refreshControl={<RefreshControl refreshing={isLoading} onRefresh={refetch} />}
                ListEmptyComponent={<Text className="text-center text-gray-400 mt-10">No profiles found. Create one!</Text>}
                renderItem={({ item }) => (
                    <TouchableOpacity 
                        className="bg-white p-4 rounded-xl mb-3 shadow-sm border border-gray-100"
                        onPress={() => openDetails(item)}
                    >
                        <View className="flex-row justify-between items-start">
                            <View className="flex-1">
                                <View className="flex-row items-center mb-1">
                                    <Sprout size={18} color="green" className="mr-2" />
                                    <Text className="text-lg font-bold text-gray-800">{item.name}</Text>
                                </View>
                                <Text className="text-gray-500 text-sm mb-2">{item.description}</Text>
                                
                                <View className="flex-row gap-3 flex-wrap">
                                    <View className="bg-red-50 px-2 py-1 rounded">
                                        <Text className="text-xs text-red-700">🌡 {item.temp_min}-{item.temp_max}°C</Text>
                                    </View>
                                    <View className="bg-blue-50 px-2 py-1 rounded">
                                        <Text className="text-xs text-blue-700">💧 {item.soil_min}-{item.soil_max}%</Text>
                                    </View>
                                </View>
                            </View>
                            <TouchableOpacity 
                                onPress={() => item.id && handleDelete(item.id)} 
                                className="p-2"
                                disabled={!item.userId} // Base profiles don't have userId usually, or we can check simple logic
                            >
                                {item.userId ? <Trash2 size={20} color="#ef4444" /> : <View style={{ width: 20 }} />}
                            </TouchableOpacity>
                        </View>
                    </TouchableOpacity>
                )}
            />

            {/* Profile Details Modal */}
            <React.Fragment>
             {selectedProfile && (
                <View style={{ display: isDetailsOpen ? 'flex' : 'none', position: 'absolute', top: 0, left: 0, right: 0, bottom: 0, backgroundColor: 'rgba(0,0,0,0.5)', zIndex: 1000, justifyContent: 'center', alignItems: 'center' }}>
                     <View className="bg-white w-[90%] max-h-[80%] rounded-2xl p-6 shadow-xl">
                        <View className="flex-row justify-between items-center mb-4 border-b border-gray-100 pb-2">
                            <View>
                                <Text className="text-xl font-bold text-gray-800">{selectedProfile.name}</Text>
                                {selectedProfile.description ? <Text className="text-sm text-gray-500">{selectedProfile.description}</Text> : null}
                            </View>
                            <TouchableOpacity onPress={() => setIsDetailsOpen(false)} className="p-2">
                                <Text className="text-gray-400 font-bold text-lg">✕</Text>
                            </TouchableOpacity>
                        </View>
                        
                        <View>
                            <Text className="text-green-700 font-bold mb-2 uppercase text-xs">Environment Targets</Text>
                            <DetailItem label="Temperature" value={`${selectedProfile.temp_min} - ${selectedProfile.temp_max} °C`} />
                            <DetailItem label="Soil Moisture" value={`${selectedProfile.soil_min} - ${selectedProfile.soil_max} %`} />
                            <DetailItem label="Humidity" value={`${selectedProfile.hum_min} - ${selectedProfile.hum_max} %`} />
                            <DetailItem label="Light" value={`${selectedProfile.light_min} - ${selectedProfile.light_max} lux`} />
                            
                            <Text className="text-green-700 font-bold mb-2 uppercase text-xs mt-4">Automation Settings</Text>
                            <DetailItem label="Measurement Interval" value={`${selectedProfile.measurement_interval_sec} sec`} />
                            <DetailItem label="Watering Duration" value={`${selectedProfile.watering_duration_sec} sec`} />
                        </View>

                        <TouchableOpacity 
                            className="mt-6 bg-gray-100 p-3 rounded-xl items-center"
                            onPress={() => setIsDetailsOpen(false)}
                        >
                            <Text className="text-gray-700 font-bold">Close</Text>
                        </TouchableOpacity>
                     </View>
                </View>
             )}
            </React.Fragment>
        </SafeAreaView>
    );
}
