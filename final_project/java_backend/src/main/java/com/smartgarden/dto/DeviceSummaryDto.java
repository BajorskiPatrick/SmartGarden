package com.smartgarden.dto;

import com.smartgarden.entity.Device;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;

import java.time.LocalDateTime;

@Data
@NoArgsConstructor
@EqualsAndHashCode(callSuper = true)
public class DeviceSummaryDto extends Device {
    private Float temperature;
    private Integer soilMoisture;
    private Float humidity;
    private Float lightLux;
    private Boolean waterTankOk;
    private LocalDateTime lastMeasurementTime;
    
    // We can add a constructor to initialize from Device
    public DeviceSummaryDto(Device device) {
        this.setMacAddress(device.getMacAddress());
        this.setUserId(device.getUserId());
        this.setFriendlyName(device.getFriendlyName());
        this.setLastSeen(device.getLastSeen());
        this.setOnline(device.isOnline());
        this.setActiveProfileName(device.getActiveProfileName());
    }
}
