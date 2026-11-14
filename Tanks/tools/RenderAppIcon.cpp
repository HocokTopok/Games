#include "tankwars/Renderer.hpp"
#include <iostream>

int main(int argc,char** argv) {
    try {
        if (argc!=3) throw std::runtime_error("Usage: tankwars_icon_tool ASSETS OUTPUT.png");
        sf::RenderTexture target({1024,1024},sf::ContextSettings{.antiAliasingLevel=8});
        tw::Renderer renderer(target,argv[1]);
        target.clear(sf::Color::Transparent); renderer.applicationIcon(); target.display();
        if (!target.getTexture().copyToImage().saveToFile(argv[2])) throw std::runtime_error("Cannot save app icon");
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
