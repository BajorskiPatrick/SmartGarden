import React, { useState } from 'react';
import { View, Text, TouchableOpacity, ActivityIndicator, Dimensions } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { useRoute, useNavigation } from '@react-navigation/native';
import { LineChart } from 'react-native-chart-kit';
import { useQuery } from '@tanstack/react-query';
import { ArrowLeft } from 'lucide-react-native';
import { api } from '../lib/axios';

const screenWidth = Dimensions.get("window").width;

export default function TelemetryChartScreen() {
    const navigation = useNavigation();
    const route = useRoute();
    const { macAddress, sensorType, sensorLabel, unit, color = "rgba(46, 204, 113, 1)" } = route.params as any;

    const [timeRange, setTimeRange] = useState<'hour' | 'day' | 'week'>('hour');

    const { data: telemetry, isLoading, refetch } = useQuery({
        queryKey: ['telemetry', macAddress, timeRange],
        queryFn: async () => {
            // Using size=50 to get enough points for a line chart
            const res = await api.get(`/devices/${macAddress}/telemetry?size=50`);
            return res.data.content || [];
        },
        refetchInterval: 10000,
    });

    const processData = () => {
        if (!telemetry || telemetry.length === 0) return { labels: ["Now"], datasets: [{ data: [0] }] };

        // Reverse to show oldest to newest
        const sorted = [...telemetry].reverse();
        
        // Extract data points
        const dataPoints = sorted.map((t: any) => {
            const val = t[sensorType];
            return val !== null && val !== undefined ? val : 0;
        });

        // Create sparse labels to avoid clutter
        const labels = sorted.map((t: any, index: number) => {
            if (index % 10 === 0) {
              const date = new Date(t.timestamp);
              return `${date.getHours()}:${date.getMinutes().toString().padStart(2, '0')}`;
            }
            return "";
        });

        return {
            labels,
            datasets: [
                {
                    data: dataPoints,
                    color: (opacity = 1) => color.replace('1)', `${opacity})`), 
                    strokeWidth: 2
                }
            ],
            legend: [sensorLabel]
        };
    };

    const chartConfig = {
      backgroundGradientFrom: "#ffffff",
      backgroundGradientTo: "#ffffff",
      color: (opacity = 1) => `rgba(0, 0, 0, ${opacity})`,
      strokeWidth: 2, 
      barPercentage: 0.5,
      useShadowColorFromDataset: false 
    };

    return (
        <SafeAreaView className="flex-1 bg-white">
            <View className="px-6 py-4 border-b border-gray-100 flex-row items-center">
                <TouchableOpacity onPress={() => navigation.goBack()} className="mr-4">
                    <ArrowLeft size={24} color="#374151" />
                </TouchableOpacity>
                <View>
                    <Text className="text-xl font-bold text-gray-800">{sensorLabel} History</Text>
                    <Text className="text-xs text-gray-500">Last 50 measurements</Text>
                </View>
            </View>

            <View className="items-center justify-center mt-10">
                {isLoading ? (
                    <ActivityIndicator size="large" color="#16a34a" />
                ) : (
                    <View>
                        <LineChart
                            data={processData()}
                            width={screenWidth - 32}
                            height={250}
                            chartConfig={chartConfig}
                            bezier
                            style={{
                                marginVertical: 8,
                                borderRadius: 16
                            }}
                            withDots={false}
                            withInnerLines={false}
                            yAxisSuffix={unit}
                        />
                         <Text className="text-center text-xs text-gray-400 mt-2">Time (Oldest → Newest)</Text>
                    </View>
                )}
            </View>
        </SafeAreaView>
    );
}
