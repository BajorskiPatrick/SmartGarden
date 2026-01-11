package com.smartgarden.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;

@Entity
@Table(name = "device_settings")
@Data
@NoArgsConstructor
@AllArgsConstructor
public class DeviceSettings {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @OneToOne
    @JoinColumn(name = "device_id", nullable = false, unique = true)
    private Device device;

    // Defaults based on ESP32 code
    // Defaults based on ESP32 code
    private Float tempMin;
    private Float tempMax;

    private Float humMin;
    private Float humMax;

    private Integer soilMin;
    private Integer soilMax;

    private Float lightMin;
    private Float lightMax;

    private Integer wateringDurationSeconds;
}
