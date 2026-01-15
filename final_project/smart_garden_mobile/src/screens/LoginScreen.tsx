import React, { useState } from 'react';
import { View, Text, TextInput, TouchableOpacity, Alert } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useAuth } from '../context/AuthContext';
import { api } from '../lib/axios';

export default function LoginScreen({ navigation }: any) {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const { login } = useAuth();
  const [loading, setLoading] = useState(false);

  const handleLogin = async () => {
    if (!username || !password) return Alert.alert("Error", "Please fill all fields");
    setLoading(true);
    try {
      const res = await api.post('/auth/login', { username, password });
      // API returns the token string directly as res.data
      await login(res.data, username);
    } catch (error: any) {
      console.error('Login error:', error);
      const errorMessage = typeof error.response?.data === 'string' 
        ? error.response.data 
        : error.response?.data?.message || 'Login failed';
      Alert.alert('Login Failed', errorMessage);
    } finally {
      setLoading(false);
    }
  };

  return (
    <SafeAreaView className="flex-1 bg-white justify-center px-6">
      <Text className="text-3xl font-bold text-green-700 mb-8 text-center">SmartGarden</Text>
      
      <View className="space-y-4">
        <View>
          <Text className="text-gray-700 mb-1">Username</Text>
          <TextInput 
            className="w-full bg-gray-100 p-4 rounded-xl border border-gray-200"
            placeholder="Enter username"
            value={username}
            onChangeText={setUsername}
            autoCapitalize="none"
          />
        </View>
        
        <View>
          <Text className="text-gray-700 mb-1">Password</Text>
          <TextInput 
            className="w-full bg-gray-100 p-4 rounded-xl border border-gray-200"
            placeholder="Enter password"
            value={password}
            onChangeText={setPassword}
            secureTextEntry
          />
        </View>

        <TouchableOpacity 
          className={`bg-green-600 p-4 rounded-xl mt-4 ${loading ? 'opacity-70' : ''}`}
          onPress={handleLogin}
          disabled={loading}
        >
          <Text className="text-white text-center font-bold text-lg">
            {loading ? 'Logging in...' : 'Log In'}
          </Text>
        </TouchableOpacity>

        <TouchableOpacity onPress={() => navigation.navigate('Register')} className="mt-4">
          <Text className="text-gray-500 text-center">Don't have an account? <Text className="text-green-600 font-bold">Register</Text></Text>
        </TouchableOpacity>
      </View>
    </SafeAreaView>
  );
}
