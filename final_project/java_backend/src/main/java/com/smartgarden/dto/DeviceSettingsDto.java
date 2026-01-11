package com.smartgarden.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonAlias;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
public class DeviceSettingsDto {
    @JsonProperty("temp_min")
    @JsonAlias("tempMin")
    private Float tempMin;
    @JsonProperty("temp_max")
    @JsonAlias("tempMax")
    private Float tempMax;
    @JsonProperty("hum_min")
    @JsonAlias("humMin")
    private Float humMin;
    @JsonProperty("hum_max")
    @JsonAlias("humMax")
    private Float humMax;
    @JsonProperty("soil_min")
    @JsonAlias("soilMin")
    private Integer soilMin;
    @JsonProperty("soil_max")
    @JsonAlias("soilMax")
    private Integer soilMax;
    @JsonProperty("light_min")
    @JsonAlias("lightMin")
    private Float lightMin;
    @JsonProperty("light_max")
    @JsonAlias("lightMax")
    private Float lightMax;

    @JsonProperty("watering_duration_sec")
    @JsonAlias("wateringDurationSeconds")
    private Integer wateringDurationSeconds;

    @JsonProperty("measurement_interval_sec")
    @JsonAlias("measurementIntervalSeconds")
    private Integer measurementIntervalSeconds;
}
