import axios from 'axios';
import AsyncStorage from '@react-native-async-storage/async-storage';

// Use 10.0.2.2 for Android Emulator to access host machine's localhost
// For physical device, replace with your local IP, e.g., 'http://192.168.1.X:8080/api'
export const api = axios.create({
    baseURL: 'http://10.0.2.2:8080/api',
    headers: {
        'Content-Type': 'application/json',
    },
});

api.interceptors.request.use(async (config) => {
    const token = await AsyncStorage.getItem('token');
    if (token) {
        config.headers.Authorization = `Bearer ${token}`;
    }
    return config;
});
