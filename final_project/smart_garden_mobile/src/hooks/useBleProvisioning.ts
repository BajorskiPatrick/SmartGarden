import { useState, useRef, useEffect } from 'react';
import { BleManager, Device, Characteristic } from 'react-native-ble-plx';
import { PermissionsAndroid, Platform } from 'react-native';
import { Buffer } from 'buffer';

const SERVICE_UUID = '12345678-90ab-cdef-1234-567890abcdef';
// 16-bit UUIDs are normalized to Base UUID in Web Bluetooth: 0000xxxx-0000-1000-8000-00805f9b34fb
const BASE_UUID_PREFIX = '0000';
const BASE_UUID_SUFFIX = '-0000-1000-8000-00805f9b34fb';

const UUIDS = {
    SSID: `${BASE_UUID_PREFIX}ff01${BASE_UUID_SUFFIX}`,
    PASS: `${BASE_UUID_PREFIX}ff02${BASE_UUID_SUFFIX}`,
    CTRL: `${BASE_UUID_PREFIX}ff03${BASE_UUID_SUFFIX}`,
    BROKER: `${BASE_UUID_PREFIX}ff04${BASE_UUID_SUFFIX}`,
    MQTT_LOGIN: `${BASE_UUID_PREFIX}ff05${BASE_UUID_SUFFIX}`,
    MQTT_PASS: `${BASE_UUID_PREFIX}ff06${BASE_UUID_SUFFIX}`,
    USER_ID: `${BASE_UUID_PREFIX}ff07${BASE_UUID_SUFFIX}`,
    DEVICE_ID_MAC: `${BASE_UUID_PREFIX}ff08${BASE_UUID_SUFFIX}`,
    AUTH: `${BASE_UUID_PREFIX}ff09${BASE_UUID_SUFFIX}`,
};

const toBase64 = (str: string) => Buffer.from(str).toString('base64');
const fromBase64 = (str: string) => Buffer.from(str, 'base64').toString();

export interface ProvisioningData {
    ssid: string;
    pass: string;
    mqtt_login: string;
    mqtt_pass: string;
    user_id: string;
    broker_url: string;
}

