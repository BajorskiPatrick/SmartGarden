package com.smartgarden.service;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.smartgarden.config.MqttGateway;
import com.smartgarden.dto.DeviceSettingsDto;
import com.smartgarden.entity.Alert;
import com.smartgarden.entity.Device;
import com.smartgarden.entity.DeviceSettings;
import com.smartgarden.entity.Measurement;
import com.smartgarden.repository.AlertRepository;
import com.smartgarden.repository.DeviceRepository;
import com.smartgarden.repository.DeviceSettingsRepository;
import com.smartgarden.repository.MeasurementRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;

@Service
@RequiredArgsConstructor
@Slf4j
public class SmartGardenService {

    private final DeviceRepository deviceRepository;
    private final MeasurementRepository measurementRepository;
    private final AlertRepository alertRepository;
    private final DeviceSettingsRepository deviceSettingsRepository;
    private final ObjectMapper objectMapper;
    private final MqttGateway mqttGateway;

    // Map<MacAddress, Future> for async settings retrieval
    private final java.util.Map<String, java.util.concurrent.CompletableFuture<DeviceSettingsDto>> pendingSettingsRequests = new java.util.concurrent.ConcurrentHashMap<>();

    /**
     * Process incoming telemetry JSON.
     */
    @Transactional
    public void processTelemetry(String payload) {
        try {
            JsonNode root = objectMapper.readTree(payload);
            String mac = root.get("device").asText();
            String userId = root.has("user") ? root.get("user").asText() : "unknown";

            Device device = getOrCreateDevice(mac, userId);
            device.setLastSeen(LocalDateTime.now());
            device.setOnline(true);
            deviceRepository.save(device);

            Measurement measurement = new Measurement();
            measurement.setDevice(device);

            // Use timestamp from payload if available (epoch millis)
            if (root.has("timestamp")) {
                long ts = root.get("timestamp").asLong();
                measurement.setTimestamp(LocalDateTime.ofInstant(Instant.ofEpochMilli(ts), ZoneId.systemDefault()));
            } else {
                measurement.setTimestamp(LocalDateTime.now());
            }

            if (root.has("sensors")) {
                JsonNode sensors = root.get("sensors");
                if (sensors.has("soil_moisture_pct") && !sensors.get("soil_moisture_pct").isNull()) {
                    measurement.setSoilMoisture(sensors.get("soil_moisture_pct").asInt());
                }
                if (sensors.has("air_temperature_c") && !sensors.get("air_temperature_c").isNull()) {
                    measurement.setTemperature((float) sensors.get("air_temperature_c").asDouble());
                }
                if (sensors.has("air_humidity_pct") && !sensors.get("air_humidity_pct").isNull()) {
                    measurement.setHumidity((float) sensors.get("air_humidity_pct").asDouble());
                }
                if (sensors.has("pressure_hpa") && !sensors.get("pressure_hpa").isNull()) {
                    measurement.setPressure((float) sensors.get("pressure_hpa").asDouble());
                }
                if (sensors.has("light_lux") && !sensors.get("light_lux").isNull()) {
                    measurement.setLightLux((float) sensors.get("light_lux").asDouble());
                }
                if (sensors.has("water_tank_ok") && !sensors.get("water_tank_ok").isNull()) {
                    measurement.setWaterTankOk(sensors.get("water_tank_ok").asBoolean());
                }
            }

            measurementRepository.save(measurement);
            log.info("Saved telemetry for device: {}", mac);

        } catch (JsonProcessingException e) {
            log.error("Failed to parse telemetry payload", e);
        }
    }

    /**
     * Process incoming alert JSON.
     */
    @Transactional
    public void processAlert(String payload) {
        try {
            JsonNode root = objectMapper.readTree(payload);
            String mac = root.get("device").asText();
            String userId = root.has("user") ? root.get("user").asText() : "unknown";

            Device device = getOrCreateDevice(mac, userId);
            device.setLastSeen(LocalDateTime.now());
            deviceRepository.save(device);

            Alert alert = new Alert();
            alert.setDevice(device);

            if (root.has("timestamp")) {
                long ts = root.get("timestamp").asLong();
                alert.setTimestamp(LocalDateTime.ofInstant(Instant.ofEpochMilli(ts), ZoneId.systemDefault()));
            } else {
                alert.setTimestamp(LocalDateTime.now());
            }

            if (root.has("code"))
                alert.setCode(root.get("code").asText());
            if (root.has("severity"))
                alert.setSeverity(root.get("severity").asText());
            if (root.has("subsystem"))
                alert.setSubsystem(root.get("subsystem").asText());
            if (root.has("message"))
                alert.setMessage(root.get("message").asText());
            else if (root.has("msg"))
                alert.setMessage(root.get("msg").asText()); // legacy

            if (root.has("details") && !root.get("details").isNull()) {
                alert.setDetails(root.get("details").toString());
            }

            alertRepository.save(alert);
            log.warn("Received ALERT from device {}: {}", mac, alert.getMessage());

        } catch (JsonProcessingException e) {
            log.error("Failed to parse alert payload", e);
        }
    }

