import "./global.css";
import React from 'react';
import { NavigationContainer } from '@react-navigation/native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { Home, Sprout } from 'lucide-react-native';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { AuthProvider, useAuth } from './src/context/AuthContext';
import { View, ActivityIndicator } from 'react-native';

// Screens
import LoginScreen from './src/screens/LoginScreen';
import RegisterScreen from './src/screens/RegisterScreen';
import DashboardScreen from './src/screens/DashboardScreen';
import ProvisionScreen from './src/screens/ProvisionScreen';
import DeviceDetailsScreen from './src/screens/DeviceDetailsScreen';
import ProfilesScreen from './src/screens/ProfilesScreen';
import CreateProfileScreen from './src/screens/CreateProfileScreen';

const Stack = createNativeStackNavigator();
const AuthStack = createNativeStackNavigator();
const AppStack = createNativeStackNavigator();
const Tab = createBottomTabNavigator();

const queryClient = new QueryClient();

function AuthNavigator() {
  return (
    <AuthStack.Navigator screenOptions={{ headerShown: false }}>
      <AuthStack.Screen name="Login" component={LoginScreen} />
      <AuthStack.Screen name="Register" component={RegisterScreen} />
    </AuthStack.Navigator>
  );
}

function TabNavigator() {
  return (
    <Tab.Navigator
      screenOptions={{
        headerShown: false,
        tabBarActiveTintColor: '#16a34a',
        tabBarInactiveTintColor: 'gray',
      }}
    >
      <Tab.Screen 
        name="DashboardTab" 
        component={DashboardScreen} 
        options={{
          tabBarLabel: 'Home',
          tabBarIcon: ({ color }) => <Home size={24} color={color} />
        }}
      />
      <Tab.Screen 
        name="ProfilesTab" 
        component={ProfilesScreen} 
        options={{
          tabBarLabel: 'Profiles',
          tabBarIcon: ({ color }) => <Sprout size={24} color={color} />
        }}
      />
    </Tab.Navigator>
  );
}

function AppNavigator() {
  return (
    <AppStack.Navigator screenOptions={{ headerShown: false }}>
      <AppStack.Screen name="Main" component={TabNavigator} />
      <AppStack.Screen name="DeviceDetails" component={DeviceDetailsScreen} />
      <AppStack.Screen name="Provision" component={ProvisionScreen} />
      <AppStack.Screen name="CreateProfile" component={CreateProfileScreen} />
    </AppStack.Navigator>
  );
}

function Navigation() {
  const { isAuthenticated, isLoading } = useAuth();
  
  if (isLoading) {
      return (
          <View className="flex-1 justify-center items-center">
              <ActivityIndicator size="large" color="#16a34a" />
          </View>
      );
  }

  return (
      <NavigationContainer>
          {isAuthenticated ? <AppNavigator /> : <AuthNavigator />}
      </NavigationContainer>
  );
}

import { SafeAreaProvider } from 'react-native-safe-area-context';

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <AuthProvider>
        <SafeAreaProvider>
          <Navigation />
        </SafeAreaProvider>
      </AuthProvider>
    </QueryClientProvider>
  );
}
