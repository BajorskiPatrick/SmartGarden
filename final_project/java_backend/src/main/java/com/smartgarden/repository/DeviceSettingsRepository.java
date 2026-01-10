package com.smartgarden.repository;

import com.smartgarden.entity.DeviceSettings;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.Optional;

@Repository
public interface DeviceSettingsRepository extends JpaRepository<DeviceSettings, Long> {
    Optional<DeviceSettings> findByDevice_MacAddress(String macAddress);
}
