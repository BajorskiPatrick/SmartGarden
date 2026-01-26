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
import org.springframework.messaging.simp.SimpMessagingTemplate;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j; // Add this if logging is needed, or just standard logger
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
    private final SimpMessagingTemplate messagingTemplate;
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

            java.util.Optional<Device> deviceOpt = getDeviceIfExists(mac);
            if (deviceOpt.isEmpty()) {
                log.debug("Ignored telemetry from unknown device: {}", mac);
                return;
            }
            Device device = deviceOpt.get();

            device.setLastSeen(LocalDateTime.now());
            device.setOnline(true);
            deviceRepository.save(device);

            Measurement measurement = new Measurement();
            measurement.setDevice(device);

            // Use relative time if available (fixes synchronization issues)
            if (root.has("seconds_ago")) {
                long secondsAgo = root.get("seconds_ago").asLong();
                // We use server's "now" minus the age of the measurement
                measurement.setTimestamp(LocalDateTime.now().minusSeconds(secondsAgo));
            } 
            // Fallback to absolute timestamp from payload (epoch millis)
            else if (root.has("timestamp")) {
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
            
            // Broadcast telemetry to WebSocket topic
            // We need to map Entity to DTO or send a custom object. 
            // For simplicity, let's create a map or modify Measurement entity to be serializable safely? 
            // Measurement entity has Circular ref (device). 
            // Better to send a quick DTO.
            
            java.util.Map<String, Object> telemetryUpdate = new java.util.HashMap<>();
            telemetryUpdate.put("timestamp", measurement.getTimestamp().toString());
            telemetryUpdate.put("soilMoisture", measurement.getSoilMoisture());
            telemetryUpdate.put("temperature", measurement.getTemperature());
            telemetryUpdate.put("humidity", measurement.getHumidity());
            telemetryUpdate.put("pressure", measurement.getPressure());
            telemetryUpdate.put("lightLux", measurement.getLightLux());
            telemetryUpdate.put("waterTankOk", measurement.getWaterTankOk());
            
            messagingTemplate.convertAndSend("/topic/device/" + mac + "/telemetry", telemetryUpdate);

            // Also broadcast to User's private topic for Dashboard updates
            if (!"unknown".equals(userId)) {
                // Add device mac to payload for dashboard identification
                telemetryUpdate.put("macAddress", mac); 
                messagingTemplate.convertAndSend("/topic/user/" + userId + "/telemetry", telemetryUpdate);
            }
            
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

            java.util.Optional<Device> deviceOpt = getDeviceIfExists(mac);
            if (deviceOpt.isEmpty()) {
                log.debug("Ignored alert from unknown device: {}", mac);
                return;
            }
            Device device = deviceOpt.get();

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
            
            // Broadcast valid Alert to WebSocket
            // Need DTO or just send entity? Entity is fine if no recursion issue (Alert -> Device)
            // But Alert has Device. Let's send a simplified map or DTO
            java.util.Map<String, Object> alertMsg = new java.util.HashMap<>();
            alertMsg.put("id", alert.getId());
            alertMsg.put("message", alert.getMessage());
            alertMsg.put("severity", alert.getSeverity());
            alertMsg.put("timestamp", alert.getTimestamp().toString());
            alertMsg.put("deviceMac", mac);
            
            // Send to device topic
            messagingTemplate.convertAndSend("/topic/device/" + mac + "/alerts", alertMsg);
            
            // Send to user global topic (for bells)
            // We need userId. We looked it up earlier: device.getUserId().
            if (device.getUserId() != null) {
                messagingTemplate.convertAndSend("/topic/user/" + device.getUserId() + "/alerts", alertMsg);
            }

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

            java.util.Optional<Device> deviceOpt = getDeviceIfExists(mac);
            if (deviceOpt.isEmpty()) {
                return;
            }
            Device device = deviceOpt.get();
            
            device.setLastSeen(LocalDateTime.now());
            device.setOnline(true);
            deviceRepository.save(device);
            log.info("Updated capabilities/heartbeat for device: {}", mac);

        } catch (JsonProcessingException e) {
            log.error("Failed to parse capabilities payload", e);
        }
    }

    public void sendWaterCommand(String mac, Integer duration) {
        Device device = getDeviceOrThrow(mac);
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

    public void sendMeasureCommand(String mac) {
        Device device = getDeviceOrThrow(mac);
        // ESP subscribes to "command/read" not "command/measure"
        String topic = String.format("garden/%s/%s/command/read", device.getUserId(), mac);
        mqttGateway.sendToMqtt("{}", topic);
        log.info("Sent READ command to {}", topic);
    }

    // --- Device Authoritative Settings Implementation ---

    public DeviceSettingsDto getDeviceSettings(String mac) {
        // 1. Send GET request via MQTT
        Device device = getDeviceOrThrow(mac);
        String topic = String.format("garden/%s/%s/settings/get", device.getUserId(), mac);
        mqttGateway.sendToMqtt("{}", topic);
        log.info("Requested settings for device {}", mac);

        // 2. Wait for response (async)
        java.util.concurrent.CompletableFuture<DeviceSettingsDto> future = new java.util.concurrent.CompletableFuture<>();
        pendingSettingsRequests.put(mac, future);

        try {
            // Wait up to 5 seconds for response
            DeviceSettingsDto dto = future.get(5, java.util.concurrent.TimeUnit.SECONDS);
            // Append local DB info (activeProfileName)
            dto.setActiveProfileName(device.getActiveProfileName());
            return dto;
        } catch (Exception e) {
            log.error("Timeout or error waiting for device settings for {}: {}", mac, e.getMessage());
            pendingSettingsRequests.remove(mac);
            // Return empty settings (defaults) or indicate error.
            DeviceSettingsDto dto = new DeviceSettingsDto();
            dto.setActiveProfileName(device.getActiveProfileName());
            return dto;
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
        Device device = getDeviceOrThrow(mac);

        // 1. Update persisted metadata (Active Profile)
        if (dto.getActiveProfileName() != null) {
            device.setActiveProfileName(dto.getActiveProfileName());
            deviceRepository.save(device);
        }
        
        // 2. Publish to MQTT directly
        try {
            // We only send the configuration fields to ESP, not the metadata like 'activeProfileName' which ESP ignores anyway usually, but cleaner to keep.
            // ESP usually uses Json library that ignores unknown fields, so sending extra field is safe.
            String payload = objectMapper.writeValueAsString(dto);
            String topic = String.format("garden/%s/%s/settings", device.getUserId(), mac);
            mqttGateway.sendToMqtt(payload, topic);
            log.info("Published updated settings to {}", topic);
        } catch (JsonProcessingException e) {
            log.error("Failed to serialize settings DTO", e);
        }
    }

    public void resetDeviceSettings(String mac) {
        Device device = getDeviceOrThrow(mac);
        String topic = String.format("garden/%s/%s/settings/reset", device.getUserId(), mac);
        mqttGateway.sendToMqtt("{}", topic);
        log.info("Sent settings/reset command to device {}", mac);
    }

    private java.util.Optional<Device> getDeviceIfExists(String mac) {
        return deviceRepository.findByMacAddress(mac);
    }
    
    private Device getDeviceOrThrow(String mac) {
        return deviceRepository.findByMacAddress(mac)
                .orElseThrow(() -> new RuntimeException("Device not found: " + mac));
    }

    public void updateDeviceName(String mac, String friendlyName) {
        Device device = getDeviceOrThrow(mac);
        device.setFriendlyName(friendlyName);
        deviceRepository.save(device);
        log.info("Renamed device {} to {}", mac, friendlyName);
    }
    @Transactional
    public void deleteDevice(String mac) {
        // Normalize MAC just in case
        String normalizedMac = normalizeMac(mac); // Need to make sure normalizeMac is accessible or inline it
        
        log.info("Deleting device {} and all associated data", normalizedMac);
        
        measurementRepository.deleteByDevice_MacAddress(normalizedMac);
        alertRepository.deleteByDevice_MacAddress(normalizedMac);
        deviceSettingsRepository.deleteByDevice_MacAddress(normalizedMac);
        deviceRepository.deleteById(normalizedMac);
        
        // Optional: Clean up MQTT ACLs if needed, but keeping it simple for now as per plan
    }
    
    // Helper to ensure mac format match
    private String normalizeMac(String mac) {
         if (mac == null) return null;
         return mac.replace(":", "").toUpperCase();
    }
}
