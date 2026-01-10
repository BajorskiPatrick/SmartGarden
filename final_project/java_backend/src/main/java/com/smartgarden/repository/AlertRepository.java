package com.smartgarden.repository;

import com.smartgarden.entity.Alert;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface AlertRepository extends JpaRepository<Alert, Long> {
    List<Alert> findByDeviceMacAddressOrderByTimestampDesc(String macAddress);
}
