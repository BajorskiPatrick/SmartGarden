import { useState, useRef, useEffect } from 'react';
import { BleManager, Device, Characteristic } from 'react-native-ble-plx';
import { PermissionsAndroid, Platform } from 'react-native';
import { Buffer } from 'buffer';

const SERVICE_UUID = '12345678-90ab-cdef-1234-567890abcdef';
const CHAR_SSID = '0000ff01-0000-1000-8000-00805f9b34fb';
const CHAR_PASS = '0000ff02-0000-1000-8000-00805f9b34fb';
const CHAR_USER = '0000ff03-0000-1000-8000-00805f9b34fb';
const CHAR_STATUS = '0000ff04-0000-1000-8000-00805f9b34fb';

const toBase64 = (str: string) => Buffer.from(str).toString('base64');
const fromBase64 = (str: string) => Buffer.from(str, 'base64').toString();

export const useBleProvisioning = () => {
    const managerRef = useRef<BleManager>(new BleManager());
    const [device, setDevice] = useState<Device | null>(null);
    const [isScanning, setIsScanning] = useState(false);
    const [status, setStatus] = useState<string>('idle'); // idle, scanning, connecting, provisioning, success, error
    const [error, setError] = useState<string | null>(null);

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

    const scanAndConnect = async () => {
        setError(null);
        setStatus('scanning');
        const hasPerms = await requestPermissions();
        if (!hasPerms) {
            setError("Bluetooth permissions denied");
            setStatus('error');
            return;
        }

        managerRef.current.startDeviceScan([SERVICE_UUID], null, async (err, scannedDevice) => {
            if (err) {
                // Ignore innocuous scanning errors if we find the device
                // Only set error if really failed
                console.log("Scan error (might be expected):", err);
            }
            if (scannedDevice && (scannedDevice.name === 'SMART_GARDEN_PROV' || scannedDevice.localName === 'SMART_GARDEN_PROV')) {
                managerRef.current.stopDeviceScan();
                setIsScanning(false);
                setStatus('connecting');
                try {
                    const connectedDevice = await scannedDevice.connect();
                    await connectedDevice.discoverAllServicesAndCharacteristics();
                    setDevice(connectedDevice);
                    setStatus('connected');
                } catch (e: any) {
                    setError(e.message);
                    setStatus('error');
                }
            }
        });
        setIsScanning(true);
    };

    const provisionDevice = async (ssid: string, pass: string, username: string) => {
        if (!device) return;
        setStatus('provisioning');
        try {
            await device.writeCharacteristicWithResponseForService(SERVICE_UUID, CHAR_SSID, toBase64(ssid));
            await device.writeCharacteristicWithResponseForService(SERVICE_UUID, CHAR_PASS, toBase64(pass));
            await device.writeCharacteristicWithResponseForService(SERVICE_UUID, CHAR_USER, toBase64(username));

            // Allow some time for device to process
            await new Promise(r => setTimeout(r, 1000));

            // Poll status
            const statusChar = await device.readCharacteristicForService(SERVICE_UUID, CHAR_STATUS);
            const statusVal = fromBase64(statusChar.value || '');

            // Logic depending on what the firmware returns
            if (statusVal.includes('CONNECTED') || statusVal.includes('PROVISIONED')) {
                setStatus('success');
            } else {
                setStatus('provisioned_waiting');
            }
        } catch (e: any) {
            setError(e.message);
            setStatus('error');
        }
    };

    return { scanAndConnect, provisionDevice, device, status, error, isScanning };
};
