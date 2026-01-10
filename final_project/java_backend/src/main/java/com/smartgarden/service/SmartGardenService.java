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

import java.time.LocalDateTime;
import java.util.Optional;

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
            measurement.setTimestamp(LocalDateTime.now());

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
            alert.setTimestamp(LocalDateTime.now());

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

    public void sendWaterCommand(String mac) {
        // Topic: garden/{user}/{device}/command/water
        Device device = getOrCreateDevice(mac, "unknown_user");
        String topic = String.format("garden/%s/%s/command/water", device.getUserId(), mac);
        String payload = "{\"duration\": 5}"; // Default 5 seconds
        mqttGateway.sendToMqtt(payload, topic);
        log.info("Sent WATER command to {}", topic);
    }

    public DeviceSettingsDto getDeviceSettings(String mac) {
        Device settingsDevice = getOrCreateDevice(mac, "unknown");
        return deviceSettingsRepository.findByDevice_MacAddress(mac)
                .map(this::mapToDto)
                .orElseGet(() -> {
                    // Create defaults if not exists
                    DeviceSettings defaults = new DeviceSettings();
                    defaults.setDevice(settingsDevice);
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

        deviceSettingsRepository.save(settings);

        // Publish to MQTT
        // Topic: garden/{user}/{device}/thresholds
        // Payload: {"temp_min": 10.0, ...}
        String topic = String.format("garden/%s/%s/thresholds", device.getUserId(), mac);
        String payload = buildThresholdsJson(settings);
        mqttGateway.sendToMqtt(payload, topic);
        log.info("Sent updated thresholds to {}", topic);
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
        return dto;
    }

    private String buildThresholdsJson(DeviceSettings s) {
        // Simple manual JSON construction to avoid jackson overhead here if preferred,
        // or use ObjectMapper
        // Using string formatting for simplicity
        return String.format(java.util.Locale.US,
                "{" +
                        "\"temp_min\": %.2f, \"temp_max\": %.2f, " +
                        "\"hum_min\": %.2f, \"hum_max\": %.2f, " +
                        "\"soil_min\": %d, \"soil_max\": %d, " +
                        "\"light_min\": %.2f, \"light_max\": %.2f" +
                        "}",
                s.getTempMin(), s.getTempMax(),
                s.getHumMin(), s.getHumMax(),
                s.getSoilMin(), s.getSoilMax(),
                s.getLightMin(), s.getLightMax());
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
