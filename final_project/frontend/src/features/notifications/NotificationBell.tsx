import { useState, useEffect } from 'react';
import { Bell } from 'lucide-react';
import { Client } from '@stomp/stompjs';
import { useAuth } from '../../context/AuthContext';
import type { Alert } from '../../types';
import { format } from 'date-fns';

export function NotificationBell() {
  const { username } = useAuth();
  const [alerts, setAlerts] = useState<Alert[]>([]);
  const [isOpen, setIsOpen] = useState(false);
  const [unreadCount, setUnreadCount] = useState(0);

  useEffect(() => {
    if (!username) return;

    const hostname = window.location.hostname;
    const brokerURL = `ws://${hostname}:8080/ws`;

    const client = new Client({
      brokerURL,
      onConnect: () => {
        // Subscribe to user-specific alerts
        client.subscribe(`/topic/user/${username}/alerts`, (message) => {
          const newAlert = JSON.parse(message.body) as Alert;
          setAlerts((prev) => [newAlert, ...prev]);
          setUnreadCount((prev) => prev + 1);
        });
      },
    });

    client.activate();

    return () => {
      client.deactivate();
    };
  }, [username]);

  const handleOpen = () => {
    setIsOpen(!isOpen);
    if (!isOpen) {
      setUnreadCount(0);
    }
  };

  return (
    <div className="relative">
      <button
        onClick={handleOpen}
        className="p-2 text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200"
      >
        <Bell className="w-6 h-6" />
        {unreadCount > 0 && (
          <span className="absolute top-1 right-1 w-4 h-4 bg-red-500 text-white text-xs font-bold rounded-full flex items-center justify-center">
            {unreadCount > 9 ? '9+' : unreadCount}
          </span>
        )}
      </button>

      {isOpen && (
        <div className="absolute right-0 mt-2 w-80 bg-white dark:bg-gray-800 rounded-lg shadow-xl border border-gray-200 dark:border-gray-700 z-50">
          <div className="p-3 border-b border-gray-200 dark:border-gray-700 font-semibold text-gray-900 dark:text-white">
            Notifications
          </div>
          <div className="max-h-96 overflow-y-auto">
            {alerts.length === 0 ? (
              <div className="p-4 text-center text-gray-500 dark:text-gray-400">
                No new notifications
              </div>
            ) : (
                alerts.map((alert, index) => (
                    <div key={index} className="p-3 border-b border-gray-100 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700/50">
                        <div className="flex justify-between items-start mb-1">
                            <span className={`text-xs font-bold px-2 py-0.5 rounded-full ${
                                alert.severity === 'CRITICAL' ? 'bg-red-100 text-red-800' :
                                alert.severity === 'WARNING' ? 'bg-yellow-100 text-yellow-800' :
                                'bg-blue-100 text-blue-800'
                            }`}>
                                {alert.severity}
                            </span>
                            <span className="text-xs text-gray-500">
                                {alert.timestamp ? format(new Date(alert.timestamp), 'HH:mm') : ''}
                            </span>
                        </div>
                        <p className="text-sm text-gray-800 dark:text-gray-200">{alert.message}</p>
                        <p className="text-xs text-gray-400 mt-1">Device: {alert.deviceMac}</p>
                    </div>
                ))
            )}
          </div>
        </div>
      )}
    </div>
  );
}
