# Smart Garden Mobile App

## 🚀 How to Run

This project is configured for **Expo SDK 54** on Android.

### Prerequisites
1.  **Backend**: Ensure your MQTT/Backend server is running (Docker).
2.  **Network**: Your phone and computer must be on the same Wi-Fi.

### Step-by-Step Launch

1.  **Open Terminal** in this folder (`smart_garden_mobile`).
2.  **Clear Cache & Start Metro**:
    ```powershell
    npx expo start --clear
    ```
3.  **Run on Android**:
    *   Press `a` in the terminal to launch on a plugged-in Android device/emulator.
    *   **OR** Scan the QR code with your Expo Go app (if using development build, ensure you use the custom dev client).

### ⚠️ Current Known Issues
*   **Styling (CSS)**: NativeWind is currently **disabled** in `metro.config.js` to prevent a Windows-specific path error/crash. The app functionality (Login/Register) works, but it will look unstyled.
    *   *Fix planned for next session.*
*   **New Architecture**: Disabled (`newArchEnabled=false`) for Bluetooth compatibility.

### Troubleshooting
*   **"400 Bad Request"**: Check that you are registering a *new* username.
*   **"Network Error"**: Verify your IP address in `src/lib/axios.ts` matches your computer's LAN IP.
