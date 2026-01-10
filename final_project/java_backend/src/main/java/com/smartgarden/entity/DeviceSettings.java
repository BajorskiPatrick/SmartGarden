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
    private Float tempMin = 5.0f;
    private Float tempMax = 100.0f; // effectively infinity in context

    private Float humMin = 20.0f;
    private Float humMax = 100.0f;

    private Integer soilMin = 10;
    private Integer soilMax = 100;

    private Float lightMin = 100.0f;
    private Float lightMax = 20000.0f; // arbitrary high value
}
