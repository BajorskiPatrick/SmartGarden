package com.smartgarden.entity;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;

@Entity
@Table(name = "mqtt_acls")
@Getter
@Setter
public class MqttAcl {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false)
    private String username;

    @Column(nullable = false)
    private String topic;

    // 1: read, 2: write, 3: readwrite.
    // Usually standard is: 1=read, 2=write, 3=rw.
    // Mosquitto-go-auth matches "rw >= $2".
    // If $2 (acc) is 1 (read), then 1, 2(?), 3 works.
    // Actually the query is: rw >= $2 or rw = 3.
    // If requesting Read(1): 1>=1 (ok), 2>=1 (ok), 3=3 (ok).
    // If requesting Write(2): 1>=2 (fail), 2>=2 (ok), 3=3 (ok).
    @Column(nullable = false)
    private Integer rw;
}
