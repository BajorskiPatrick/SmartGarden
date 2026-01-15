const { withAndroidManifest, AndroidConfig } = require('@expo/config-plugins');

const withBleAndroid = (config, { neverForLocation = false } = {}) => {
  return withAndroidManifest(config, async (config) => {
    const androidManifest = config.modResults;
    
    // Permission helper
    const addPermission = (permission) => {
         AndroidConfig.Permissions.addPermission(androidManifest, permission);
    };

    addPermission('android.permission.BLUETOOTH');
    addPermission('android.permission.BLUETOOTH_ADMIN');
    addPermission('android.permission.BLUETOOTH_CONNECT');
    
    // For scanning, we might need location depending on Android version, unless 'neverForLocation' is true
    // But since provisioning usually needs location services, we should add it.
    // If we want to use the 'neverForLocation' flag for Android 12+, we need to add the attribute.
    // However, Permissions.addPermission in Expo config plugins usually just adds the permission tag.
    // To add attributes like `android:usesPermissionFlags="neverForLocation"`, we might need manual XML manipulation or a specialized helper.
    // For now, let's just add the plain permission to ensure it works generally.
    addPermission('android.permission.BLUETOOTH_SCAN');
    addPermission('android.permission.ACCESS_FINE_LOCATION');

    // Add hardware feature
    if (!androidManifest.manifest['uses-feature']) {
        androidManifest.manifest['uses-feature'] = [];
    }
    const hasBleFeature = androidManifest.manifest['uses-feature'].some(
        (feature) => feature.$['android:name'] === 'android.hardware.bluetooth_le'
    );
    if (!hasBleFeature) {
        androidManifest.manifest['uses-feature'].push({
            $: {
                'android:name': 'android.hardware.bluetooth_le',
                'android:required': 'true',
            },
        });
    }

    return config;
  });
};

const withBle = (config, props = {}) => {
    return withBleAndroid(config, props);
};

module.exports = withBle;
