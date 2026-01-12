package com.smartgarden.service;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.annotation.Bean;
import org.springframework.integration.annotation.ServiceActivator;
import org.springframework.messaging.Message;
import org.springframework.messaging.MessageHandler;
import org.springframework.stereotype.Service;

@Service
@RequiredArgsConstructor
@Slf4j
public class MqttInputHandler {

    private final SmartGardenService smartGardenService;

    @Bean
    @ServiceActivator(inputChannel = "mqttInputChannel")
    public MessageHandler handler() {
        return new MessageHandler() {
            @Override
            public void handleMessage(Message<?> message) {
                String topic = (String) message.getHeaders().get("mqtt_receivedTopic");
                String payload = (String) message.getPayload();

                log.debug("MQTT Rx [{}]: {}", topic, payload);

                if (topic == null)
                    return;

                // Topic format: garden/{user}/{device}/{type}
                // types: telemetry, alert, capabilities

                if (topic.endsWith("/telemetry")) {
                    smartGardenService.processTelemetry(payload);
                } else if (topic.endsWith("/alert")) {
                    smartGardenService.processAlert(payload);
                } else if (topic.endsWith("/capabilities")) {
                    smartGardenService.processCapabilities(payload);
                } else if (topic.endsWith("/settings/state")) {
                    // garden/{user}/{mac}/settings/state
                    String[] parts = topic.split("/");
                    if (parts.length >= 3) {
                        smartGardenService.processSettingsState(parts[2], payload);
                    }
                } else {
                    log.debug("Ignored message on topic: {}", topic);
                }
            }
        };
    }
}
