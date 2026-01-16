import React, { createContext, useContext, useState, useEffect } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import AsyncStorage from '@react-native-async-storage/async-storage';

interface AuthContextType {
  token: string | null;
  isAuthenticated: boolean;
  login: (token: string, username: string) => Promise<void>;
  logout: () => Promise<void>;
  username: string | null;
  isLoading: boolean;
}

const AuthContext = createContext<AuthContextType | undefined>(undefined);

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [token, setToken] = useState<string | null>(null);
  const [username, setUsername] = useState<string | null>(null);
  const [isLoading, setIsLoading] = useState(true);
  const queryClient = useQueryClient();

  useEffect(() => {
    const loadStorage = async () => {
      try {
        const storedToken = await AsyncStorage.getItem('token');
        const storedUsername = await AsyncStorage.getItem('username');
        setToken(storedToken);
        setUsername(storedUsername);
      } catch (e) {
        console.error("Failed to load auth storage", e);
      } finally {
        setIsLoading(false);
      }
    };
    loadStorage();
  }, []);

  const login = async (newToken: string, newUsername: string) => {
    setToken(newToken);
    setUsername(newUsername);
    await AsyncStorage.setItem('token', newToken);
    await AsyncStorage.setItem('username', newUsername);
  };

  const logout = async () => {
    setToken(null);
    setUsername(null);
    await AsyncStorage.removeItem('token');
    await AsyncStorage.removeItem('username');
    queryClient.removeQueries();
  };

  return (
    <AuthContext.Provider value={{ token, isAuthenticated: !!token, login, logout, username, isLoading }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth() {
  const context = useContext(AuthContext);
  if (context === undefined) {
    throw new Error('useAuth must be used within an AuthProvider');
  }
  return context;
}
