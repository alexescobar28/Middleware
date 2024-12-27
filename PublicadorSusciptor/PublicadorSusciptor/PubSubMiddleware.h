#pragma once
#include <enet/enet.h>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#ifdef WIN32
#include <winsock2.h>
#endif
#include <enet/enet.h>
class PubSubMiddleware {
private:
    ENetHost* server;
    std::map<std::string, std::vector<ENetPeer*>> topics;
    std::mutex topicsMutex;
    bool isRunning;

public:
    PubSubMiddleware();
    ~PubSubMiddleware();

    bool initialize(uint16_t port);
    void run();
    void stop();

private:
    void handleNewConnection(ENetPeer* peer);
    void handleDisconnection(ENetPeer* peer);
    void handleMessage(ENetPeer* peer, const uint8_t* data, size_t dataLength);
    void publishMessage(const std::string& topic, const uint8_t* data, size_t dataLength);
    void subscribe(ENetPeer* peer, const std::string& topic);
    void unsubscribe(ENetPeer* peer, const std::string& topic);
};

// PubSubMiddleware.cpp
#include "PubSubMiddleware.h"
#include <iostream>

PubSubMiddleware::PubSubMiddleware() : server(nullptr), isRunning(false) {}

PubSubMiddleware::~PubSubMiddleware() {
    stop();
    if (server) {
        enet_host_destroy(server);
    }
    enet_deinitialize();
}

bool PubSubMiddleware::initialize(uint16_t port) {
    if (enet_initialize() != 0) {
        std::cerr << "Failed to initialize ENet." << std::endl;
        return false;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    server = enet_host_create(&address, 32, 2, 0, 0);
    if (!server) {
        std::cerr << "Failed to create ENet server." << std::endl;
        return false;
    }

    return true;
}

void PubSubMiddleware::run() {
    isRunning = true;
    ENetEvent event;

    while (isRunning) {
        while (enet_host_service(server, &event, 100) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                handleNewConnection(event.peer);
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                handleMessage(event.peer, event.packet->data, event.packet->dataLength);
                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                handleDisconnection(event.peer);
                break;

            default:
                break;
            }
        }
    }
}

void PubSubMiddleware::stop() {
    isRunning = false;
}

void PubSubMiddleware::handleNewConnection(ENetPeer* peer) {
    std::cout << "New client connected." << std::endl;
}

void PubSubMiddleware::handleDisconnection(ENetPeer* peer) {
    std::cout << "Client disconnected." << std::endl;

    std::lock_guard<std::mutex> lock(topicsMutex);
    for (auto& topic : topics) {
        auto& subscribers = topic.second;
        subscribers.erase(
            std::remove(subscribers.begin(), subscribers.end(), peer),
            subscribers.end()
        );
    }
}

void PubSubMiddleware::handleMessage(ENetPeer* peer, const uint8_t* data, size_t dataLength) {
    // Formato simple de mensaje: "COMANDO:TOPIC:DATOS"
    std::string message(reinterpret_cast<const char*>(data), dataLength);
    size_t firstColon = message.find(':');
    if (firstColon == std::string::npos) return;

    std::string command = message.substr(0, firstColon);
    size_t secondColon = message.find(':', firstColon + 1);
    if (secondColon == std::string::npos) return;

    std::string topic = message.substr(firstColon + 1, secondColon - firstColon - 1);
    std::string payload = message.substr(secondColon + 1);

    if (command == "PUB") {
        publishMessage(topic, reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
    }
    else if (command == "SUB") {
        subscribe(peer, topic);
    }
    else if (command == "UNSUB") {
        unsubscribe(peer, topic);
    }
}

void PubSubMiddleware::publishMessage(const std::string& topic, const uint8_t* data, size_t dataLength) {
    std::lock_guard<std::mutex> lock(topicsMutex);
    if (topics.find(topic) == topics.end()) return;

    ENetPacket* packet = enet_packet_create(data, dataLength, ENET_PACKET_FLAG_RELIABLE);

    for (auto peer : topics[topic]) {
        enet_peer_send(peer, 0, packet);
    }

    enet_host_flush(server);
}

void PubSubMiddleware::subscribe(ENetPeer* peer, const std::string& topic) {
    std::lock_guard<std::mutex> lock(topicsMutex);
    topics[topic].push_back(peer);
    std::cout << "Client subscribed to topic: " << topic << std::endl;
}

void PubSubMiddleware::unsubscribe(ENetPeer* peer, const std::string& topic) {
    std::lock_guard<std::mutex> lock(topicsMutex);
    if (topics.find(topic) != topics.end()) {
        auto& subscribers = topics[topic];
        subscribers.erase(
            std::remove(subscribers.begin(), subscribers.end(), peer),
            subscribers.end()
        );
    }
    std::cout << "Client unsubscribed from topic: " << topic << std::endl;
}
