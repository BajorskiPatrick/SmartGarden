package com.smartgarden.entity;

import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import jakarta.persistence.Column;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import java.time.LocalDateTime;

@Entity
@Table(name = "devices")
@Data
@NoArgsConstructor
@AllArgsConstructor
public class Device {

    @Id
    @Column(name = "mac_address", length = 12)
    private String macAddress;

    @Column(name = "user_id", nullable = false)
    private String userId;

    private String friendlyName;

    private LocalDateTime lastSeen;

    private boolean online;

    @Column(name = "active_profile_name")
    private String activeProfileName;
}
