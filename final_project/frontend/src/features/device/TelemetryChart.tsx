import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from 'recharts';
import type { Measurement } from '../../types';
import { format } from 'date-fns';
import { Droplets, Thermometer, Sun, AlertTriangle, CheckCircle } from 'lucide-react';

interface TelemetryChartProps {
  data: Measurement[];
}

interface ChartCardProps {
  title: string;
  icon: React.ReactNode;
  color: string;
  dataKey: keyof Measurement;
  unit: string;
  data: Array<any>;
  domain?: [number | 'auto', number | 'auto'];
}

function ChartCard({ title, icon, color, dataKey, unit, data, domain }: ChartCardProps) {
  // Data is sorted Oldest -> Newest. Latest value is the last one.
  const latestValue = data.length > 0 ? data[data.length - 1][dataKey] : null;

  return (
    <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-4">
      <div className="flex items-center justify-between mb-3">
        <div className="flex items-center gap-2">
          <div className={`p-2 rounded-lg`} style={{ backgroundColor: `${color}20` }}>
            {icon}
          </div>
          <h3 className="font-semibold text-gray-900 dark:text-white">{title}</h3>
        </div>
        <div className="text-right">
          <span className="text-2xl font-bold" style={{ color }}>
            {latestValue != null ? Number(latestValue).toFixed(1) : '--'}
          </span>
          <span className="text-gray-500 dark:text-gray-400 text-sm ml-1">{unit}</span>
        </div>
      </div>
      <div className="h-[200px]">
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={data}>
            <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.1} />
            <XAxis
              dataKey="timestampNum"
              type="number"
              domain={['dataMin', 'dataMax']}
              scale="time"
              stroke="#6B7280"
              fontSize={10}
              tickLine={false}
              axisLine={false}
              tickFormatter={(unixTime) => format(new Date(unixTime), 'HH:mm')}
            />
            <YAxis
              stroke="#6B7280"
              fontSize={10}
              tickLine={false}
              axisLine={false}
              domain={domain || ['auto', 'auto']}
              width={40}
            />
            <Tooltip
              contentStyle={{
                backgroundColor: '#1F2937',
                border: 'none',
                borderRadius: '8px',
                color: '#F3F4F6',
              }}
              labelStyle={{ color: '#9CA3AF' }}
              labelFormatter={(unixTime) => format(new Date(unixTime), 'HH:mm:ss')}
              formatter={(value: number | undefined) => [`${value != null ? value.toFixed(1) : '--'} ${unit}`, title]}
            />
            <Line
              type="monotone"
              dataKey={dataKey}
              stroke={color}
              strokeWidth={2}
              dot={{ r: 3, fill: color, strokeWidth: 0 }}
              activeDot={{ r: 5, fill: color }}
              animationDuration={500}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}

function WaterTankStatus({ data }: { data: Measurement[] }) {
  // data is passed sorted Oldest -> Newest (from formattedData)
  // So latest measurement is at the END.
  const latestMeasurement = data.length > 0 ? data[data.length - 1] : null;
  const isOk = latestMeasurement?.waterTankOk;

  // null = unknown, true = OK, false = needs refill
  const status = isOk === null || isOk === undefined
    ? 'unknown'
    : isOk
    ? 'ok'
    : 'warning';

  const statusConfig = {
    unknown: {
      icon: <Droplets className="w-5 h-5 text-gray-400" />,
      text: 'Unknown',
      bgColor: 'bg-gray-100 dark:bg-gray-700',
      textColor: 'text-gray-500 dark:text-gray-400',
    },
    ok: {
      icon: <CheckCircle className="w-5 h-5 text-green-500" />,
      text: 'Water Level OK',
      bgColor: 'bg-green-50 dark:bg-green-900/20',
      textColor: 'text-green-700 dark:text-green-400',
    },
    warning: {
      icon: <AlertTriangle className="w-5 h-5 text-red-500" />,
      text: 'Refill Water Tank!',
      bgColor: 'bg-red-50 dark:bg-red-900/20',
      textColor: 'text-red-700 dark:text-red-400',
    },
  };

  const config = statusConfig[status];

  return (
    <div className={`${config.bgColor} rounded-xl p-4 flex items-center gap-3`}>
      {config.icon}
      <div>
        <p className={`font-semibold ${config.textColor}`}>{config.text}</p>
        <p className="text-xs text-gray-500 dark:text-gray-400">
          {latestMeasurement?.timestamp
            ? `Last updated: ${format(new Date(latestMeasurement.timestamp), 'HH:mm:ss')}`
            : 'No data'}
        </p>
      </div>
    </div>
  );
}

export function TelemetryChart({ data }: TelemetryChartProps) {
  // Input data is likely Newest -> Oldest (DESC from API).
  // We need to reverse it to be Oldest -> Newest for LineChart to draw left-to-right correctly.
  
  const formattedData = [...data]
    .reverse() // Make it Oldest -> Newest
    .map((item) => ({
      ...item,
      timestampNum: new Date(item.timestamp).getTime(),
    }));

  return (
    <div className="space-y-4">
      {/* Water Tank Status */}
      <WaterTankStatus data={formattedData} />

      {/* Charts Grid */}
      <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-3">
        {/* Soil Moisture Chart */}
        <ChartCard
          title="Soil Moisture"
          icon={<Droplets className="w-4 h-4 text-blue-500" />}
          color="#3B82F6"
          dataKey="soilMoisture"
          unit="%"
          data={formattedData}
          domain={[0, 100]}
        />

        {/* Temperature Chart */}
        <ChartCard
          title="Temperature"
          icon={<Thermometer className="w-4 h-4 text-orange-500" />}
          color="#F97316"
          dataKey="temperature"
          unit="°C"
          data={formattedData}
        />

        {/* Light Chart */}
        <ChartCard
          title="Light Intensity"
          icon={<Sun className="w-4 h-4 text-yellow-500" />}
          color="#EAB308"
          dataKey="lightLux"
          unit="lux"
          data={formattedData}
        />
      </div>

      {/* Additional metrics row */}
      <div className="grid gap-4 md:grid-cols-2">
        {/* Humidity Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <div className="p-2 rounded-lg bg-cyan-100 dark:bg-cyan-900/30">
                <Droplets className="w-4 h-4 text-cyan-500" />
              </div>
              <div>
                <h3 className="font-semibold text-gray-900 dark:text-white">Air Humidity</h3>
                <p className="text-xs text-gray-500">Current reading</p>
              </div>
            </div>
            <div className="text-right">
              <span className="text-2xl font-bold text-cyan-500">
                {formattedData.length > 0 && formattedData[formattedData.length - 1].humidity != null
                  ? Number(formattedData[formattedData.length - 1].humidity).toFixed(1)
                  : '--'}
              </span>
              <span className="text-gray-500 dark:text-gray-400 text-sm ml-1">%</span>
            </div>
          </div>
        </div>

        {/* Pressure Card */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700 p-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <div className="p-2 rounded-lg bg-purple-100 dark:bg-purple-900/30">
                <svg className="w-4 h-4 text-purple-500" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
                </svg>
              </div>
              <div>
                <h3 className="font-semibold text-gray-900 dark:text-white">Pressure</h3>
                <p className="text-xs text-gray-500">Atmospheric</p>
              </div>
            </div>
            <div className="text-right">
              <span className="text-2xl font-bold text-purple-500">
                {formattedData.length > 0 && formattedData[formattedData.length - 1].pressure != null
                  ? Number(formattedData[formattedData.length - 1].pressure).toFixed(0)
                  : '--'}
              </span>
              <span className="text-gray-500 dark:text-gray-400 text-sm ml-1">hPa</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
