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
    private final com.smartgarden.repository.PlantProfileRepository plantProfileRepository;

    @GetMapping
    public List<com.smartgarden.dto.DeviceSummaryDto> getAllDevices() {
        String username = org.springframework.security.core.context.SecurityContextHolder.getContext().getAuthentication().getName();
        List<Device> devices = deviceRepository.findByUserId(username);

        return devices.stream().map(device -> {
            com.smartgarden.dto.DeviceSummaryDto dto = new com.smartgarden.dto.DeviceSummaryDto(device);
            
            // Get latest measurement
            org.springframework.data.domain.Pageable topOne = org.springframework.data.domain.PageRequest.of(0, 1, org.springframework.data.domain.Sort.by(org.springframework.data.domain.Sort.Direction.DESC, "timestamp"));
            List<Measurement> measurements = measurementRepository.findByDevice_MacAddress(device.getMacAddress(), topOne).getContent();

            if (!measurements.isEmpty()) {
                Measurement m = measurements.get(0);
                dto.setTemperature(m.getTemperature());
                dto.setSoilMoisture(m.getSoilMoisture());
                dto.setHumidity(m.getHumidity());
                dto.setLightLux(m.getLightLux());
                dto.setWaterTankOk(m.getWaterTankOk());
                dto.setLastMeasurementTime(m.getTimestamp());
            }
            return dto;
        }).collect(java.util.stream.Collectors.toList());
    }

    @GetMapping("/{mac}")
    public ResponseEntity<com.smartgarden.dto.DeviceSummaryDto> getDevice(@PathVariable String mac) {
        String normalizedMac = normalizeMac(mac);
        return deviceRepository.findByMacAddress(normalizedMac)
                .map(device -> {
                    com.smartgarden.dto.DeviceSummaryDto dto = new com.smartgarden.dto.DeviceSummaryDto(device);
                    return ResponseEntity.ok(dto);
                })
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

        // Fetch only unread alerts
        Page<Alert> alerts;
        String normalizedMac = normalizeMac(mac);
        if (from != null && to != null) {
            alerts = alertRepository.findByDevice_MacAddressAndTimestampBetweenAndIsReadFalse(normalizedMac, from, to,
                    pageable);
        } else {
            alerts = alertRepository.findByDevice_MacAddressAndIsReadFalse(normalizedMac, pageable);
        }

        // Mark fetched alerts as read
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

    @PostMapping("/{mac}/measure")
    public void requestMeasurement(@PathVariable String mac) {
        smartGardenService.sendMeasureCommand(normalizeMac(mac));
    }

    @GetMapping("/{mac}/settings")
    public com.smartgarden.dto.DeviceSettingsDto getSettings(@PathVariable String mac) {
        return smartGardenService.getDeviceSettings(normalizeMac(mac));
    }

    @PostMapping("/{mac}/settings")
    public void updateSettings(@PathVariable String mac, @RequestBody com.smartgarden.dto.DeviceSettingsDto settings) {
        smartGardenService.updateDeviceSettings(normalizeMac(mac), settings);
    }

    @PatchMapping("/{mac}")
    public void renameDevice(@PathVariable String mac, @RequestBody java.util.Map<String, String> body) {
        String newName = body.get("friendlyName");
        if (newName != null) {
            smartGardenService.updateDeviceName(normalizeMac(mac), newName);
        }
    }

    @PostMapping("/{mac}/settings/reset")
    public void resetSettings(@PathVariable String mac) {
        smartGardenService.resetDeviceSettings(normalizeMac(mac));
    }

    @DeleteMapping("/{mac}")
    public void deleteDevice(@PathVariable String mac) {
        smartGardenService.deleteDevice(mac);
    }
}
