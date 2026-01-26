import React from 'react';
import { View, Text, FlatList, TouchableOpacity, Alert, RefreshControl } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useNavigation } from '@react-navigation/native';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { Plus, Trash2, Sprout } from 'lucide-react-native';
import { api } from '../lib/axios';
import { PlantProfile } from '../types';

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

    return (
        <SafeAreaView className="flex-1 bg-gray-50">
            <View className="px-6 py-4 flex-row justify-between items-center bg-white border-b border-gray-100">
                <Text className="text-2xl font-bold text-gray-800">Plant Profiles</Text>
                <TouchableOpacity onPress={() => navigation.navigate('CreateProfile' as never)}>
                    <Plus size={24} color="#16a34a" />
                </TouchableOpacity>
            </View>

            <FlatList
                data={profiles}
                keyExtractor={item => item.id || Math.random().toString()}
                contentContainerStyle={{ padding: 20 }}
                refreshControl={<RefreshControl refreshing={isLoading} onRefresh={refetch} />}
                ListEmptyComponent={<Text className="text-center text-gray-400 mt-10">No profiles found. Create one!</Text>}
                renderItem={({ item }) => (
                    <View className="bg-white p-4 rounded-xl mb-3 shadow-sm border border-gray-100">
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
                                    <View className="bg-yellow-50 px-2 py-1 rounded">
                                        <Text className="text-xs text-yellow-700">☀ {item.light_min}-{item.light_max}lx</Text>
                                    </View>
                                </View>
                            </View>
                            <TouchableOpacity onPress={() => item.id && handleDelete(item.id)} className="p-2">
                                <Trash2 size={20} color="#ef4444" />
                            </TouchableOpacity>
                        </View>
                    </View>
                )}
            />
        </SafeAreaView>
    );
}
