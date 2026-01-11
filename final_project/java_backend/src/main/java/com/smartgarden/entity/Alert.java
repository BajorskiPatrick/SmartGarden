package com.smartgarden.entity;

import com.fasterxml.jackson.annotation.JsonIgnore;
import jakarta.persistence.*;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import java.time.LocalDateTime;

@Entity
@Table(name = "alerts")
@Data
@NoArgsConstructor
@AllArgsConstructor
public class Alert {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "device_mac", nullable = false)
    @JsonIgnore
    private Device device;

    @Column(nullable = false)
    private LocalDateTime timestamp;

    private String code;

    private String severity; // info, warning, error

    private String subsystem;

    private String message;

    @Column(columnDefinition = "TEXT")
    private String details;
}
