# Repair Android Environment
Write-Host "Repairing SmartGarden Mobile Environment..." -ForegroundColor Green

# 1. Kill potentially locking processes
Stop-Process -Name "java" -ErrorAction SilentlyContinue
Stop-Process -Name "node" -ErrorAction SilentlyContinue
Stop-Process -Name "adb" -ErrorAction SilentlyContinue

# 2. Navigate to project
Set-Location "C:\sg\final_project\smart_garden_mobile"

# 3. Clean Gradle Build
Write-Host "Cleaning Gradle..." -ForegroundColor Yellow
cd android
./gradlew clean
cd ..

# 4. Install Dependencies (ensure clean state)
Write-Host "Verifying Dependencies..." -ForegroundColor Yellow
npm install

# 5. Run Prebuild to regenerate Android Manifests (Clean)
Write-Host "Regenerating Android Native Code..." -ForegroundColor Yellow
npx expo prebuild --clean --platform android

# 6. Start the App on LAN
Write-Host "Starting App on LAN..." -ForegroundColor Green
Write-Host "Scan the QR code with Expo Go or build the development client."
Write-Host "If building custom client, run: npx expo run:android --device"

# For physical device build:
npx expo run:android
