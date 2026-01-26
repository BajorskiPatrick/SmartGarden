package com.smartgarden.repository;

import com.smartgarden.entity.Device;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.Optional;

@Repository
public interface DeviceRepository extends JpaRepository<Device, String> {
    Optional<Device> findByMacAddress(String macAddress);
    java.util.List<Device> findByUserId(String userId);
    java.util.List<Device> findByUserIdAndActiveProfileName(String userId, String activeProfileName);
}
