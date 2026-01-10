package com.smartgarden.controller;

import com.smartgarden.service.ProvisioningService;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/api/provisioning")
@RequiredArgsConstructor
public class ProvisioningController {

    private final ProvisioningService provisioningService;

    @PostMapping("/device")
    public ResponseEntity<ProvisioningService.MqttCredentialsDto> registerDevice(@RequestParam String macAddress) {
        return ResponseEntity.ok(provisioningService.registerDevice(macAddress));
    }
}
