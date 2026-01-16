import React, { useState, useEffect } from 'react';
import { View, Text, TextInput, TouchableOpacity, ScrollView, Alert, ActivityIndicator } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useBleProvisioning } from '../hooks/useBleProvisioning';
import { useAuth } from '../context/AuthContext';
import { Bluetooth, Check, X } from 'lucide-react-native';
import { api } from '../lib/axios';

export default function ProvisionScreen({ navigation }: any) {
  const { username } = useAuth();
  const { scanAndConnect, provisionDevice, waitForAuth, device, status, error, isScanning } = useBleProvisioning();
  
  const [ssid, setSsid] = useState('');
  const [password, setPassword] = useState('');
  const [loadingCreds, setLoadingCreds] = useState(false);

  // Auto-fill username if available, needed for provisioning
  useEffect(() => {
    if (status === 'connected') {
        // Ready for input
    }
  }, [status]);

  const handleStartScan = () => {
    scanAndConnect();
  };

  // BLE MAC is usually +2 from WiFi Station MAC on ESP32
  // We need to register the WiFi MAC on the backend so topics match
  const getWifiMacFromBleMac = (bleMac: string) => {
      try {
        const parts = bleMac.split(':');
        if (parts.length !== 6) return bleMac;
        
        const lastByte = parseInt(parts[5], 16);
        const wifiLastByte = lastByte - 2;
        
        parts[5] = wifiLastByte.toString(16).toUpperCase().padStart(2, '0');
        return parts.join(':');
      } catch (e) {
          return bleMac;
      }
  };

  const handleProvision = async () => {
    if (!ssid || !password) {
        Alert.alert("Missing Info", "Please enter WiFi SSID and Password");
        return;
    }
    if (!device) return;

    setLoadingCreds(true);
    try {
         // Use the calculated WiFi MAC for registration 
         const bleMac = device.id; 
         const wifiMac = getWifiMacFromBleMac(bleMac);
         console.log(`Fetching credentials for WiFi MAC: ${wifiMac} (BLE: ${bleMac})`);

         const response = await api.post(`/provisioning/device?mac=${wifiMac}`);
         const creds = response.data;
         
         // 2. Wait for Authorization
         await waitForAuth();

         // 3. Write Config
         await provisionDevice({
             ssid,
             pass: password,
             mqtt_login: creds.mqtt_login,
             mqtt_pass: creds.mqtt_password,
             user_id: creds.user_id,
             broker_url: creds.broker_url
         });

    } catch (e: any) {
        console.error("Provisioning Prep Error:", e);
        Alert.alert("Error", "Failed to prepare provisioning: " + (e.response?.data?.message || e.message));
    } finally {
        setLoadingCreds(false);
    }
  };

  return (
    <SafeAreaView className="flex-1 bg-white">
      <ScrollView className="p-6">
        <Text className="text-2xl font-bold text-gray-800 mb-6">Device Setup</Text>

        {/* Step 1: Scan & Connect */}
        <View className="mb-8">
            <Text className="text-lg font-semibold text-gray-700 mb-2">1. Connect to Device</Text>
            {device ? (
                <View className="bg-green-50 p-4 rounded-xl border border-green-200 flex-row items-center">
                    <Check size={20} color="green" />
                    <Text className="ml-2 text-green-700 font-bold">Connected to {device.name}</Text>
                </View>
            ) : (
                <View>
                    <Text className="text-gray-500 mb-4">Ensure your SmartGarden device is defined in specific provision mode (Blue LED blinking).</Text>
                    <TouchableOpacity 
                        className={`bg-blue-600 p-4 rounded-xl flex-row justify-center items-center ${isScanning ? 'opacity-50' : ''}`}
                        onPress={handleStartScan}
                        disabled={isScanning}
                    >
                        {isScanning ? <ActivityIndicator color="white" className="mr-2" /> : <Bluetooth color="white" className="mr-2" />}
                        <Text className="text-white font-bold">{isScanning ? 'Scanning...' : 'Start BLE Scan'}</Text>
                    </TouchableOpacity>
                    {error && <Text className="text-red-500 mt-2">{error}</Text>}
                </View>
            )}
        </View>

        {/* Step 2: Configure WiFi */}
        {device && (
            <View>
                <Text className="text-lg font-semibold text-gray-700 mb-2">2. Configure WiFi</Text>
                <View className="space-y-4">
                    <TextInput 
                        className="bg-gray-100 p-4 rounded-xl"
                        placeholder="WiFi SSID"
                        value={ssid}
                        onChangeText={setSsid}
                    />
                    <TextInput 
                        className="bg-gray-100 p-4 rounded-xl"
                        placeholder="WiFi Password"
                        value={password}
                        onChangeText={setPassword}
                        secureTextEntry
                    />
                    
                    <TouchableOpacity 
                        className={`bg-green-600 p-4 rounded-xl mt-2 ${status === 'provisioning' || status === 'awaiting_auth' || loadingCreds ? 'opacity-50' : ''}`}
                        onPress={handleProvision}
                        disabled={status === 'provisioning' || status === 'awaiting_auth' || loadingCreds}
                    >
                        {status === 'provisioning' || loadingCreds ? (
                            <ActivityIndicator color="white" />
                        ) : status === 'awaiting_auth' ? (
                            <Text className="text-white text-center font-bold">Press BOOT Button on Device...</Text>
                        ) : (
                            <Text className="text-white text-center font-bold">Send Configuration</Text>
                        )}
                    </TouchableOpacity>
                    
                    {status === 'success' && (
                         <View className="mt-4 bg-green-100 p-4 rounded-xl">
                            <Text className="text-green-800 text-center font-bold">Success! Device is rebooting.</Text>
                            <TouchableOpacity onPress={() => navigation.goBack()} className="mt-2">
                                <Text className="text-green-800 text-center underline">Return to Dashboard</Text>
                            </TouchableOpacity>
                         </View>
                    )}
                     {status === 'error' && (
                         <View className="mt-4 bg-red-100 p-4 rounded-xl">
                            <Text className="text-red-800 text-center font-bold">Error: {error}</Text>
                         </View>
                    )}
                </View>
            </View>
        )}
      </ScrollView>
    </SafeAreaView>
  );
}
