package com.smartgarden.repository;

import com.smartgarden.entity.MqttAcl;
import org.springframework.data.jpa.repository.JpaRepository;

public interface MqttAclRepository extends JpaRepository<MqttAcl, Long> {
    boolean existsByUsernameAndTopic(String username, String topic);
}
