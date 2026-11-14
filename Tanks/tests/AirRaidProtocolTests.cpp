// Exercise the private wire codec and partial-frame queue in their own translation
// unit, without making test-only transport APIs part of the public game interface.
#include "../src/Network.cpp"
#include "tankwars/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void run() {
    using namespace tw;
    net::Channel stalled;
    for (int i=0;i<200;++i) {
        sf::Packet state; state << std::string(10000,'x') << i; stalled.queue(std::move(state),true);
    }
    require(stalled.alive && stalled.outbound.size()==1,"stalled reader retains newest state without queue overflow");
    stalled.outbound.front().sent=3;
    stalled.queue(net::packet(net::Message::State),true);
    stalled.queue(net::packet(net::Message::State),true);
    require(stalled.outbound.size()==2 && stalled.outbound.front().sent==3,"partially sent snapshot is preserved");
    stalled.queue(net::packet(net::Message::Welcome));
    stalled.queue(net::packet(net::Message::State),true);
    require(stalled.outbound.size()==4,"ordered control packets are never replaced");

    auto c=MapFactory::preset({},2); c.titanMode=true;
    World world(c,123); world.reset({{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"JOIN"}});
    while (world.snapshot().elapsed<182.5f) world.step({});
    auto source=world.snapshot(); source.phase=Phase::Playing;
    require(source.plane.bombing && !source.bombs.empty(),"capture live bomber and falling bombs");
    source.events.push_back({8000,EventKind::BombImpact,{c.worldWidth/2,world.map().height(c.worldWidth/2)},noPlayer,c.weapon.mortarBlastRadius*1.25f,1});
    sf::Packet encoded; net::writeState(encoded,source,true);
    Snapshot decoded; require(net::readState(encoded,decoded),"air raid snapshot decodes");
    require(decoded.plane.bombing && decoded.bombs.size()==source.bombs.size() && decoded.events.back().kind==EventKind::BombImpact,"bombs and explosion identity survive wire roundtrip");
    require(decoded.terrain==source.terrain && decoded.bombs.front().position.y==source.bombs.front().position.y,"terrain and bomb position remain exact");

    sf::TcpListener listener; require(listener.listen(0,sf::IpAddress::LocalHost)==sf::Socket::Status::Done,"listen for bomb snapshot transport");
    net::Channel sender,receiver;
    require(sender.socket->connect(sf::IpAddress::LocalHost,listener.getLocalPort(),sf::seconds(1))==sf::Socket::Status::Done,"connect snapshot transport");
    require(listener.accept(*receiver.socket)==sf::Socket::Status::Done,"accept snapshot transport");
    sender.socket->setBlocking(false); receiver.socket->setBlocking(false);
    auto wire=net::packet(net::Message::State); net::writeState(wire,source,true); sender.queue(std::move(wire),true);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (receiver.messages.empty() && std::chrono::steady_clock::now()<deadline) { sender.flush(); receiver.receive(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(!receiver.messages.empty(),"bomb snapshot arrives over TCP");
    auto received=std::move(receiver.messages.front()); std::uint8_t type{}; received >> type;
    Snapshot peer; require(type==static_cast<std::uint8_t>(net::Message::State) && net::readState(received,peer),"TCP framing preserves full air raid snapshot");

    sf::Packet truncated; truncated.append(encoded.getData(),encoded.getDataSize()-1);
    Snapshot invalid; require(!net::readState(truncated,invalid),"truncated bomb data is rejected");
    auto bad=source; bad.bombs.front().velocity.y=std::numeric_limits<float>::quiet_NaN();
    sf::Packet malformed; net::writeState(malformed,bad,true); require(!net::readState(malformed,invalid),"nonfinite bomb physics is rejected");
    sf::RenderTexture a({1600,900}),b({1600,900}); Renderer ra(a,TANKWARS_TEST_ASSETS),rb(b,TANKWARS_TEST_ASSETS);
    ra.settings.shake=rb.settings.shake=false;
    const auto render=[&](Renderer& renderer,sf::RenderTexture& target,const Snapshot& state) {
        target.clear(); renderer.resize(c); renderer.observe(state); renderer.update(0.35f);
        renderer.scenery(c,state.elapsed,false,&state); renderer.battle(c,state,noPlayer,{},false); renderer.hud(c,state,0); target.display();
    };
    render(ra,a,source); render(rb,b,peer);
    auto ia=a.getTexture().copyToImage(),ib=b.getTexture().copyToImage();
    require(std::equal(ia.getPixelsPtr(),ia.getPixelsPtr()+1600*900*4,ib.getPixelsPtr()),"both peers draw identical bombs, blasts and fog");
    require(ia.saveToFile(std::filesystem::temp_directory_path()/"tankwars-1.7.0-air-raid.png"),"save air raid preview");
    std::cout<<"Air raid TCP, codec, backpressure and visual parity checks passed\n";
}
}
int main() { try { run(); return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
