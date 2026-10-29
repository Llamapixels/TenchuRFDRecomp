#pragma once
#include "workbench.h"
#include <memory>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

// A 3D view drawn off screen with the window's Direct3D 11 device and shown with ImGui::Image.
// Left-handed and y up, like the game's own camera, so nothing appears mirrored.
namespace wb {

ID3D11Device* Device();  // main.cpp
ID3D11DeviceContext* Context();

struct DrawVertex { float x, y, z; uint32_t colour; float u, v; };  // colour: 0xAARRGGBB
struct DrawBatch {
    std::vector<DrawVertex> vertices;  // triangles, three each; or line pairs
    std::shared_ptr<Picture> texture;  // null: vertex colour only
    bool lines = false;
};

struct Camera {
    float target[3]{};
    float yaw = 0.6f, pitch = 0.5f, distance = 6000;
    void Orbit(float dx, float dy);
    void Pan(float dx, float dy);  // in pixels of a view this tall
    void Zoom(float wheel);
    void Frame(const float low[3], const float high[3]);
};

class Viewport {
public:
    Viewport();
    ~Viewport();
    void SetBatches(std::vector<DrawBatch> batches);  // replaces what is drawn
    // draws into an off-screen picture of this size; null when the device cannot
    ImTextureID Render(int width, int height, const Camera& camera);
    // a ray from the camera through a pixel of the last render: origin and direction
    void Ray(float px, float py, float origin[3], float direction[3]) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wb
