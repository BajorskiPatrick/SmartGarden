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

            // Known Alert Codes from ESP32:
            // - temperature_low, temperature_high
            // - humidity_low, humidity_high
            // - soil_moisture_low, soil_moisture_high
            // - light_low, light_high
            // - water_level_critical (CRITICAL)
            // - provisioning.timeout, provisioning.incomplete, provisioning.save_failed
            // - wifi.disconnected, wifi.got_ip
            // - system.factory_reset
            // - sensor.*_recovered, sensor.*_read_failed
            // - connection.mqtt_connected, connection.mqtt_disconnected,
            // connection.mqtt_error
            // - command.watering_started, command.watering_finished
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
        // Topic: garden/{user}/{device}/command/water
        Device device = getOrCreateDevice(mac, "unknown_user");
        String topic = String.format("garden/%s/%s/command/water", device.getUserId(), mac);

        String payload;
        if (duration != null && duration > 0) {
            payload = String.format("{\"duration\": %d}", duration);
        } else {
            // Sending empty JSON or default duration?
            // If we send empty {}, ESP32 uses its configured default.
            // If we send explicitly "duration": 5, it overrides.
            // Let's send empty to let ESP32 decide based on its settings,
            // OR fetch settings here and send them?
            // ESP32 logic: "int duration = settings.watering_duration_sec; ... if
            // (cJSON_IsNumber(d)) duration = d->valueint;"
            // So sending {} triggers default.
            payload = "{}";
        }

        mqttGateway.sendToMqtt(payload, topic);
        log.info("Sent WATER command to {} with duration {}", topic, duration);
    }

    public DeviceSettingsDto getDeviceSettings(String mac) {
        Device settingsDevice = getOrCreateDevice(mac, "unknown");
        return deviceSettingsRepository.findByDevice_MacAddress(mac)
                .map(this::mapToDto)
                .orElseGet(() -> {
                    // Create defaults if not exists
                    DeviceSettings defaults = new DeviceSettings();
                    defaults.setDevice(settingsDevice);
                    defaults.setWateringDurationSeconds(5); // Default 5s
                    defaults.setMeasurementIntervalSeconds(60); // Default 60s
                    deviceSettingsRepository.save(defaults);
                    return mapToDto(defaults);
                });
    }

    public void updateDeviceSettings(String mac, DeviceSettingsDto dto) {
        Device device = getOrCreateDevice(mac, "unknown");
        DeviceSettings settings = deviceSettingsRepository.findByDevice_MacAddress(mac)
                .orElse(new DeviceSettings());
        settings.setDevice(device);

        // Update fields if present
        if (dto.getTempMin() != null)
            settings.setTempMin(dto.getTempMin());
        if (dto.getTempMax() != null)
            settings.setTempMax(dto.getTempMax());
        if (dto.getHumMin() != null)
            settings.setHumMin(dto.getHumMin());
        if (dto.getHumMax() != null)
            settings.setHumMax(dto.getHumMax());
        if (dto.getSoilMin() != null)
            settings.setSoilMin(dto.getSoilMin());
        if (dto.getSoilMax() != null)
            settings.setSoilMax(dto.getSoilMax());
        if (dto.getLightMin() != null)
            settings.setLightMin(dto.getLightMin());
        if (dto.getLightMax() != null)
            settings.setLightMax(dto.getLightMax());

        if (dto.getWateringDurationSeconds() != null)
            settings.setWateringDurationSeconds(dto.getWateringDurationSeconds());

        if (dto.getMeasurementIntervalSeconds() != null)
            settings.setMeasurementIntervalSeconds(dto.getMeasurementIntervalSeconds());

        deviceSettingsRepository.save(settings);

        // Publish to MQTT
        try {
            // Map Entity to DTO (which has JSON annotations) to send proper JSON
            DeviceSettingsDto fullDto = mapToDto(settings);
            String payload = objectMapper.writeValueAsString(fullDto);
            String topic = String.format("garden/%s/%s/settings", device.getUserId(), mac);
            mqttGateway.sendToMqtt(payload, topic);
            log.info("Published updated settings to {}", topic);
        } catch (JsonProcessingException e) {
            log.error("Failed to allow msg", e);
        }
    }

    public void resetDeviceSettings(String mac) {
        // Defaults:
        // Temp/Hum/Light: Infinite (nulls/doubles) -> we set nulls in DTO to signify
        // "no limit"
        // But for updateDeviceSettings logic, we want to OVERWRITE existing values with
        // defaults.
        // So we need a DTO that has explicit default values.

        DeviceSettingsDto defaults = new DeviceSettingsDto();

        // Use "impossible" values to effectively disable checks or use defaults
        // Backend entity uses Float/Int.
        // Logic in ESP32: valid if min <= max.
        // To "disable" limits: min = -Inf, max = Inf.
        // Java Double.NEGATIVE_INFINITY translates to JSON specific handling or "null"
        // if not supported?
        // Jackson supports Infinity. Let's see if ESP32 cJSON supports it. cJSON might
        // not parse "Infinity".
        // ESP32 code: .temp_min = -INFINITY.
        // If we send null in JSON, our update logic IGNORES it (keeps old value).
        // So we must send explicit values.

        // Let's explicitly set these to "safe" wide ranges.
        defaults.setTempMin(-100.0f);
        defaults.setTempMax(100.0f);
        defaults.setHumMin(0.0f);
        defaults.setHumMax(100.0f);
        defaults.setSoilMin(0);
        defaults.setSoilMax(100);
        defaults.setLightMin(0.0f);
        defaults.setLightMax(1000000.0f);

        defaults.setWateringDurationSeconds(5);
        defaults.setMeasurementIntervalSeconds(60);

        updateDeviceSettings(mac, defaults);
        log.info("Reset settings for device {} to defaults.", mac);
        updateDeviceSettings(mac, defaults);
        log.info("Reset settings for device {} to defaults.", mac);
    }

    private DeviceSettingsDto mapToDto(DeviceSettings s) {
        DeviceSettingsDto dto = new DeviceSettingsDto();
        dto.setTempMin(s.getTempMin());
        dto.setTempMax(s.getTempMax());
        dto.setHumMin(s.getHumMin());
        dto.setHumMax(s.getHumMax());
        dto.setSoilMin(s.getSoilMin());
        dto.setSoilMax(s.getSoilMax());
        dto.setLightMin(s.getLightMin());
        dto.setLightMax(s.getLightMax());
        dto.setWateringDurationSeconds(s.getWateringDurationSeconds() != null ? s.getWateringDurationSeconds() : 5);
        dto.setMeasurementIntervalSeconds(
                s.getMeasurementIntervalSeconds() != null ? s.getMeasurementIntervalSeconds() : 60);
        return dto;
    }

    private String buildThresholdsJson(DeviceSettings s) {
        try {
            return objectMapper.writeValueAsString(mapToDto(s));
        } catch (JsonProcessingException e) {
            log.error("Failed to serialize thresholds", e);
            return "{}";
        }
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