    @Transactional
    public void processCapabilities(String payload) {
        try {
            JsonNode root = objectMapper.readTree(payload);
            String mac = root.get("device").asText();
            String userId = root.has("user") ? root.get("user").asText() : "unknown";

            Device device = getOrCreateDevice(mac, userId);
            device.setLastSeen(LocalDateTime.now());
            device.setOnline(true);
            deviceRepository.save(device);
            log.info("Updated capabilities/heartbeat for device: {}", mac);

        } catch (JsonProcessingException e) {
            log.error("Failed to parse capabilities payload", e);
        }
    }

    public void sendWaterCommand(String mac, Integer duration) {
        Device device = getOrCreateDevice(mac, "unknown_user");
        String topic = String.format("garden/%s/%s/command/water", device.getUserId(), mac);

        String payload;
        if (duration != null && duration > 0) {
            payload = String.format("{\"duration\": %d}", duration);
        } else {
            payload = "{}";
        }

        mqttGateway.sendToMqtt(payload, topic);
        log.info("Sent WATER command to {} with duration {}", topic, duration);
    }

    public void sendMeasureCommand(String mac, java.util.List<String> fields) {
        Device device = getOrCreateDevice(mac, "unknown_user");
        String topic = String.format("garden/%s/%s/command/read", device.getUserId(), mac);

        String payload;
        try {
            if (fields != null && !fields.isEmpty()) {
                // Construct {"fields": ["f1", "f2"]}
                java.util.Map<String, Object> map = new java.util.HashMap<>();
                map.put("fields", fields);
                payload = objectMapper.writeValueAsString(map);
            } else {
                payload = "{}";
            }
            mqttGateway.sendToMqtt(payload, topic);
            log.info("Sent MEASURE command to {} (fields={})", topic, fields);
        } catch (JsonProcessingException e) {
            log.error("Failed to construct measure command payload", e);
        }
    }

    // --- Device Authoritative Settings Implementation ---

    public DeviceSettingsDto getDeviceSettings(String mac) {
        // 1. Send GET request via MQTT
        Device device = getOrCreateDevice(mac, "unknown");
        String topic = String.format("garden/%s/%s/settings/get", device.getUserId(), mac);
        mqttGateway.sendToMqtt("{}", topic);
        log.info("Requested settings for device {}", mac);

        // 2. Wait for response (async)
        java.util.concurrent.CompletableFuture<DeviceSettingsDto> future = new java.util.concurrent.CompletableFuture<>();
        pendingSettingsRequests.put(mac, future);

        try {
            // Wait up to 5 seconds for response
            return future.get(5, java.util.concurrent.TimeUnit.SECONDS);
        } catch (Exception e) {
            log.error("Timeout or error waiting for device settings for {}: {}", mac, e.getMessage());
            pendingSettingsRequests.remove(mac);
            // Return empty settings (defaults) or indicate error.
            // Returning empty DTO prevents null pointer exceptions in callers.
            return new DeviceSettingsDto();
        }
    }

    /**
     * Callback method called by MqttInputHandler when a settings/state message
     * arrives.
     */
    public void processSettingsState(String mac, String payload) {
        try {
            // Parse payload directly to DTO
            DeviceSettingsDto dto = objectMapper.readValue(payload, DeviceSettingsDto.class);

            // Complete the pending future if exists
            java.util.concurrent.CompletableFuture<DeviceSettingsDto> future = pendingSettingsRequests.remove(mac);
            if (future != null) {
                future.complete(dto);
            } else {
                log.debug("Received settings state for {} but no pending request (or timed out).", mac);
            }
        } catch (JsonProcessingException e) {
            log.error("Failed to parse settings state payload for " + mac, e);
        }
    }

    public void updateDeviceSettings(String mac, DeviceSettingsDto dto) {
        Device device = getOrCreateDevice(mac, "unknown");

        // Publish to MQTT directly (No DB Save)
        try {
            String payload = objectMapper.writeValueAsString(dto);
            String topic = String.format("garden/%s/%s/settings", device.getUserId(), mac);
            mqttGateway.sendToMqtt(payload, topic);
            log.info("Published updated settings to {}", topic);
        } catch (JsonProcessingException e) {
            log.error("Failed to serialize settings DTO", e);
        }
    }

    public void resetDeviceSettings(String mac) {
        Device device = getOrCreateDevice(mac, "unknown");
        String topic = String.format("garden/%s/%s/settings/reset", device.getUserId(), mac);
        mqttGateway.sendToMqtt("{}", topic);
        log.info("Sent settings/reset command to device {}", mac);
    }

    private Device getOrCreateDevice(String mac, String userId) {
        return deviceRepository.findByMacAddress(mac)
                .orElseGet(() -> {
                    Device newDevice = new Device();
                    newDevice.setMacAddress(mac);
                    newDevice.setUserId(userId);
                    newDevice.setFriendlyName("New Device " + mac.substring(8));
                    newDevice.setOnline(true);
                    newDevice.setLastSeen(LocalDateTime.now());
                    return deviceRepository.save(newDevice);
                });
    }
}
