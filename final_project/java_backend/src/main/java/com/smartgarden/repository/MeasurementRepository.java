package com.smartgarden.repository;

import com.smartgarden.entity.Measurement;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface MeasurementRepository extends JpaRepository<Measurement, Long> {
    List<Measurement> findByDeviceMacAddressOrderByTimestampDesc(String macAddress);
}
