package com.smartgarden.repository;

import com.smartgarden.entity.MqttUser;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.Optional;

@Repository
public interface MqttUserRepository extends JpaRepository<MqttUser, Long> {
    Optional<MqttUser> findByUsername(String username);
}
