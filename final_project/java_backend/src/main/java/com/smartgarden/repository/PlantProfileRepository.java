package com.smartgarden.repository;

import com.smartgarden.entity.PlantProfile;
import org.springframework.data.jpa.repository.JpaRepository;
import java.util.List;
import java.util.UUID;

public interface PlantProfileRepository extends JpaRepository<PlantProfile, UUID> {
    List<PlantProfile> findByUserId(String userId);
    java.util.Optional<PlantProfile> findByUserIdAndName(String userId, String name);
}
