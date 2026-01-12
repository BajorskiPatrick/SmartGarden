import { useState } from 'react';

// UUIDs matching wifi_prov.c
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
};

interface ProvisioningData {
    ssid: string;
    pass: string;
    mqtt_login: string;
    mqtt_pass: string;
    user_id: string;
    broker_url: string;
}

export function useBleProvisioning() {
    const [isProvisioning, setIsProvisioning] = useState(false);
    const [provisioningError, setProvisioningError] = useState<string | null>(null);
    const [progress, setProgress] = useState<string>('');

    const provisionDevice = async (data: ProvisioningData) => {
        setIsProvisioning(true);
        setProvisioningError(null);
        setProgress('Searching for device...');

        console.log('[BLE Provisioning] Starting with data:', {
            ssid: data.ssid,
            pass: data.pass ? '***' : '(empty)',
            broker_url: data.broker_url,
            mqtt_login: data.mqtt_login,
            mqtt_pass: data.mqtt_pass ? '***' : '(empty)',
            user_id: data.user_id
        });

        try {
            const SERVICE_UUID_REV = 'efcdab90-7856-3412-efcd-ab9078563412';

            // @ts-ignore - Web Bluetooth types might be missing
            const device = await navigator.bluetooth.requestDevice({
                filters: [{ namePrefix: 'SMART_GARDEN_PROV' }],
                optionalServices: [SERVICE_UUID, SERVICE_UUID_REV]
            });

            setProgress('Connecting to GATT Server...');
            const server = await device?.gatt?.connect();

            setProgress('Discovering Services...');
            const services = await server?.getPrimaryServices();
            const service = services?.find((s: { uuid: string }) =>
                s.uuid === SERVICE_UUID || s.uuid === SERVICE_UUID_REV
            );

            if (!service) {
                throw new Error(`Service not found. Found: ${services?.map((s: { uuid: string }) => s.uuid).join(', ')}`);
            }

            console.log('Found Service:', service.uuid);

            const writeChar = async (uuid: string, value: string, label: string) => {
                setProgress(`Writing ${label}...`);
                console.log(`[BLE] Writing ${label}: "${value}" to UUID: ${uuid}`);
                try {
                    const char = await service.getCharacteristic(uuid);
                    const encoder = new TextEncoder();
                    await char.writeValue(encoder.encode(value));
                    console.log(`[BLE] ✓ Successfully wrote ${label}`);
                } catch (charError: any) {
                    console.error(`[BLE] ✗ Failed to write ${label}:`, charError);
                    throw new Error(`Failed to write ${label}: ${charError.message}`);
                }
            };

            await writeChar(UUIDS.SSID, data.ssid, 'WiFi SSID');
            await writeChar(UUIDS.PASS, data.pass, 'WiFi Password');
            await writeChar(UUIDS.BROKER, data.broker_url, 'Broker URL');
            await writeChar(UUIDS.MQTT_LOGIN, data.mqtt_login, 'MQTT Login');
            await writeChar(UUIDS.MQTT_PASS, data.mqtt_pass, 'MQTT Password');
            await writeChar(UUIDS.USER_ID, data.user_id, 'User ID');

            setProgress('Finalizing configuration...');
            const ctrlChar = await service.getCharacteristic(UUIDS.CTRL);
            // Write 0x01 to trigger save & reboot
            await ctrlChar.writeValue(new Uint8Array([0x01]));

            setProgress('Provisioning Complete! Device is rebooting.');

            // Give it a moment before disconnecting
            await new Promise(resolve => setTimeout(resolve, 1000));
            await device?.gatt?.disconnect();

        } catch (error: any) {
            console.error('BLE Provisioning failed:', error);
            setProvisioningError(error.message || 'Bluetooth connection failed');
            setProgress('');
        } finally {
            setIsProvisioning(false);
        }
    };

    return {
        provisionDevice,
        isProvisioning,
        provisioningError,
        progress
    };
}
