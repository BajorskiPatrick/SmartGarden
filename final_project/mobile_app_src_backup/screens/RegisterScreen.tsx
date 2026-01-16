import React, { useState } from 'react';
import { View, Text, TextInput, TouchableOpacity, Alert, SafeAreaView } from 'react-native';
import { api } from '../lib/axios';

export default function RegisterScreen({ navigation }: any) {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [loading, setLoading] = useState(false);

  const handleRegister = async () => {
    if (!username || !password) return Alert.alert("Error", "Please fill all fields");
    setLoading(true);
    try {
      await api.post('/auth/register', { username, password });
      Alert.alert("Success", "Account created! Please login.");
      navigation.goBack();
    } catch (e: any) {
      Alert.alert("Registration Failed", e.response?.data?.message || "Connection error");
    } finally {
      setLoading(false);
    }
  };

  return (
    <SafeAreaView className="flex-1 bg-white justify-center px-6">
      <Text className="text-2xl font-bold text-green-700 mb-6 text-center">Create Account</Text>
      
      <View className="space-y-4">
        <View>
          <Text className="text-gray-700 mb-1">Username</Text>
          <TextInput 
            className="w-full bg-gray-100 p-4 rounded-xl border border-gray-200"
            placeholder="Choose a username"
            value={username}
            onChangeText={setUsername}
            autoCapitalize="none"
          />
        </View>
        
        <View>
          <Text className="text-gray-700 mb-1">Password</Text>
          <TextInput 
            className="w-full bg-gray-100 p-4 rounded-xl border border-gray-200"
            placeholder="Choose a password"
            value={password}
            onChangeText={setPassword}
            secureTextEntry
          />
        </View>

        <TouchableOpacity 
          className={`bg-green-600 p-4 rounded-xl mt-4 ${loading ? 'opacity-70' : ''}`}
          onPress={handleRegister}
          disabled={loading}
        >
          <Text className="text-white text-center font-bold text-lg">
            {loading ? 'Creating...' : 'Register'}
          </Text>
        </TouchableOpacity>

        <TouchableOpacity onPress={() => navigation.goBack()} className="mt-4">
          <Text className="text-gray-500 text-center">Already have an account? <Text className="text-green-600 font-bold">Login</Text></Text>
        </TouchableOpacity>
      </View>
    </SafeAreaView>
  );
}
