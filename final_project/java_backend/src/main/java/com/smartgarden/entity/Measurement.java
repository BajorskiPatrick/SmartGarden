package com.smartgarden.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import java.time.LocalDateTime;

@Entity
@Table(name = "measurements")
@Data
@NoArgsConstructor
@AllArgsConstructor
public class Measurement {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "device_mac", nullable = false)
    private Device device;

    @Column(nullable = false)
    private LocalDateTime timestamp;

    // Sensor values can be null if not reported
    private Integer soilMoisture; // 0-100%

    private Float temperature;

    private Float humidity;

    private Float pressure;

    private Float lightLux;

    // 0 = OK, 1 = Error (Empty)
    private Boolean waterTankOk;
}
