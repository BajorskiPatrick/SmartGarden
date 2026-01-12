package com.smartgarden.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import java.util.UUID;

@Entity
@Table(name = "plant_profiles")
@Data
@NoArgsConstructor
@AllArgsConstructor
public class PlantProfile {

    @Id
    @GeneratedValue(strategy = GenerationType.UUID)
    private UUID id;

    @Column(name = "user_id", nullable = false)
    private String userId;

    @Column(nullable = false)
    private String name;

    @Column(length = 500)
    private String description;

    // We can embed settings or map them flat. 
    // For simplicity with existing DTO structure, let's map essential settings flat here.
    
    private Integer measurement_interval_sec;
    private Integer watering_duration_sec;
    
    // Thresholds
    private Integer temp_min;
    private Integer temp_max;
    
    private Integer hum_min;
    private Integer hum_max;
    
    private Integer soil_min;
    private Integer soil_max;
    
    private Integer light_min;
    private Integer light_max;
}
