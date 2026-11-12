#include "tankwars/Network.hpp"
#include <SFML/Network.hpp>
#include <chrono>
#include <atomic>
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
enum class Message : std::uint8_t { Hello, Welcome, State, Input, Ready, Color, Rematch, Error, Weapon, Map };
constexpr std::uint32_t magic = 0x54414e4b;
constexpr std::uint32_t extensionMagic = 0x54575831;
constexpr std::uint16_t extensionVersion = 2;
constexpr std::uint32_t capabilityMagic = 0x54574331, healthCapability = 1, weaponCapability = 2, battlefieldCapability = 4, skyCapability = 8;
constexpr std::uint32_t weaponMagic = 0x41525731;
constexpr std::uint16_t weaponVersion = 1;
constexpr std::uint32_t healthMagic = 0x48505731;
constexpr std::uint16_t healthVersion = 1;
constexpr std::uint32_t battlefieldMagic=0x42465731;
constexpr std::uint16_t battlefieldVersion=1;
constexpr std::uint32_t skyMagic=0x534b5931;
constexpr std::uint16_t skyVersion=1;
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
    c.titanHealth = 100;
    c.laserDamageKnown = false;
    c.weapon.laserDamagePercent = 0;
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
void writeWeaponConfig(sf::Packet& p, const WeaponSpec& w) {
    p << weaponMagic << weaponVersion << w.mortarReload << w.mortarSpeedScale << w.mortarGravityScale << w.mortarBlastRadius << w.mortarDamage
      << w.orbReload << w.orbSpeedScale << w.orbGrowthSeconds << w.orbInitialRadius << w.orbMaxRadius << w.orbLifetime;
}
void writeBattlefieldConfig(sf::Packet& p, const Config& c) {
    p << battlefieldMagic << battlefieldVersion << c.loadoutSelection << c.mapId << c.weapon.ricochetReload << c.weapon.ricochetSpeedScale;
}
void readWeaponConfig(sf::Packet& p, WeaponSpec& w) {
    p >> w.mortarReload >> w.mortarSpeedScale >> w.mortarGravityScale >> w.mortarBlastRadius >> w.mortarDamage
      >> w.orbReload >> w.orbSpeedScale >> w.orbGrowthSeconds >> w.orbInitialRadius >> w.orbMaxRadius >> w.orbLifetime;
}
void writeSkyConfig(sf::Packet& p, const WeaponSpec& w) {
    p << skyMagic << skyVersion << w.flareReload << w.flareSpeedScale << w.flareGravityScale
      << w.skyWarmup << w.skyDuration << w.skyRadius << w.skyDamagePerSecond;
}
void readSkyConfig(sf::Packet& p, WeaponSpec& w) {
    p >> w.flareReload >> w.flareSpeedScale >> w.flareGravityScale
      >> w.skyWarmup >> w.skyDuration >> w.skyRadius >> w.skyDamagePerSecond;
}
EventKind legacyKind(EventKind kind) { return kind>=EventKind::MortarImpact ? EventKind::Impact : kind; }
void writeState(sf::Packet& p, const Snapshot& s, bool nativeHealth) {
    p << s.tick << s.round << static_cast<std::uint8_t>(s.phase) << s.winner << s.elapsed;
    p << static_cast<std::uint8_t>(s.players.size());
    for (const auto& v : s.players) p << v.id << v.color << v.team << v.connected << v.ready << v.bot << v.name;
    p << static_cast<std::uint8_t>(s.tanks.size());
    for (const auto& t : s.tanks) p << t.id << t.color << t.team << t.position << t.bodyAngle << t.aim
        << t.power << (nativeHealth ? t.hp : t.healthRatio()*100.f) << t.reload << t.track << t.recoil;
    p << static_cast<std::uint16_t>(s.projectiles.size());
    for (const auto& v : s.projectiles) p << v.id << v.owner << v.position << v.velocity << v.age;
    p << static_cast<std::uint16_t>(s.events.size());
    for (const auto& e : s.events) p << e.id << static_cast<std::uint8_t>(e.kind >= EventKind::NapalmImpact ? EventKind::Impact : e.kind) << e.position << e.player;
    p << extensionMagic << extensionVersion << static_cast<std::uint8_t>(s.tanks.size());
    // Older HUDs interpret this flag as invulnerability and expect HP on a 0-100 scale.
    for (const auto& t : s.tanks) p << t.id << (t.titan && nativeHealth);
    p << static_cast<std::uint16_t>(s.lasers.size());
    for (const auto& beam : s.lasers) p << beam.id << beam.owner << beam.from << beam.to;
    for (const auto& t : s.tanks) p << static_cast<std::uint8_t>(t.weapon>WeaponKind::Napalm ? WeaponKind::Cannon : t.weapon) << t.canFly << t.verticalVelocity << t.burnRemaining;
    for (const auto& v : s.projectiles) p << v.napalm;
    p << static_cast<std::uint16_t>(s.fires.size());
    for (const auto& fire : s.fires) p << fire.id << fire.owner << fire.position << fire.remaining << fire.radius;
    for (const auto& e : s.events) p << static_cast<std::uint8_t>(legacyKind(e.kind));
    if (nativeHealth) {
        p << healthMagic << healthVersion << static_cast<std::uint8_t>(s.tanks.size());
        for (const auto& tank : s.tanks) p << tank.id << tank.maxHp;
    }
    // Visual identity belongs to the snapshot, regardless of optional Hello capabilities.
    // Older readers ignore this trailing block; newer readers must always receive it.
    {
        p << weaponMagic << weaponVersion;
        for (const auto& tank : s.tanks) p << static_cast<std::uint8_t>(tank.weapon);
        for (const auto& projectile : s.projectiles) p << static_cast<std::uint8_t>(projectile.weapon) << projectile.bounces;
        for (const auto& event : s.events) p << static_cast<std::uint8_t>(event.kind) << event.radius << event.intensity;
    }
    p << battlefieldMagic << battlefieldVersion << s.mapId << s.mapSelected;
    for (const auto& player : s.players) p << static_cast<std::uint8_t>(player.weapon) << player.weaponChosen;
    for (const auto& tank : s.tanks) p << tank.damageRemaining << tank.shieldRemaining << tank.healIndicator << tank.inMud;
    for (const auto& projectile : s.projectiles) p << projectile.damageScale;
    for (const auto& fire : s.fires) p << fire.damageScale;
    p << static_cast<std::uint16_t>(s.terrain.size());
    for (float y : s.terrain) p << y;
    p << static_cast<std::uint8_t>(s.supplies.size());
    for (const auto& drop : s.supplies) p << drop.id << drop.position << drop.velocity << drop.age << static_cast<std::uint8_t>(drop.kind) << drop.landed;
    p << s.plane.active << s.plane.position << s.plane.targetX << s.plane.released;
    p << skyMagic << skyVersion << static_cast<std::uint8_t>(s.skyStrikes.size());
    for (const auto& strike : s.skyStrikes) p << strike.id << strike.owner << strike.position << strike.age << strike.radius << strike.damageScale;
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
    if (!p) return false;
    while (!p.endOfPacket()) {
        p >> marker >> version;
        if (!p) return false;
        if (marker == healthMagic && version == healthVersion) {
            p >> count;
            if (!p || count != s.tanks.size()) return false;
            seen = {};
            for (unsigned i = 0; i < count; ++i) {
                PlayerId id{}; float maximum{};
                p >> id >> maximum;
                if (!p || id >= maxPlayers || seen[id] || !std::isfinite(maximum) || maximum < 1 || maximum > 1000000) return false;
                seen[id] = true;
                auto tank = std::ranges::find(s.tanks,id,&Tank::id);
                if (tank == s.tanks.end() || tank->hp < 0 || tank->hp > maximum) return false;
                tank->maxHp = maximum;
            }
        } else if (marker == weaponMagic && version == weaponVersion) {
            s.detailedWeapons=true;
            for (auto& tank : s.tanks) {
                std::uint8_t kind{}; p >> kind;
                if (!p || kind>static_cast<std::uint8_t>(WeaponKind::Flare)) return false;
                tank.weapon=static_cast<WeaponKind>(kind);
            }
            for (auto& projectile : s.projectiles) {
                std::uint8_t kind{}; p >> kind >> projectile.bounces;
                if (!p || kind>static_cast<std::uint8_t>(WeaponKind::Flare) || projectile.bounces>3) return false;
                projectile.weapon=static_cast<WeaponKind>(kind);
            }
            for (auto& event : s.events) {
                std::uint8_t kind{}; p >> kind >> event.radius >> event.intensity;
                if (!p || kind>static_cast<std::uint8_t>(EventKind::SkyStrike) || !std::isfinite(event.radius) || event.radius<0 || event.radius>400 ||
                    !std::isfinite(event.intensity) || event.intensity<0 || event.intensity>1) return false;
                event.kind=static_cast<EventKind>(kind);
            }
        } else if (marker==battlefieldMagic && version==battlefieldVersion) {
            s.battlefieldData=true;
            p >> s.mapId >> s.mapSelected;
            if (!p || s.mapId>3) return false;
            for (auto& player : s.players) {
                std::uint8_t kind{}; p >> kind >> player.weaponChosen;
                if (!p || kind>static_cast<std::uint8_t>(WeaponKind::Flare)) return false;
                player.weapon=static_cast<WeaponKind>(kind);
            }
            for (auto& tank : s.tanks) {
                p >> tank.damageRemaining >> tank.shieldRemaining >> tank.healIndicator >> tank.inMud;
                if (!p || !std::isfinite(tank.damageRemaining) || tank.damageRemaining<0 || tank.damageRemaining>15 ||
                    !std::isfinite(tank.shieldRemaining) || tank.shieldRemaining<0 || tank.shieldRemaining>10 ||
                    !std::isfinite(tank.healIndicator) || tank.healIndicator<0 || tank.healIndicator>1.5f) return false;
            }
            for (auto& projectile : s.projectiles) { p >> projectile.damageScale; if (!p || (projectile.damageScale!=1 && projectile.damageScale!=1.5f)) return false; }
            for (auto& fire : s.fires) { p >> fire.damageScale; if (!p || (fire.damageScale!=1 && fire.damageScale!=1.5f)) return false; }
            p >> n; if (!p || n>1001) return false;
            s.terrain.resize(n);
            for (auto& y : s.terrain) { p >> y; if (!p || !std::isfinite(y) || y<100 || y>2000) return false; }
            p >> count; if (!p || count>8) return false;
            s.supplies.resize(count);
            for (auto& drop : s.supplies) {
                std::uint8_t kind{}; p >> drop.id >> drop.position >> drop.velocity >> drop.age >> kind >> drop.landed;
                if (!p || !finite(drop.position) || !std::isfinite(drop.velocity) || !std::isfinite(drop.age) || drop.age<0 || drop.age>80 || kind>2) return false;
                drop.kind=static_cast<PickupKind>(kind);
            }
            p >> s.plane.active >> s.plane.position >> s.plane.targetX >> s.plane.released;
            if (!p || !finite(s.plane.position) || !std::isfinite(s.plane.targetX)) return false;
        } else if (marker==skyMagic && version==skyVersion) {
            p >> count; if (!p || count>16) return false;
            s.skyStrikes.resize(count);
            for (auto& strike : s.skyStrikes) {
                p >> strike.id >> strike.owner >> strike.position >> strike.age >> strike.radius >> strike.damageScale;
                if (!p || strike.owner>=maxPlayers || !finite(strike.position) || !std::isfinite(strike.age) || strike.age<0 || strike.age>15 ||
                    !std::isfinite(strike.radius) || strike.radius<20 || strike.radius>200 || (strike.damageScale!=1 && strike.damageScale!=1.5f)) return false;
            }
        } else break;
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
        bool nativeHealth{}, battlefield{}, sky{};
        Clock::time_point lastInput{Clock::now()};
    };
    std::vector<std::unique_ptr<Peer>> peers;
    std::atomic<bool> stopping{false};
    std::thread worker;
    explicit Impl(Config c, unsigned short port) : config(c), session(c) {
        if (listener.listen(port) != Status::Done) throw std::runtime_error("Port is busy. Choose another port or close the other host.");
        listener.setBlocking(false);
        worker = std::thread([this] { run(); });
    }
    ~Impl() { stopping.store(true, std::memory_order_relaxed); if (worker.joinable()) worker.join(); }
    void process(Peer& peer, sf::Packet& p) {
        std::uint8_t type{}; p >> type;
        if (!p) { peer.channel.alive = false; return; }
        if (peer.id == noPlayer) {
            std::uint32_t sig{}, version{};
            std::string name;
            if (type != static_cast<std::uint8_t>(Message::Hello)) { peer.channel.alive = false; return; }
            p >> sig >> version >> name;
            if (!p || sig != magic || version != protocolVersion || name.size() > 18) { peer.channel.alive = false; return; }
            if (!p.endOfPacket()) {
                std::uint32_t marker{}, capabilities{};
                p >> marker >> capabilities;
                if (!p) { peer.channel.alive = false; return; }
                peer.nativeHealth = marker == capabilityMagic && (capabilities & healthCapability) != 0;
                peer.battlefield = marker == capabilityMagic && (capabilities & battlefieldCapability) != 0;
                peer.sky = marker == capabilityMagic && (capabilities & skyCapability) != 0;
            }
            if (config.loadoutSelection && (!peer.battlefield || !peer.sky)) {
                auto error=packet(Message::Error); error << std::string("Update both games to Tank Wars 1.4.0 or newer for solar flares and three ricochets.");
                peer.channel.queue(std::move(error)); peer.channel.flush(); peer.channel.alive=false; return;
            }
            for (char& ch : name) if (ch < 32 || ch > 126) ch = '?';
            auto id = session.addPlayer(name.empty() ? "PILOT" : name);
            if (!id) {
                auto error = packet(Message::Error); error << std::string("Room is full or a match is already in progress.");
                peer.channel.queue(std::move(error)); peer.channel.flush(); peer.channel.alive = false; return;
            }
            peer.id = *id;
            auto welcome = packet(Message::Welcome); welcome << peer.id;
            writeConfig(welcome,config,!(config.asymmetricLoadouts && peer.id == 0));
            welcome << extensionMagic << extensionVersion << (config.titanMode && peer.nativeHealth) << config.weapon.laserReload;
            writeLoadoutConfig(welcome, config);
            if (peer.nativeHealth) welcome << healthMagic << healthVersion << config.titanHealth
                << std::min(10000.f,config.weapon.laserHitDamage(config.titanMode ? config.titanHealth : 100.f)) << config.weapon.laserDamagePercent;
            writeWeaponConfig(welcome,config.weapon);
            writeBattlefieldConfig(welcome,config);
            writeSkyConfig(welcome,config.weapon);
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
        case Message::Weapon: { std::uint8_t value{}; if (p >> value) session.setWeapon(peer.id,static_cast<WeaponKind>(value)); break; }
        case Message::Map: { std::uint8_t value{}; if (p >> value) session.setMap(peer.id,value); break; }
        case Message::Ready: { bool value{}; if (p >> value) session.setReady(peer.id, value); break; }
        case Message::Color: { std::uint8_t value{}; if (p >> value) session.setColor(peer.id, value); break; }
        case Message::Rematch: if (peer.id == 0) session.rematch(); break;
        default: peer.channel.alive = false; break;
        }
        if (!p) peer.channel.alive = false;
    }
    void run() {
        auto previous = Clock::now();
        double accumulator = 0, snapshotTimer = 0;
        while (!stopping.load(std::memory_order_relaxed)) {
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
                const auto snapshot = session.snapshot();
                for (auto& peer : peers) if (peer->id != noPlayer) {
                    auto state = packet(Message::State);
                    writeState(state,snapshot,peer->nativeHealth);
                    peer->channel.queue(std::move(state));
                }
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
    std::vector<GameEvent> pendingEvents;
    std::vector<LaserBeam> pendingBeams;
    std::uint32_t collectedEvent{}, collectedBeam{};
    bool detailedWeaponConfig{};
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
    auto hello = packet(Message::Hello);
    hello << magic << protocolVersion << name.substr(0,18) << capabilityMagic << (healthCapability|weaponCapability|battlefieldCapability|skyCapability);
    impl_->queue(std::move(hello));
    return true;
}
void Client::disconnect() { impl_->channel.reset(); impl_->id = noPlayer; impl_->history.clear(); impl_->latest = {}; impl_->detailedWeaponConfig=false; impl_->pendingEvents.clear(); impl_->pendingBeams.clear(); impl_->collectedEvent=impl_->collectedBeam=0; }
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
                    while (p && !p.endOfPacket()) {
                        p >> marker >> version;
                        if (marker == healthMagic && version == healthVersion) {
                            p >> impl_->config.titanHealth;
                            if (p && !p.endOfPacket()) {
                                p >> impl_->config.weapon.laserDamage;
                                impl_->config.laserDamageKnown = static_cast<bool>(p);
                                if (p && !p.endOfPacket()) p >> impl_->config.weapon.laserDamagePercent;
                            }
                        } else if (marker==weaponMagic && version==weaponVersion) {
                            readWeaponConfig(p,impl_->config.weapon);
                            impl_->detailedWeaponConfig=static_cast<bool>(p);
                        } else if (marker==battlefieldMagic && version==battlefieldVersion) {
                            p >> impl_->config.loadoutSelection >> impl_->config.mapId >> impl_->config.weapon.ricochetReload >> impl_->config.weapon.ricochetSpeedScale;
                        } else if (marker==skyMagic && version==skyVersion) {
                            readSkyConfig(p,impl_->config.weapon);
                        } else break;
                    }
                    valid = static_cast<bool>(p);
                    if (valid) try { impl_->config.validate(); } catch (...) { valid = false; }
                }
            }
        } else if (type == static_cast<std::uint8_t>(Message::State) && impl_->id != noPlayer) {
            Snapshot s; valid = readState(p, s);
            if (valid && impl_->detailedWeaponConfig && !s.detailedWeapons) {
                impl_->error="Missing projectile graphics data. Restart both games using the same updated build.";
                valid=false;
            }
            if (valid && impl_->config.loadoutSelection && !s.battlefieldData) valid=false;
            if (valid) {
                if (s.mapSelected) {
                    impl_->config=MapFactory::preset(impl_->config,s.mapId);
                    const auto expected=static_cast<std::size_t>(std::ceil(impl_->config.worldWidth/4))+1;
                    if (s.terrain.size()!=expected) { impl_->error="Invalid battlefield surface."; ch.alive=false; break; }
                }
                if (s.round != impl_->latest.round || s.phase != impl_->latest.phase) impl_->history.clear();
                if (s.round!=impl_->latest.round) {
                    impl_->pendingEvents.clear(); impl_->pendingBeams.clear(); impl_->collectedEvent=impl_->collectedBeam=0;
                }
                // A slow rendered frame may drain several network snapshots. Preserve every
                // newly received event instead of exposing only the final snapshot's tail.
                for (const auto& event : s.events) if (event.id>impl_->collectedEvent) { impl_->pendingEvents.push_back(event); impl_->collectedEvent=event.id; }
                for (const auto& beam : s.lasers) if (beam.id>impl_->collectedBeam) { impl_->pendingBeams.push_back(beam); impl_->collectedBeam=beam.id; }
                if (impl_->pendingEvents.size()>512) impl_->pendingEvents.erase(impl_->pendingEvents.begin(),impl_->pendingEvents.end()-512);
                if (impl_->pendingBeams.size()>128) impl_->pendingBeams.erase(impl_->pendingBeams.begin(),impl_->pendingBeams.end()-128);
                impl_->latest = s; impl_->history.push_back({Clock::now(), std::move(s)});
                while (impl_->history.size() > 32) impl_->history.pop_front();
            }
        } else if (type == static_cast<std::uint8_t>(Message::Error)) {
            p >> impl_->error; ch.alive = false;
        } else valid = false;
        if (!valid) { if (impl_->error.empty()) impl_->error = "Incompatible or invalid server message."; ch.alive = false; break; }
    }
    ch.flush();
    if (!ch.alive && impl_->error.empty()) impl_->error = "Connection lost. Return to the menu to reconnect.";
}
void Client::sendInput(Input in) { auto p = packet(Message::Input); p << in.move << in.aim << in.powerDelta << in.fire << in.lift; impl_->queue(std::move(p)); }
void Client::ready(bool value) { auto p = packet(Message::Ready); p << value; impl_->queue(std::move(p)); }
void Client::weapon(WeaponKind value) { auto p=packet(Message::Weapon); p << static_cast<std::uint8_t>(value); impl_->queue(std::move(p)); }
void Client::map(std::uint8_t value) { auto p=packet(Message::Map); p << value; impl_->queue(std::move(p)); }
void Client::color(std::uint8_t value) { auto p = packet(Message::Color); p << value; impl_->queue(std::move(p)); }
void Client::rematch() { impl_->queue(packet(Message::Rematch)); }
bool Client::connected() const { return impl_->channel && impl_->channel->alive; }
bool Client::welcomed() const { return impl_->id != noPlayer; }
PlayerId Client::id() const { return impl_->id; }
const Config& Client::config() const { return impl_->config; }
const Snapshot& Client::latest() const { return impl_->latest; }
const std::string& Client::error() const { return impl_->error; }
Snapshot Client::consumeEffects() {
    auto state=impl_->latest;
    state.events=std::move(impl_->pendingEvents); state.lasers=std::move(impl_->pendingBeams);
    impl_->pendingEvents.clear(); impl_->pendingBeams.clear();
    return state;
}
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
