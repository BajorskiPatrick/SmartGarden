package com.smartgarden.service;

import com.smartgarden.entity.MqttUser;
import com.smartgarden.repository.MqttUserRepository;
import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;

@Service
@RequiredArgsConstructor
@Slf4j
public class ProvisioningService {

    private final MqttUserRepository mqttUserRepository;
    private final PasswordEncoder passwordEncoder;

    @Value("${mqtt.username}")
    private String backendMqttUser;

    @Value("${mqtt.password}")
    private String backendMqttPass;

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
    }

    public MqttCredentialsDto registerDevice(String macAddress) {
        String deviceUsername = "device_" + macAddress.replace(":", "").toLowerCase();

        // Check if exists
        if (mqttUserRepository.findByUsername(deviceUsername).isPresent()) {
            throw new RuntimeException("Device already registered");
        }

        // Generate random password (simple for now)
        String rawPassword = java.util.UUID.randomUUID().toString().substring(0, 8);

        MqttUser user = new MqttUser();
        user.setUsername(deviceUsername);
        user.setPasswordHash(passwordEncoder.encode(rawPassword));
        user.setSuperuser(false);
        mqttUserRepository.save(user);

        return new MqttCredentialsDto(deviceUsername, rawPassword);
    }

    public record MqttCredentialsDto(String username, String password) {
    }
}
