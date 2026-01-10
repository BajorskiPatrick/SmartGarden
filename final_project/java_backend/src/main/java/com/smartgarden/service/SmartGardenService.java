package com.smartgarden.service;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.smartgarden.config.MqttGateway;
import com.smartgarden.entity.Alert;
import com.smartgarden.entity.Device;
import com.smartgarden.entity.Measurement;
import com.smartgarden.repository.AlertRepository;
import com.smartgarden.repository.DeviceRepository;
import com.smartgarden.repository.MeasurementRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;

@Service
@RequiredArgsConstructor
@Slf4j
public class SmartGardenService {

    private final DeviceRepository deviceRepository;
    private final MeasurementRepository measurementRepository;
    private final AlertRepository alertRepository;
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
        Device device = deviceRepository.findByMacAddress(mac)
                .orElseThrow(() -> new RuntimeException("Device not found"));

        String topic = String.format("garden/%s/%s/command/water", device.getUserId(), device.getMacAddress());
        String payload = "{\"command\":\"ON\", \"duration\":5}";

        mqttGateway.sendToMqtt(payload, topic);
        log.info("Sent WATER command to {}", topic);
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
