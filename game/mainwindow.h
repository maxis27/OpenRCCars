#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Event>
#include <Tempest/Fence>
#include <Tempest/Swapchain>
#include <Tempest/Window>
#include <Tempest/ZBuffer>

#include <array>
#include <memory>

#include "camera.h"
#include "resources.h"
#include "world/level.h"

class MainWindow : public Tempest::Window {
  public:
    MainWindow(Tempest::Device& device, Resources& res, const std::string& level);
    ~MainWindow() override;

  private:
    enum { MaxFramesInFlight = 2 };

    void render() override;
    void resizeEvent    (Tempest::SizeEvent&  e) override;
    void keyDownEvent   (Tempest::KeyEvent&   e) override;
    void keyUpEvent     (Tempest::KeyEvent&   e) override;
    void mouseDownEvent (Tempest::MouseEvent& e) override;
    void mouseDragEvent (Tempest::MouseEvent& e) override;
    void mouseWheelEvent(Tempest::MouseEvent& e) override;

    void resetSwapchain();
    void tick(float dt);
    void loadLevel(const std::string& name);

    Tempest::Device&        device;
    Resources&              resources;
    Tempest::Swapchain      swapchain;
    Tempest::ZBuffer        zbuffer;

    std::unique_ptr<Level>  level;
    Camera                  camera;

    Tempest::CommandBuffer  commands[MaxFramesInFlight];
    Tempest::Fence          fence[MaxFramesInFlight];
    uint8_t                 frameId = 0;

    std::array<bool,256>    keys = {};
    Tempest::Point          mousePos;
    float                   speed    = 8.f; // m/s
    uint64_t                lastTick = 0;
    float                   fpsTime  = 0;
    uint32_t                fpsCount = 0;
  };
