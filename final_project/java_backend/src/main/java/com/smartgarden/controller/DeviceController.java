package com.smartgarden.controller;

import com.smartgarden.entity.Alert;
import com.smartgarden.entity.Device;
import com.smartgarden.entity.Measurement;
import com.smartgarden.repository.AlertRepository;
import com.smartgarden.repository.DeviceRepository;
import com.smartgarden.repository.MeasurementRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/api/devices")
@RequiredArgsConstructor
@CrossOrigin(origins = "*") // Allow frontend access
public class DeviceController {

    private final DeviceRepository deviceRepository;
    private final MeasurementRepository measurementRepository;
    private final AlertRepository alertRepository;

    @GetMapping
    public List<Device> getAllDevices() {
        return deviceRepository.findAll();
    }

    @GetMapping("/{mac}")
    public ResponseEntity<Device> getDevice(@PathVariable String mac) {
        return deviceRepository.findByMacAddress(mac)
                .map(ResponseEntity::ok)
                .orElse(ResponseEntity.notFound().build());
    }

    @GetMapping("/{mac}/telemetry")
    public List<Measurement> getDeviceTelemetry(@PathVariable String mac) {
        return measurementRepository.findByDeviceMacAddressOrderByTimestampDesc(mac);
    }

    @GetMapping("/{mac}/alerts")
    public List<Alert> getDeviceAlerts(@PathVariable String mac) {
        return alertRepository.findByDeviceMacAddressOrderByTimestampDesc(mac);
    }

    private final com.smartgarden.service.SmartGardenService smartGardenService;

    @PostMapping("/{mac}/water")
    public void waterPlant(@PathVariable String mac) {
        smartGardenService.sendWaterCommand(mac);
    }

    @GetMapping("/{mac}/settings")
    public com.smartgarden.dto.DeviceSettingsDto getSettings(@PathVariable String mac) {
        return smartGardenService.getDeviceSettings(mac);
    }

    @PostMapping("/{mac}/settings")
    public void updateSettings(@PathVariable String mac, @RequestBody com.smartgarden.dto.DeviceSettingsDto dto) {
        smartGardenService.updateDeviceSettings(mac, dto);
    }
}
