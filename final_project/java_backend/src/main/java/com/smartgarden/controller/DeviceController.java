package com.smartgarden.controller;

import com.smartgarden.entity.Alert;
import com.smartgarden.entity.Device;
import com.smartgarden.entity.Measurement;
import com.smartgarden.repository.AlertRepository;
import com.smartgarden.repository.DeviceRepository;
import com.smartgarden.repository.MeasurementRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.web.PageableDefault;
import org.springframework.format.annotation.DateTimeFormat;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.time.LocalDateTime;
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
        String normalizedMac = normalizeMac(mac);
        return deviceRepository.findByMacAddress(normalizedMac)
                .map(ResponseEntity::ok)
                .orElse(ResponseEntity.notFound().build());
    }

    @GetMapping("/{mac}/telemetry")
    public Page<Measurement> getTelemetry(
            @PathVariable String mac,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime from,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime to,
            @PageableDefault(size = 20, sort = "timestamp", direction = org.springframework.data.domain.Sort.Direction.DESC) Pageable pageable) {

        String normalizedMac = normalizeMac(mac);
        if (from != null && to != null) {
            return measurementRepository.findByDevice_MacAddressAndTimestampBetween(normalizedMac, from, to, pageable);
        }
        return measurementRepository.findByDevice_MacAddress(normalizedMac, pageable);
    }

    @GetMapping("/{mac}/alerts")
    public Page<Alert> getAlerts(
            @PathVariable String mac,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime from,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime to,
            @PageableDefault(size = 20, sort = "timestamp", direction = org.springframework.data.domain.Sort.Direction.DESC) Pageable pageable) {

        // IMPLEMENTACJA CONSUMPCJI ALERTÓW
        // 1. Pobierz TYLKO nieprzeczytane (IsReadFalse)
        Page<Alert> alerts;
        String normalizedMac = normalizeMac(mac);
        if (from != null && to != null) {
            alerts = alertRepository.findByDevice_MacAddressAndTimestampBetweenAndIsReadFalse(normalizedMac, from, to,
                    pageable);
        } else {
            alerts = alertRepository.findByDevice_MacAddressAndIsReadFalse(normalizedMac, pageable);
        }

        // 2. Oznacz pobrane jako przeczytane
        // (Tylko te, które są na bieżącej stronie wyników)
        if (alerts.hasContent()) {
            alerts.getContent().forEach(alert -> alert.setIsRead(true));
            alertRepository.saveAll(alerts.getContent());
        }

        return alerts;
    }

    private String normalizeMac(String mac) {
        if (mac == null)
            return null;
        return mac.replace(":", "").toUpperCase();
    }

    private final com.smartgarden.service.SmartGardenService smartGardenService;

    @PostMapping("/{mac}/water")
    public void waterPlant(@PathVariable String mac, @RequestParam(required = false) Integer duration) {
        smartGardenService.sendWaterCommand(normalizeMac(mac), duration);
    }

    @GetMapping("/{mac}/settings")
    public com.smartgarden.dto.DeviceSettingsDto getSettings(@PathVariable String mac) {
        return smartGardenService.getDeviceSettings(normalizeMac(mac));
    }

    @PostMapping("/{mac}/settings")
    public void updateSettings(@PathVariable String mac, @RequestBody com.smartgarden.dto.DeviceSettingsDto dto) {
        smartGardenService.updateDeviceSettings(normalizeMac(mac), dto);
    }
}
