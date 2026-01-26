package com.smartgarden.controller;

import com.smartgarden.entity.PlantProfile;
import com.smartgarden.repository.PlantProfileRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.UUID;

@RestController
@RequestMapping("/api/profiles")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class PlantProfileController {

    private final PlantProfileRepository plantProfileRepository;
    private final com.smartgarden.repository.DeviceRepository deviceRepository;
    private final com.smartgarden.service.SmartGardenService smartGardenService;

    @GetMapping
    public List<PlantProfile> getUserProfiles() {
        String userId = SecurityContextHolder.getContext().getAuthentication().getName();
        return plantProfileRepository.findByUserId(userId);
    }

    @PostMapping
    public PlantProfile createProfile(@RequestBody PlantProfile profile) {
        String userId = SecurityContextHolder.getContext().getAuthentication().getName();
        profile.setUserId(userId);
        return plantProfileRepository.save(profile);
    }

    @DeleteMapping("/{id}")
    public ResponseEntity<Void> deleteProfile(@PathVariable UUID id) {
        String userId = SecurityContextHolder.getContext().getAuthentication().getName();
        return plantProfileRepository.findById(id)
                .map(profile -> {
                    if (!profile.getUserId().equals(userId)) {
                        return ResponseEntity.status(403).<Void>build();
                    }
                    
                    // Reset devices using this profile
                    com.smartgarden.repository.DeviceRepository deviceRepository = this.deviceRepository; // Access injected repo
                    java.util.List<com.smartgarden.entity.Device> devices = deviceRepository.findByUserIdAndActiveProfileName(userId, profile.getName());
                    for (com.smartgarden.entity.Device device : devices) {
                        device.setActiveProfileName(null);
                        deviceRepository.save(device);
                        smartGardenService.resetDeviceSettings(device.getMacAddress());
                    }

                    plantProfileRepository.delete(profile);
                    return ResponseEntity.ok().<Void>build();
                })
                .orElse(ResponseEntity.notFound().build());
    }
}
