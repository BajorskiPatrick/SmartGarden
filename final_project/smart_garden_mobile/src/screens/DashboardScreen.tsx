import React from 'react';
import { View, Text, FlatList, TouchableOpacity, RefreshControl } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useAuth } from '../context/AuthContext';
import { useDevices, Device } from '../hooks/useDevices';
import { Plus, Power, Wifi, WifiOff, LogOut } from 'lucide-react-native';

export default function DashboardScreen({ navigation }: any) {
  const { logout, username } = useAuth();
  const { data: devices, isLoading, refetch } = useDevices();

  const renderDevice = ({ item }: { item: Device }) => (
    <TouchableOpacity 
      className="bg-white p-4 mb-3 rounded-xl shadow-sm border border-gray-100 flex-row items-center justify-between"
      onPress={() => console.log("Navigate to details", item.id)}
    >
      <View className="flex-row items-center space-x-4">
        <View className={`w-10 h-10 rounded-full items-center justify-center ${item.status === 'ONLINE' ? 'bg-green-100' : 'bg-gray-100'}`}>
           <Power size={20} color={item.status === 'ONLINE' ? '#16a34a' : '#9ca3af'} />
        </View>
        <View>
          <Text className="font-bold text-gray-800 text-lg">{item.name}</Text>
          <View className="flex-row items-center">
            {item.status === 'ONLINE' ? (
                <Wifi size={14} color="#16a34a" />
            ) : (
                <WifiOff size={14} color="#ef4444" />
            )}
            <Text className={`ml-1 text-xs ${item.status === 'ONLINE' ? 'text-green-600' : 'text-red-500'}`}>
                {item.status}
            </Text>
          </View>
        </View>
      </View>
    </TouchableOpacity>
  );

  return (
    <SafeAreaView className="flex-1 bg-gray-50">
      <View className="px-6 py-4 bg-white flex-row justify-between items-center shadow-sm">
        <View>
           <Text className="text-gray-500 text-xs uppercase">Welcome Back</Text>
           <Text className="text-xl font-bold text-green-800">{username}</Text>
        </View>
        <TouchableOpacity onPress={() => logout()} className="p-2 bg-gray-100 rounded-full">
            <LogOut size={20} color="#374151" />
        </TouchableOpacity>
      </View>

      <FlatList
        data={devices}
        keyExtractor={(item) => item.id.toString()}
        renderItem={renderDevice}
        contentContainerStyle={{ padding: 20 }}
        refreshControl={<RefreshControl refreshing={isLoading} onRefresh={refetch} />}
        ListEmptyComponent={
          <View className="items-center justify-center mt-20">
            <Text className="text-gray-400">No devices found</Text>
          </View>
        }
      />

      <TouchableOpacity 
        className="absolute bottom-8 right-8 bg-green-600 w-14 h-14 rounded-full items-center justify-center shadow-lg"
        onPress={() => navigation.navigate('Provision')}
      >
        <Plus size={30} color="white" />
      </TouchableOpacity>
    </SafeAreaView>
  );
}
