package com.smartgarden.service;

import com.smartgarden.entity.Device;
import com.smartgarden.entity.MqttUser;
import com.smartgarden.entity.MqttAcl;
import com.smartgarden.repository.AlertRepository;
import com.smartgarden.repository.DeviceRepository;
import com.smartgarden.repository.DeviceSettingsRepository;
import com.smartgarden.repository.MeasurementRepository;
import com.smartgarden.repository.MqttUserRepository;
import com.smartgarden.repository.MqttAclRepository;
import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

@Service
@RequiredArgsConstructor
@Slf4j
public class ProvisioningService {

    private final MqttUserRepository mqttUserRepository;
    private final MqttAclRepository mqttAclRepository;
    private final DeviceRepository deviceRepository;
    private final MeasurementRepository measurementRepository;
    private final AlertRepository alertRepository;
    private final DeviceSettingsRepository deviceSettingsRepository;
    private final PasswordEncoder passwordEncoder;

    @Value("${mqtt.username}")
    private String backendMqttUser;

    @Value("${mqtt.password}")
    private String backendMqttPass;

    @Value("${mqtt.broker.public-url}")
    private String publicBrokerUrl;

    @PostConstruct
    public void initBackendUser() {
        if (backendMqttUser != null && !backendMqttUser.isEmpty() &&
                mqttUserRepository.findByUsername(backendMqttUser).isEmpty()) {

            log.info("Seeding backend MQTT user: {}", backendMqttUser);
            MqttUser user = new MqttUser();
            user.setUsername(backendMqttUser);
            user.setPasswordHash(passwordEncoder.encode(backendMqttPass));
            user.setSuperuser(true);
            mqttUserRepository.save(user);
        }

        // Ensure ACL exists even if user existed (in case of schema update)
        if (backendMqttUser != null && !mqttAclRepository.existsByUsernameAndTopic(backendMqttUser, "garden/#")) {
            log.info("Seeding backend MQTT ACL for: {}", backendMqttUser);
            MqttAcl acl = new MqttAcl();
            acl.setUsername(backendMqttUser);
            acl.setTopic("garden/#");
            acl.setRw(3); // Read-Write
            mqttAclRepository.save(acl);
        }
    }

    @Transactional
    public MqttCredentialsDto registerDevice(String rawMacAddress, String username) {
        // ESP32 uses MAC without colons (e.g., AABBCCDDEEFF) in topics and payload.
        // We must normalize input to match hardware identity and DB constraints (length
        // 12).
        String macAddress = rawMacAddress.replace(":", "").toUpperCase();
        String deviceUsername = "device_" + macAddress.toLowerCase();

        // 1. Ensure Device entity exists and is assigned to user
        Device device = deviceRepository.findByMacAddress(macAddress)
                .orElse(new Device());

        // Implement "Resell" Logic: If device exists, clear its data
        // Implement "Resell" Logic: If device exists AND user changes, clear its data
        if (device.getMacAddress() != null) {
            if (!username.equals(device.getUserId())) {
                log.info("Ownership change for device {}. Clearing old data from previous user.", macAddress);
                measurementRepository.deleteByDevice_MacAddress(macAddress);
                alertRepository.deleteByDevice_MacAddress(macAddress);
                deviceSettingsRepository.deleteByDevice_MacAddress(macAddress);
                // Note: We do not delete the device entity itself, just reassign it below.
            } else {
                log.info("Re-provisioning device {} for same user {}. Preserving data.", macAddress, username);
            }
        }

        device.setMacAddress(macAddress);
        device.setUserId(username); // Assign ownership
        if (device.getFriendlyName() == null) {
            device.setFriendlyName("New Device " + macAddress.substring(macAddress.length() - 4));
        }
        deviceRepository.save(device);

        // 2. Check if MQTT creds exist
        // 2. Check if MQTT creds exist
        MqttUser mqttUser = mqttUserRepository.findByUsername(deviceUsername)
                .orElse(new MqttUser());

        // Always rotate password on re-provisioning
        String newPassword = java.util.UUID.randomUUID().toString().substring(0, 8);
        mqttUser.setUsername(deviceUsername);
        mqttUser.setPasswordHash(passwordEncoder.encode(newPassword));
        mqttUser.setSuperuser(false);
        mqttUserRepository.save(mqttUser);

        // 3. Ensure ACL for device
        // Grant full access to garden subtree for simplicity during dev
        if (!mqttAclRepository.existsByUsernameAndTopic(deviceUsername, "garden/#")) {
            MqttAcl acl = new MqttAcl();
            acl.setUsername(deviceUsername);
            acl.setTopic("garden/#");
            acl.setRw(3); // Read-Write
            mqttAclRepository.save(acl);
        }

        return new MqttCredentialsDto(deviceUsername, newPassword, username, publicBrokerUrl);
    }

    public record MqttCredentialsDto(String mqtt_login, String mqtt_password, String user_id, String broker_url) {
    }
}
