package com.smartgarden.dto;

import lombok.Data;

@Data
public class DeviceSettingsDto {
    private Float tempMin;
    private Float tempMax;
    private Float humMin;
    private Float humMax;
    private Integer soilMin;
    private Integer soilMax;
    private Float lightMin;
    private Float lightMax;
}
