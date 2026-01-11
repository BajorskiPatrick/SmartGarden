package com.smartgarden.repository;

import com.smartgarden.entity.Alert;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;

@Repository
public interface AlertRepository extends JpaRepository<Alert, Long> {
    Page<Alert> findByDevice_MacAddress(String macAddress, Pageable pageable);

    Page<Alert> findByDevice_MacAddressAndTimestampBetween(
            String macAddress, LocalDateTime start, LocalDateTime end, Pageable pageable);

    void deleteByDevice_MacAddress(String macAddress);
}
