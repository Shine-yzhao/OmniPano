// RiftAppSkeleton.h

#pragma once

#ifdef __APPLE__
#include "opengl/gl.h"
#endif

#ifdef _WIN32
#  define WINDOWS_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include <vector>

#include <OVR_CAPI.h>
#include <OVR_CAPI_GL.h>

#include "FBO.h"
#include "Scene.h"
#include "StereoPanoramaScene.h"

///@brief Encapsulates as much of the VR viewer state as possible,
/// pushing all viewer-independent stuff to Scene.
class RiftAppSkeleton
{
public:
    RiftAppSkeleton();
    virtual ~RiftAppSkeleton();

    void initGL();
    void initHMD();
    void initVR();
    void exitVR();

    ovrSizei getHmdResolution() const;
    ovrVector2i getHmdWindowPos() const;
    GLuint getRenderBufferTex() const { return m_renderBuffer.tex; }
    float GetFboScale() const { return m_fboScale; }

    int ConfigureSDKRendering();
    int ConfigureClientRendering();

#if defined(OVR_OS_WIN32)
    void setWindow(HWND w) { m_Cfg.OGL.Window = w; }
#elif defined(OVR_OS_LINUX)
    void setWindow(Window Win, Display* Disp)
    {
        m_Cfg.OGL.Win = Win;
        m_Cfg.OGL.Disp = Disp;
    }
#endif

    void DismissHealthAndSafetyWarning() const;
    void CheckForTapToDismissHealthAndSafetyWarning() const;

    void display_raw() const;
    void display_buffered(bool setViewport=true) const;
    void display_stereo_undistorted();// const;
    void display_sdk();// const;
    void display_client();// const;
    void timestep(float dt);

    void resize(int w, int h);
    void mouseWheel(int x, int y);

protected:
    void _initPresentFbo();
    void _initPresentDistMesh(ShaderWithVariables& shader, int eyeIdx);
    void _resetGLState() const;
    void _drawSceneMono() const;

    ovrHmd m_Hmd;
    ovrFovPort m_EyeFov[2];
    ovrGLConfig m_Cfg;
    ovrEyeRenderDesc m_EyeRenderDesc[2];
    ovrGLTexture l_EyeTexture[2];

    // For client rendering
    ovrRecti m_RenderViewports[2];
    ovrVector2f m_uvScaleOffsetOut[4];
    ovrDistortionMesh m_DistMeshes[2];

public:
    // This public section is for exposing state variables to AntTweakBar
    Scene m_scene;
    StereoPanoramaScene m_panoramaScene;

protected:
    std::vector<IScene*> m_scenes;
    FBO m_renderBuffer;
    float m_fboScale;
    ShaderWithVariables m_presentFbo;
    ShaderWithVariables m_presentDistMeshL;
    ShaderWithVariables m_presentDistMeshR;

    ovrVector3f m_chassisPos;
    float m_chassisYaw;

public:
    glm::vec3 m_keyboardMove;
    glm::vec3 m_joystickMove;
    glm::vec3 m_mouseMove;
    float m_keyboardYaw;
    float m_joystickYaw;
    float m_mouseDeltaYaw;

private: // Disallow copy ctor and assignment operator
    RiftAppSkeleton(const RiftAppSkeleton&);
    RiftAppSkeleton& operator=(const RiftAppSkeleton&);
};