export const useBleProvisioning = () => {
    const managerRef = useRef<BleManager>(new BleManager());
    const [device, setDevice] = useState<Device | null>(null);
    const [isScanning, setIsScanning] = useState(false);
    const [status, setStatus] = useState<string>('idle'); // idle, scanning, connecting, provisioning, success, error
    const [error, setError] = useState<string | null>(null);
    const [serviceUUID, setServiceUUID] = useState<string>(SERVICE_UUID);

    useEffect(() => {
        const subscription = managerRef.current.onStateChange((state) => {
            if (state === 'PoweredOn') {
                // Ready
            }
        }, true);
        return () => {
            subscription.remove();
            managerRef.current.destroy();
        };
    }, []);

    const requestPermissions = async () => {
        if (Platform.OS === 'android') {
            if (Platform.Version >= 31) {
                const granted = await PermissionsAndroid.requestMultiple([
                    PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
                    PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
                    PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
                ]);
                return (
                    granted['android.permission.BLUETOOTH_SCAN'] === PermissionsAndroid.RESULTS.GRANTED &&
                    granted['android.permission.BLUETOOTH_CONNECT'] === PermissionsAndroid.RESULTS.GRANTED
                );
            } else {
                const granted = await PermissionsAndroid.request(
                    PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION
                );
                return granted === PermissionsAndroid.RESULTS.GRANTED;
            }
        }
        return true;
    };

    const [scannedDevices, setScannedDevices] = useState<Device[]>([]);

    const startScan = async () => {
        setError(null);
        setStatus('scanning');
        setScannedDevices([]);

        // 1. Check/Request Permissions
        const hasPerms = await requestPermissions();
        if (!hasPerms) {
            setError("Bluetooth permissions denied. Please enable them in settings.");
            setStatus('error');
            return;
        }

        // 2. Enable Bluetooth (Android)
        if (Platform.OS === 'android') {
            try {
                const state = await managerRef.current.state();
                if (state !== 'PoweredOn') {
                    await managerRef.current.enable();
                }
            } catch (e) {
                console.warn("Failed to enable Bluetooth programmatically", e);
            }
        }

        // 3. Start Scan 
        managerRef.current.startDeviceScan(null, null, (err, scannedDevice) => {
            if (err) {
                if (err.errorCode === 601) {
                    setError("Location services are disabled. Please enable GPS.");
                    return;
                }
                console.log("Scan note:", err.message);
                return;
            }

            if (scannedDevice && scannedDevice.name) {
                // Filter duplicates and only show relevant devices
                setScannedDevices(prev => {
                    const exists = prev.find(d => d.id === scannedDevice.id);
                    if (!exists) {
                        // Filter by name prefix if desired, or show all with names
                        if (scannedDevice.name?.includes('SMART_GARDEN') || scannedDevice.name?.includes('ESP32')) {
                            return [...prev, scannedDevice];
                        }
                    }
                    return prev;
                });
            }
        });
        setIsScanning(true);
    };

    const stopScan = () => {
        managerRef.current.stopDeviceScan();
        setIsScanning(false);
    };

    const connectToDevice = async (selectedDevice: Device) => {
        stopScan();
        setStatus('connecting');
        try {
            const connectedDevice = await selectedDevice.connect();
            await connectedDevice.discoverAllServicesAndCharacteristics();

            // Request higher MTU for Android 
            if (Platform.OS === 'android') {
                try {
                    await connectedDevice.requestMTU(512);
                } catch (mtuError) {
                    console.warn("MTU Request failed", mtuError);
                }
            }

            // Dynamically find Service UUID
            const services = await connectedDevice.services();
            const SERVICE_UUID_REV = 'efcdab90-7856-3412-efcd-ab9078563412';
            const foundService = services.find(s => s.uuid === SERVICE_UUID || s.uuid === SERVICE_UUID_REV);

            if (foundService) {
                setServiceUUID(foundService.uuid);
            }

            setDevice(connectedDevice);
            setStatus('connected');
        } catch (e: any) {
            setError("Connection failed: " + e.message);
            setStatus('error');
        }
    };

    const waitForAuth = async (): Promise<boolean> => {
        if (!device) throw new Error("Device not connected");

        setStatus('awaiting_auth');
        console.log("Waiting for user authorization (Press BOOT button)...");

        return new Promise((resolve, reject) => {
            let isFinished = false;

            const subscription = device.monitorCharacteristicForService(
                serviceUUID,
                UUIDS.AUTH,
                (err, characteristic) => {
                    // If we are already done/resolved, ignore any subsequent callbacks (zombies)
                    if (isFinished) return;

                    if (err) {
                        console.log("Auth monitor reported error:", err.message);
                        // If error, we fail
                        isFinished = true;
                        subscription.remove();
                        reject(err);
                        return;
                    }

                    if (characteristic?.value) {
                        // Decoded value is base64
                        const bytes = Buffer.from(characteristic.value, 'base64');
                        const authorized = bytes[0] === 1;
                        console.log("Auth Notification:", authorized);

                        if (authorized) {
                            console.log("Authorized! Resolving...");
                            isFinished = true;
                            resolve(true);

                            // Delay removal to prevent native race condition/crash
                            setTimeout(() => {
                                console.log("Cleaning up auth subscription...");
                                try {
                                    subscription.remove();
                                } catch (e) {
                                    console.log("Ignored cleanup error:", e);
                                }
                            }, 2000);
                        }
                    }
                }
            );
        });
    };

    const provisionDevice = async (data: ProvisioningData) => {
        if (!device) return;
        setStatus('provisioning');
        try {
            console.log("Writing Provisioning Data...", data);

            const write = async (uuid: string, val: string, label: string) => {
                console.log(`Writing ${label} (${val.length} chars)...`);
                await device.writeCharacteristicWithResponseForService(serviceUUID, uuid, toBase64(val));
                console.log(`✓ ${label} Written`);
            };

            await write(UUIDS.SSID, data.ssid, "SSID");
            await write(UUIDS.PASS, data.pass, "Password");
            await write(UUIDS.BROKER, data.broker_url, "Broker URL");
            await write(UUIDS.MQTT_LOGIN, data.mqtt_login, "MQTT Login");
            await write(UUIDS.MQTT_PASS, data.mqtt_pass, "MQTT Password");
            await write(UUIDS.USER_ID, data.user_id, "User ID");

            // Allow some time for device to process
            await new Promise(r => setTimeout(r, 500));

            // Write 0x01 to trigger save & reboot
            // Sending raw bytes for control char
            console.log("Sending Reboot Command...");
            await device.writeCharacteristicWithResponseForService(serviceUUID, UUIDS.CTRL, toBase64("\x01"));
            console.log("✓ Reboot Command Sent");

            setStatus('success');
        } catch (e: any) {
            console.error("Provisioning Error", e);
            setError(e.message);
            setStatus('error');
        }
    };

    return { startScan, stopScan, connectToDevice, scannedDevices, provisionDevice, waitForAuth, device, status, error, isScanning };
};
