#include "tankwars/Network.hpp"
#include <SFML/Network.hpp>
#include <chrono>
#include <deque>
#include <thread>
#include <stdexcept>
#if defined(__APPLE__) || defined(__linux__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace tw::net {
namespace {
using Clock = std::chrono::steady_clock;
using Status = sf::Socket::Status;
enum class Message : std::uint8_t { Hello, Welcome, State, Input, Ready, Color, Rematch, Error };
constexpr std::uint32_t magic = 0x54414e4b;
constexpr std::uint32_t extensionMagic = 0x54575831;
constexpr std::uint16_t extensionVersion = 2;
constexpr std::size_t maxFrame = 65536, maxQueued = 262144;

sf::Packet packet(Message type) { sf::Packet p; p << static_cast<std::uint8_t>(type); return p; }
sf::Packet& operator<<(sf::Packet& p, Vec2 v) { return p << v.x << v.y; }
sf::Packet& operator>>(sf::Packet& p, Vec2& v) { return p >> v.x >> v.y; }
void writeConfig(sf::Packet& p, const Config& source, bool laserGuide) {
    auto c = source;
    if (c.titanMode && laserGuide) {
        // Protocol 1 clients approximate a laser with a fast, almost straight aiming guide.
        c.gravity = 50;
        c.weapon.minPower = 350; c.weapon.maxPower = 4000; c.weapon.initialPower = 4000;
        c.weapon.reloadSeconds = c.weapon.laserReload;
    }
    p << c.worldWidth << c.worldHeight << c.groundHeight << c.hillHeight << c.gravity << c.fixedStep
      << c.tankSpeed << c.turretSpeed << c.powerSpeed << c.tankHalfWidth << c.tankHalfHeight << c.spawnInset
      << c.snapshotRate << c.inputRate << c.interpolationDelay << c.inputTimeout
      << c.weapon.minPower << c.weapon.maxPower << c.weapon.initialPower << c.weapon.reloadSeconds
      << c.weapon.directDamage << c.weapon.splashDamage << c.weapon.blastRadius << c.weapon.projectileRadius
      << c.weapon.barrelLength << c.weapon.projectileLifetime << c.port << static_cast<std::uint32_t>(c.playerLimit) << c.teams;
}
bool readConfig(sf::Packet& p, Config& c) {
    c.titanMode = false;
    c.asymmetricLoadouts = false;
    std::uint32_t limit{};
    p >> c.worldWidth >> c.worldHeight >> c.groundHeight >> c.hillHeight >> c.gravity >> c.fixedStep
      >> c.tankSpeed >> c.turretSpeed >> c.powerSpeed >> c.tankHalfWidth >> c.tankHalfHeight >> c.spawnInset
      >> c.snapshotRate >> c.inputRate >> c.interpolationDelay >> c.inputTimeout
      >> c.weapon.minPower >> c.weapon.maxPower >> c.weapon.initialPower >> c.weapon.reloadSeconds
      >> c.weapon.directDamage >> c.weapon.splashDamage >> c.weapon.blastRadius >> c.weapon.projectileRadius
      >> c.weapon.barrelLength >> c.weapon.projectileLifetime >> c.port >> limit >> c.teams;
    c.playerLimit = limit;
    if (!p) return false;
    try { c.validate(); return true; } catch (...) { return false; }
}
void writeLoadoutConfig(sf::Packet& p, const Config& c) {
    // Restore the authoritative ballistics after the legacy laser approximation.
    p << c.gravity << c.weapon.minPower << c.weapon.maxPower << c.weapon.initialPower << c.weapon.reloadSeconds
      << c.asymmetricLoadouts << c.jumpSpeed << c.flightSpeed << c.flightAcceleration << c.descentSpeed
      << c.weapon.napalmReload << c.weapon.napalmDirectDamage << c.weapon.napalmSplashDamage << c.weapon.napalmSpread
      << c.weapon.fireRadius << c.weapon.fireDuration << c.weapon.fireDamagePerSecond
      << c.weapon.burnDuration << c.weapon.burnDamagePerSecond;
}
void readLoadoutConfig(sf::Packet& p, Config& c) {
    p >> c.gravity >> c.weapon.minPower >> c.weapon.maxPower >> c.weapon.initialPower >> c.weapon.reloadSeconds
      >> c.asymmetricLoadouts >> c.jumpSpeed >> c.flightSpeed >> c.flightAcceleration >> c.descentSpeed
      >> c.weapon.napalmReload >> c.weapon.napalmDirectDamage >> c.weapon.napalmSplashDamage >> c.weapon.napalmSpread
      >> c.weapon.fireRadius >> c.weapon.fireDuration >> c.weapon.fireDamagePerSecond
      >> c.weapon.burnDuration >> c.weapon.burnDamagePerSecond;
}
void writeState(sf::Packet& p, const Snapshot& s) {
    p << s.tick << s.round << static_cast<std::uint8_t>(s.phase) << s.winner << s.elapsed;
    p << static_cast<std::uint8_t>(s.players.size());
    for (const auto& v : s.players) p << v.id << v.color << v.team << v.connected << v.ready << v.bot << v.name;
    p << static_cast<std::uint8_t>(s.tanks.size());
    for (const auto& t : s.tanks) p << t.id << t.color << t.team << t.position << t.bodyAngle << t.aim
        << t.power << t.hp << t.reload << t.track << t.recoil;
    p << static_cast<std::uint16_t>(s.projectiles.size());
    for (const auto& v : s.projectiles) p << v.id << v.owner << v.position << v.velocity << v.age;
    p << static_cast<std::uint16_t>(s.events.size());
    for (const auto& e : s.events) p << e.id << static_cast<std::uint8_t>(e.kind == EventKind::NapalmImpact ? EventKind::Impact : e.kind) << e.position << e.player;
    p << extensionMagic << extensionVersion << static_cast<std::uint8_t>(s.tanks.size());
    for (const auto& t : s.tanks) p << t.id << t.titan;
    p << static_cast<std::uint16_t>(s.lasers.size());
    for (const auto& beam : s.lasers) p << beam.id << beam.owner << beam.from << beam.to;
    for (const auto& t : s.tanks) p << static_cast<std::uint8_t>(t.weapon) << t.canFly << t.verticalVelocity << t.burnRemaining;
    for (const auto& v : s.projectiles) p << v.napalm;
    p << static_cast<std::uint16_t>(s.fires.size());
    for (const auto& fire : s.fires) p << fire.id << fire.owner << fire.position << fire.remaining << fire.radius;
    for (const auto& e : s.events) p << static_cast<std::uint8_t>(e.kind);
}
bool readState(sf::Packet& p, Snapshot& s) {
    std::uint8_t phase{}, count{};
    p >> s.tick >> s.round >> phase >> s.winner >> s.elapsed >> count;
    if (!p || phase > 2 || count > maxPlayers || !std::isfinite(s.elapsed)) return false;
    s.phase = static_cast<Phase>(phase);
    s.players.resize(count);
    std::array<bool, maxPlayers> seen{};
    for (auto& v : s.players) {
        p >> v.id >> v.color >> v.team >> v.connected >> v.ready >> v.bot >> v.name;
        if (!p || v.id >= maxPlayers || v.color >= 6 || v.team >= maxPlayers || v.name.size() > 18 || seen[v.id]) return false;
        seen[v.id] = true;
    }
    p >> count;
    if (!p || count > maxPlayers) return false;
    s.tanks.resize(count); seen = {};
    for (auto& t : s.tanks) {
        p >> t.id >> t.color >> t.team >> t.position >> t.bodyAngle >> t.aim >> t.power >> t.hp >> t.reload >> t.track >> t.recoil;
        if (!p || t.id >= maxPlayers || seen[t.id] || t.color >= 6 || t.team >= maxPlayers || !finite(t.position) ||
            !std::isfinite(t.aim) || !std::isfinite(t.bodyAngle) || !std::isfinite(t.power) || !std::isfinite(t.hp) ||
            !std::isfinite(t.reload) || !std::isfinite(t.track) || !std::isfinite(t.recoil)) return false;
        seen[t.id] = true;
    }
    std::uint16_t n{};
    p >> n;
    if (!p || n > 128) return false;
    s.projectiles.resize(n);
    for (auto& v : s.projectiles) {
        p >> v.id >> v.owner >> v.position >> v.velocity >> v.age;
        if (!p || v.owner >= maxPlayers || !finite(v.position) || !finite(v.velocity) || !std::isfinite(v.age)) return false;
    }
    p >> n;
    if (!p || n > 128) return false;
    s.events.resize(n);
    for (auto& e : s.events) {
        std::uint8_t kind{};
        p >> e.id >> kind >> e.position >> e.player;
        if (!p || kind > 2 || !finite(e.position)) return false;
        e.kind = static_cast<EventKind>(kind);
    }
    if (!p) return false;
    if (p.endOfPacket()) return true;
    std::uint32_t marker{};
    std::uint16_t version{};
    p >> marker >> version;
    if (!p) return false;
    if (marker != extensionMagic || version < 1 || version > extensionVersion) return true;
    p >> count;
    if (!p || count != s.tanks.size()) return false;
    seen = {};
    for (unsigned i = 0; i < count; ++i) {
        PlayerId id{}; bool titan{};
        p >> id >> titan;
        if (!p || id >= maxPlayers || seen[id]) return false;
        seen[id] = true;
        auto tank = std::ranges::find(s.tanks,id,&Tank::id);
        if (tank == s.tanks.end()) return false;
        tank->titan = titan;
        tank->weapon = titan ? WeaponKind::Laser : WeaponKind::Cannon;
    }
    p >> n;
    if (!p || n > 64) return false;
    s.lasers.resize(n);
    for (auto& beam : s.lasers) {
        p >> beam.id >> beam.owner >> beam.from >> beam.to;
        if (!p || beam.owner >= maxPlayers || !finite(beam.from) || !finite(beam.to)) return false;
    }
    if (version >= 2) {
        for (auto& tank : s.tanks) {
            std::uint8_t weapon{};
            p >> weapon >> tank.canFly >> tank.verticalVelocity >> tank.burnRemaining;
            if (!p || weapon > static_cast<std::uint8_t>(WeaponKind::Napalm) || !std::isfinite(tank.verticalVelocity) ||
                !std::isfinite(tank.burnRemaining) || tank.burnRemaining < 0 || tank.burnRemaining > 15) return false;
            tank.weapon = static_cast<WeaponKind>(weapon);
        }
        for (auto& projectile : s.projectiles) p >> projectile.napalm;
        p >> n;
        if (!p || n > 64) return false;
        s.fires.resize(n);
        for (auto& fire : s.fires) {
            p >> fire.id >> fire.owner >> fire.position >> fire.remaining >> fire.radius;
            if (!p || fire.owner >= maxPlayers || !finite(fire.position) || !std::isfinite(fire.remaining) ||
                fire.remaining <= 0 || fire.remaining > 15 || !std::isfinite(fire.radius) || fire.radius < 10 || fire.radius > 150) return false;
        }
        for (auto& event : s.events) {
            std::uint8_t kind{}; p >> kind;
            if (!p || kind > static_cast<std::uint8_t>(EventKind::NapalmImpact)) return false;
            event.kind = static_cast<EventKind>(kind);
        }
    }
    return static_cast<bool>(p);
}

struct Channel {
    std::unique_ptr<sf::TcpSocket> socket{std::make_unique<sf::TcpSocket>()};
    struct Frame { std::vector<std::uint8_t> bytes; std::size_t sent{}; };
    std::deque<Frame> outbound;
    std::vector<std::uint8_t> inbound;
    std::deque<sf::Packet> messages;
    bool alive{true};
    Clock::time_point lastReceive{Clock::now()};
    void queue(sf::Packet p) {
        const auto size = p.getDataSize();
        std::size_t queued = 0;
        for (const auto& f : outbound) queued += f.bytes.size() - f.sent;
        if (size > maxFrame || queued + size + 4 > maxQueued) { alive = false; return; }
        Frame f;
        f.bytes.resize(size + 4);
        for (int i = 0; i < 4; ++i) f.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(size >> (24 - 8 * i));
        const auto* bytes = static_cast<const std::uint8_t*>(p.getData());
        std::copy(bytes, bytes + size, f.bytes.begin() + 4);
        outbound.push_back(std::move(f));
    }
    void flush() {
        for (unsigned budget = 0; alive && !outbound.empty() && budget < 32; ++budget) {
            auto& f = outbound.front();
            std::size_t sent = 0;
            const auto status = socket->send(f.bytes.data() + f.sent, f.bytes.size() - f.sent, sent);
            f.sent += sent;
            if (status == Status::Disconnected || status == Status::Error) { alive = false; break; }
            if (f.sent == f.bytes.size()) outbound.pop_front();
            else break;
        }
    }
    void receive() {
        std::array<std::uint8_t, 4096> buffer{};
        for (unsigned budget = 0; alive && budget < 32; ++budget) {
            std::size_t size = 0;
            const auto status = socket->receive(buffer.data(), buffer.size(), size);
            if (status == Status::Disconnected || status == Status::Error) { alive = false; break; }
            if (status == Status::NotReady) break;
            inbound.insert(inbound.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
            lastReceive = Clock::now();
            while (inbound.size() >= 4) {
                std::uint32_t length = 0;
                for (int i = 0; i < 4; ++i) length = (length << 8) | inbound[static_cast<std::size_t>(i)];
                if (length == 0 || length > maxFrame || messages.size() >= 128) { alive = false; return; }
                if (inbound.size() < length + 4) break;
                sf::Packet p; p.append(inbound.data() + 4, length); messages.push_back(std::move(p));
                inbound.erase(inbound.begin(), inbound.begin() + static_cast<std::ptrdiff_t>(length + 4));
            }
            if (inbound.size() > maxFrame + 4) { alive = false; return; }
        }
        if (Clock::now() - lastReceive > std::chrono::seconds(10)) alive = false;
    }
};
}

struct Server::Impl {
    Config config;
    GameSession session;
    sf::TcpListener listener;
    struct Peer {
        Channel channel;
        PlayerId id{noPlayer};
        Clock::time_point lastInput{Clock::now()};
    };
    std::vector<std::unique_ptr<Peer>> peers;
    std::jthread worker;
    explicit Impl(Config c, unsigned short port) : config(c), session(c) {
        if (listener.listen(port) != Status::Done) throw std::runtime_error("Port is busy. Choose another port or close the other host.");
        listener.setBlocking(false);
        worker = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~Impl() { worker.request_stop(); if (worker.joinable()) worker.join(); }
    void process(Peer& peer, sf::Packet& p) {
        std::uint8_t type{}; p >> type;
        if (!p) { peer.channel.alive = false; return; }
        if (peer.id == noPlayer) {
            std::uint32_t sig{}, version{};
            std::string name;
            if (type != static_cast<std::uint8_t>(Message::Hello)) { peer.channel.alive = false; return; }
            p >> sig >> version >> name;
            if (!p || sig != magic || version != protocolVersion || name.size() > 18) { peer.channel.alive = false; return; }
            for (char& ch : name) if (ch < 32 || ch > 126) ch = '?';
            auto id = session.addPlayer(name.empty() ? "PILOT" : name);
            if (!id) {
                auto error = packet(Message::Error); error << std::string("Room is full or a match is already in progress.");
                peer.channel.queue(std::move(error)); peer.channel.flush(); peer.channel.alive = false; return;
            }
            peer.id = *id;
            auto welcome = packet(Message::Welcome); welcome << peer.id;
            writeConfig(welcome,config,!(config.asymmetricLoadouts && peer.id == 0));
            welcome << extensionMagic << extensionVersion << config.titanMode << config.weapon.laserReload;
            writeLoadoutConfig(welcome, config);
            peer.channel.queue(std::move(welcome));
            return;
        }
        switch (static_cast<Message>(type)) {
        case Message::Input: {
            Input in;
            p >> in.move >> in.aim >> in.powerDelta >> in.fire;
            if (p && !p.endOfPacket()) p >> in.lift;
            else in.lift = in.powerDelta;
            if (!p || !std::isfinite(in.move) || !std::isfinite(in.aim) || !std::isfinite(in.powerDelta) || !std::isfinite(in.lift)) { peer.channel.alive = false; return; }
            in.move = std::clamp(in.move, -1.f, 1.f); in.powerDelta = std::clamp(in.powerDelta, -1.f, 1.f);
            in.lift = std::clamp(in.lift, -1.f, 1.f);
            in.aim = std::remainder(in.aim, 2 * pi);
            session.setInput(peer.id, in); peer.lastInput = Clock::now(); break;
        }
        case Message::Ready: { bool value{}; if (p >> value) session.setReady(peer.id, value); break; }
        case Message::Color: { std::uint8_t value{}; if (p >> value) session.setColor(peer.id, value); break; }
        case Message::Rematch: if (peer.id == 0) session.rematch(); break;
        default: peer.channel.alive = false; break;
        }
        if (!p) peer.channel.alive = false;
    }
    void run(std::stop_token stop) {
        auto previous = Clock::now();
        double accumulator = 0, snapshotTimer = 0;
        while (!stop.stop_requested()) {
            const auto now = Clock::now();
            const double dt = std::min(0.1, std::chrono::duration<double>(now - previous).count()); previous = now;
            accumulator += dt; snapshotTimer += dt;
            auto incoming = std::make_unique<Peer>();
            if (listener.accept(*incoming->channel.socket) == Status::Done) {
                incoming->channel.socket->setBlocking(false);
                if (peers.size() < maxPlayers + 2) peers.push_back(std::move(incoming));
            }
            for (auto& peer : peers) {
                peer->channel.receive();
                while (peer->channel.alive && !peer->channel.messages.empty()) {
                    auto p = std::move(peer->channel.messages.front()); peer->channel.messages.pop_front(); process(*peer, p);
                }
                if (peer->id != noPlayer && std::chrono::duration<float>(now - peer->lastInput).count() > config.inputTimeout) {
                    auto state = session.snapshot();
                    const auto* tank = findTank(state, peer->id);
                    session.setInput(peer->id, {0, tank ? tank->aim : 0, 0, false});
                }
            }
            std::erase_if(peers, [&](const auto& peer) {
                if (peer->channel.alive) return false;
                if (peer->id != noPlayer) session.removePlayer(peer->id);
                return true;
            });
            while (accumulator >= config.fixedStep) { session.step(); accumulator -= config.fixedStep; }
            if (snapshotTimer >= 1.0 / config.snapshotRate) {
                snapshotTimer = std::fmod(snapshotTimer, 1.0 / config.snapshotRate);
                auto state = packet(Message::State); writeState(state, session.snapshot());
                for (auto& peer : peers) if (peer->id != noPlayer) peer->channel.queue(state);
            }
            for (auto& peer : peers) peer->channel.flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};
Server::Server(Config config, unsigned short port) : impl_(std::make_unique<Impl>(config, port)) {}
Server::~Server() = default;
unsigned short Server::port() const { return impl_->listener.getLocalPort(); }

struct Client::Impl {
    std::unique_ptr<Channel> channel;
    PlayerId id{noPlayer};
    Config config;
    Snapshot latest;
    struct Timed { Clock::time_point time; Snapshot state; };
    std::deque<Timed> history;
    std::string error;
    void queue(sf::Packet p) { if (channel && channel->alive) { channel->queue(std::move(p)); channel->flush(); } }
};
Client::Client() : impl_(std::make_unique<Impl>()) {}
Client::~Client() = default;
bool Client::connect(const std::string& address, unsigned short port, const std::string& name) {
    disconnect(); impl_->error.clear();
    const auto ip = sf::IpAddress::resolve(address);
    if (!ip) { impl_->error = "Invalid IP address."; return false; }
    auto channel = std::make_unique<Channel>();
    if (channel->socket->connect(*ip, port, sf::seconds(1.5f)) != Status::Done) {
        impl_->error = "Could not connect. Check IP, port, and the host's firewall."; return false;
    }
    channel->socket->setBlocking(false); impl_->channel = std::move(channel);
    auto hello = packet(Message::Hello); hello << magic << protocolVersion << name.substr(0, 18); impl_->queue(std::move(hello));
    return true;
}
void Client::disconnect() { impl_->channel.reset(); impl_->id = noPlayer; impl_->history.clear(); impl_->latest = {}; }
void Client::poll() {
    if (!impl_->channel) return;
    auto& ch = *impl_->channel;
    ch.receive();
    while (!ch.messages.empty()) {
        auto p = std::move(ch.messages.front()); ch.messages.pop_front();
        std::uint8_t type{}; p >> type;
        bool valid = static_cast<bool>(p);
        if (type == static_cast<std::uint8_t>(Message::Welcome)) {
            p >> impl_->id;
            valid = impl_->id < maxPlayers && readConfig(p, impl_->config);
            if (valid && !p.endOfPacket()) {
                std::uint32_t marker{}; std::uint16_t version{};
                p >> marker >> version;
                valid = static_cast<bool>(p);
                if (valid && marker == extensionMagic && version >= 1 && version <= extensionVersion) {
                    p >> impl_->config.titanMode >> impl_->config.weapon.laserReload;
                    if (version >= 2) readLoadoutConfig(p, impl_->config);
                    valid = static_cast<bool>(p);
                    if (valid) try { impl_->config.validate(); } catch (...) { valid = false; }
                }
            }
        } else if (type == static_cast<std::uint8_t>(Message::State) && impl_->id != noPlayer) {
            Snapshot s; valid = readState(p, s);
            if (valid) {
                if (s.round != impl_->latest.round || s.phase != impl_->latest.phase) impl_->history.clear();
                impl_->latest = s; impl_->history.push_back({Clock::now(), std::move(s)});
                while (impl_->history.size() > 32) impl_->history.pop_front();
            }
        } else if (type == static_cast<std::uint8_t>(Message::Error)) {
            p >> impl_->error; ch.alive = false;
        } else valid = false;
        if (!valid) { impl_->error = "Incompatible or invalid server message."; ch.alive = false; break; }
    }
    ch.flush();
    if (!ch.alive && impl_->error.empty()) impl_->error = "Connection lost. Return to the menu to reconnect.";
}
void Client::sendInput(Input in) { auto p = packet(Message::Input); p << in.move << in.aim << in.powerDelta << in.fire << in.lift; impl_->queue(std::move(p)); }
void Client::ready(bool value) { auto p = packet(Message::Ready); p << value; impl_->queue(std::move(p)); }
void Client::color(std::uint8_t value) { auto p = packet(Message::Color); p << value; impl_->queue(std::move(p)); }
void Client::rematch() { impl_->queue(packet(Message::Rematch)); }
bool Client::connected() const { return impl_->channel && impl_->channel->alive; }
bool Client::welcomed() const { return impl_->id != noPlayer; }
PlayerId Client::id() const { return impl_->id; }
const Config& Client::config() const { return impl_->config; }
const Snapshot& Client::latest() const { return impl_->latest; }
const std::string& Client::error() const { return impl_->error; }
Snapshot Client::interpolated() const {
    auto result = impl_->latest;
    if (impl_->history.size() < 2) return result;
    const auto when = Clock::now() - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(impl_->config.interpolationDelay));
    const auto* a = &impl_->history.front();
    const auto* b = a;
    for (const auto& sample : impl_->history) {
        b = &sample;
        if (sample.time >= when) break;
        a = &sample;
    }
    const float span = std::chrono::duration<float>(b->time - a->time).count();
    const float t = span > 0 ? std::clamp(std::chrono::duration<float>(when - a->time).count() / span, 0.f, 1.f) : 1;
    for (auto& tank : result.tanks) {
        const auto* ta = findTank(a->state, tank.id); const auto* tb = findTank(b->state, tank.id);
        if (!ta || !tb) continue;
        tank.position = mix(ta->position, tb->position, t);
        tank.aim = ta->aim + angleDelta(ta->aim, tb->aim) * t;
        tank.bodyAngle = ta->bodyAngle + angleDelta(ta->bodyAngle, tb->bodyAngle) * t;
        tank.track = ta->track + (tb->track - ta->track) * t;
    }
    for (auto& projectile : result.projectiles) {
        auto pa = std::ranges::find(a->state.projectiles, projectile.id, &Projectile::id);
        auto pb = std::ranges::find(b->state.projectiles, projectile.id, &Projectile::id);
        if (pa != a->state.projectiles.end() && pb != b->state.projectiles.end()) projectile.position = mix(pa->position, pb->position, t);
    }
    return result;
}
std::string localAddress() {
#if defined(__APPLE__) || defined(__linux__)
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0) {
        std::unique_ptr<ifaddrs,decltype(&freeifaddrs)> guard(interfaces,freeifaddrs);
        std::string best;
        int bestScore = 0;
        for (auto* item = interfaces; item; item = item->ifa_next) {
            if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
                !(item->ifa_flags & IFF_UP) || (item->ifa_flags & IFF_LOOPBACK)) continue;
            const std::string name = item->ifa_name;
            if (name.starts_with("utun") || name.starts_with("tun") || name.starts_with("tap") || name.starts_with("veth")) continue;
            const auto* addr = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
            const auto ip = ntohl(addr->sin_addr.s_addr);
            const bool privateIp = (ip >> 24) == 10 || (ip >> 16) == 0xc0a8 || (ip >> 20) == 0xac1;
            const bool physical = name.starts_with("en") || name.starts_with("eth") || name.starts_with("wl");
            const int score = (privateIp ? 10 : 0) + (physical ? 20 : 0);
            char buffer[INET_ADDRSTRLEN]{};
            if (score > bestScore && inet_ntop(AF_INET,&addr->sin_addr,buffer,sizeof(buffer))) { best = buffer; bestScore = score; }
        }
        if (!best.empty()) return best;
    }
#endif
    const auto address = sf::IpAddress::getLocalAddress();
    return address ? address->toString() : "127.0.0.1";
}
}
