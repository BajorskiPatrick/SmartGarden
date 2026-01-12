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
                    plantProfileRepository.delete(profile);
                    return ResponseEntity.ok().<Void>build();
                })
                .orElse(ResponseEntity.notFound().build());
    }
}
