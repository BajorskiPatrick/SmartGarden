# syntax=docker/dockerfile:1

FROM maven:3.9-eclipse-temurin-21 AS build
WORKDIR /workspace

COPY pom.xml ./

# Cache dependencies
RUN mvn -q -DskipTests dependency:go-offline

COPY src/ src/

# Build fat jar (Spring Boot)
RUN mvn -q -DskipTests package \
 && (cp target/*-SNAPSHOT.jar target/app.jar || cp target/*.jar target/app.jar)

FROM eclipse-temurin:21-jre
WORKDIR /app

COPY --from=build /workspace/target/app.jar ./app.jar

EXPOSE 8080

ENTRYPOINT ["java","-jar","/app/app.jar"]
