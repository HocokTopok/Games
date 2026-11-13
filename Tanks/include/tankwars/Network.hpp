#pragma once
#include "GameSession.hpp"
#include <memory>
#include <string>

namespace tw::net {
inline constexpr std::uint32_t protocolVersion = 1;
class Server {
public:
    explicit Server(Config config, unsigned short port);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    unsigned short port() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Client {
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    bool connect(const std::string& address, unsigned short port, const std::string& name);
    void disconnect();
    void poll();
    void sendInput(Input input);
    void ready(bool value);
    void weapon(WeaponKind value);
    void map(std::uint8_t value);
    void color(std::uint8_t value);
    void rematch();
    void mode(std::uint8_t teamSize);
    void seat(PlayerId target, std::uint8_t seat);
    void botWeapon(PlayerId target, WeaponKind weapon);
    bool connected() const;
    bool welcomed() const;
    PlayerId id() const;
    const Config& config() const;
    const Snapshot& latest() const;
    Snapshot interpolated() const;
    Snapshot consumeEffects();
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string localAddress();
}
