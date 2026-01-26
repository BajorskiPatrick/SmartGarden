import axios from 'axios';
import AsyncStorage from '@react-native-async-storage/async-storage';

// Use your LAN IP for physical device testing
export const api = axios.create({
    baseURL: 'http://192.168.29.4:8080/api',
    headers: {
        'Content-Type': 'application/json',
    },
});

api.interceptors.request.use(async (config) => {
    const token = await AsyncStorage.getItem('token');
    if (token) {
        config.headers.Authorization = `Bearer ${token}`;
    }
    console.log(`[API] Request: ${config.method?.toUpperCase()} ${config.url}`, config.data);
    return config;
});

api.interceptors.response.use(
    response => {
        console.log(`[API] Response: ${response.status}`, response.data);
        return response;
    },
    error => {
        console.error(`[API] Error: ${error.response?.status} ${error.message}`, error.response?.data);
        return Promise.reject(error);
    }
);
