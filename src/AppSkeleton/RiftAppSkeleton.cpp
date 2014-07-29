// RiftAppSkeleton.cpp

#ifdef _WIN32
#  define WINDOWS_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include <GL/glew.h>

#include <OVR.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <iostream>

#include "RiftAppSkeleton.h"
#include "ShaderFunctions.h"
#include "GLUtils.h"

RiftAppSkeleton::RiftAppSkeleton()
: m_Hmd(NULL)
, m_panoramaScene()
, m_scenes()
, m_fboScale(1.0f)
, m_presentFbo()
, m_presentDistMeshL()
, m_presentDistMeshR()
, m_chassisYaw(0.0f)
, m_keyboardMove(0.0f)
, m_joystickMove(0.0f)
, m_mouseMove(0.0f)
, m_keyboardYaw(0.0f)
, m_joystickYaw(0.0f)
, m_mouseDeltaYaw(0.0f)
{
    m_chassisPos.x = 0.0f;
    m_chassisPos.y = 1.78f;
    m_chassisPos.z = 0.0f;

    // Add as many scenes here as you like. They will share color and depth buffers,
    // so drawing one after the other should just result in pixel-perfect integration -
    // provided they all do forward rendering. Per-scene deferred render passes will
    // take a little bit more work.
    //m_scenes.push_back(&m_scene);
    m_scenes.push_back(&m_panoramaScene);
}

RiftAppSkeleton::~RiftAppSkeleton()
{
}

ovrSizei RiftAppSkeleton::getHmdResolution() const
{
    if (m_Hmd == NULL)
    {
        ovrSizei empty = {0, 0};
        return empty;
    }
    return m_Hmd->Resolution;
}

ovrVector2i RiftAppSkeleton::getHmdWindowPos() const
{
    if (m_Hmd == NULL)
    {
        ovrVector2i empty = {0, 0};
        return empty;
    }
    return m_Hmd->WindowsPos;
}

void RiftAppSkeleton::initGL()
{
    for (std::vector<IScene*>::iterator it = m_scenes.begin();
        it != m_scenes.end();
        ++it)
    {
        IScene* pScene = *it;
        if (pScene != NULL)
        {
            pScene->initGL();
        }
    }

    m_presentFbo.initProgram("presentfbo");
    _initPresentFbo();
    m_presentDistMeshL.initProgram("presentmesh");
    m_presentDistMeshR.initProgram("presentmesh");
    // Init the present mesh VAO *after* initVR, which creates the mesh

    // sensible initial value?
    allocateFBO(m_renderBuffer, 800, 600);
}

void RiftAppSkeleton::_initPresentFbo()
{
    m_presentFbo.bindVAO();

    const float verts[] = {
        -1, -1,
        1, -1,
        1, 1,
        -1, 1
    };
    const float texs[] = {
        0, 0,
        1, 0,
        1, 1,
        0, 1,
    };

    GLuint vertVbo = 0;
    glGenBuffers(1, &vertVbo);
    m_presentFbo.AddVbo("vPosition", vertVbo);
    glBindBuffer(GL_ARRAY_BUFFER, vertVbo);
    glBufferData(GL_ARRAY_BUFFER, 4*2*sizeof(GLfloat), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(m_presentFbo.GetAttrLoc("vPosition"), 2, GL_FLOAT, GL_FALSE, 0, NULL);

    GLuint texVbo = 0;
    glGenBuffers(1, &texVbo);
    m_presentFbo.AddVbo("vTex", texVbo);
    glBindBuffer(GL_ARRAY_BUFFER, texVbo);
    glBufferData(GL_ARRAY_BUFFER, 4*2*sizeof(GLfloat), texs, GL_STATIC_DRAW);
    glVertexAttribPointer(m_presentFbo.GetAttrLoc("vTex"), 2, GL_FLOAT, GL_FALSE, 0, NULL);

    glEnableVertexAttribArray(m_presentFbo.GetAttrLoc("vPosition"));
    glEnableVertexAttribArray(m_presentFbo.GetAttrLoc("vTex"));

    glUseProgram(m_presentFbo.prog());
    {
        OVR::Matrix4f id = OVR::Matrix4f::Identity();
        glUniformMatrix4fv(m_presentFbo.GetUniLoc("mvmtx"), 1, false, &id.Transposed().M[0][0]);
        glUniformMatrix4fv(m_presentFbo.GetUniLoc("prmtx"), 1, false, &id.Transposed().M[0][0]);
    }
    glUseProgram(0);

    glBindVertexArray(0);
}


///@brief Set this up early so we can get the HMD's display dimensions to create a window.
void RiftAppSkeleton::initHMD()
{
    ovr_Initialize();

    m_Hmd = ovrHmd_Create(0);
    if (m_Hmd == NULL)
    {
        m_Hmd = ovrHmd_CreateDebug(ovrHmd_DK1);
    }
}

void RiftAppSkeleton::initVR()
{
    if (m_Hmd != NULL)
    {
        const ovrBool ret = ovrHmd_ConfigureTracking(m_Hmd,
            ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position,
            ovrTrackingCap_Orientation);
        if (ret == 0)
        {
            std::cerr << "Error calling ovrHmd_ConfigureTracking." << std::endl;
        }
    }

    // The RTSize fields are used by all rendering paths
    ovrSizei l_ClientSize;
    l_ClientSize = getHmdResolution();
    m_Cfg.OGL.Header.RTSize.w = l_ClientSize.w;
    m_Cfg.OGL.Header.RTSize.h = l_ClientSize.h;

    ///@todo Do we need to choose here?
    ConfigureSDKRendering();
    ConfigureClientRendering();

    _initPresentDistMesh(m_presentDistMeshL, 0);
    _initPresentDistMesh(m_presentDistMeshR, 1);
}

void RiftAppSkeleton::_initPresentDistMesh(ShaderWithVariables& shader, int eyeIdx)
{
    // Init left and right VAOs separately
    shader.bindVAO();

    const ovrDistortionMesh& mesh = m_DistMeshes[eyeIdx];
    GLuint vertVbo = 0;
    glGenBuffers(1, &vertVbo);
    shader.AddVbo("vPosition", vertVbo);
    glBindBuffer(GL_ARRAY_BUFFER, vertVbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.VertexCount * sizeof(ovrDistortionVertex), &mesh.pVertexData[0].ScreenPosNDC.x, GL_STATIC_DRAW);

    glVertexAttribPointer(shader.GetAttrLoc("vPosition"), 4, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), NULL);
    glEnableVertexAttribArray(shader.GetAttrLoc("vPosition"));

    const int a_texR = shader.GetAttrLoc("vTexR");
    if (a_texR > -1)
    {
        glVertexAttribPointer(a_texR, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), NULL);
        glEnableVertexAttribArray(a_texR);
    }

    const int a_texG = shader.GetAttrLoc("vTexG");
    if (a_texG > -1)
    {
        glVertexAttribPointer(a_texG, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), NULL);
        glEnableVertexAttribArray(a_texG);
    }

    const int a_texB = shader.GetAttrLoc("vTexB");
    if (a_texB > -1)
    {
        glVertexAttribPointer(a_texB, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), NULL);
        glEnableVertexAttribArray(a_texB);
    }


    GLuint elementVbo = 0;
    glGenBuffers(1, &elementVbo);
    shader.AddVbo("elements", elementVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementVbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.IndexCount * sizeof(GLushort), &mesh.pIndexData[0], GL_STATIC_DRAW);



    glBindVertexArray(0);
}

void RiftAppSkeleton::exitVR()
{
    deallocateFBO(m_renderBuffer);
    ovrHmd_Destroy(m_Hmd);
    ovr_Shutdown();
}

// Active GL context is required for the following
int RiftAppSkeleton::ConfigureSDKRendering()
{
    if (m_Hmd == NULL)
        return 1;
    ovrSizei l_TextureSizeLeft = ovrHmd_GetFovTextureSize(m_Hmd, ovrEye_Left, m_Hmd->DefaultEyeFov[0], 1.0f);
    ovrSizei l_TextureSizeRight = ovrHmd_GetFovTextureSize(m_Hmd, ovrEye_Right, m_Hmd->DefaultEyeFov[1], 1.0f);
    ovrSizei l_TextureSize;
    l_TextureSize.w = l_TextureSizeLeft.w + l_TextureSizeRight.w;
    l_TextureSize.h = (l_TextureSizeLeft.h>l_TextureSizeRight.h ? l_TextureSizeLeft.h : l_TextureSizeRight.h);

    // Oculus Rift eye configurations...
    m_EyeFov[0] = m_Hmd->DefaultEyeFov[0];
    m_EyeFov[1] = m_Hmd->DefaultEyeFov[1];

    m_Cfg.OGL.Header.API = ovrRenderAPI_OpenGL;
    m_Cfg.OGL.Header.Multisample = 0;

    const int l_DistortionCaps = ovrDistortionCap_Chromatic | ovrDistortionCap_TimeWarp;
    ovrHmd_ConfigureRendering(m_Hmd, &m_Cfg.Config, l_DistortionCaps, m_EyeFov, m_EyeRenderDesc);

    // Reset this state before rendering anything else or we get a black screen.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    l_EyeTexture[0].OGL.Header.API = ovrRenderAPI_OpenGL;
    l_EyeTexture[0].OGL.Header.TextureSize.w = l_TextureSize.w;
    l_EyeTexture[0].OGL.Header.TextureSize.h = l_TextureSize.h;
    l_EyeTexture[0].OGL.Header.RenderViewport.Pos.x = 0;
    l_EyeTexture[0].OGL.Header.RenderViewport.Pos.y = 0;
    l_EyeTexture[0].OGL.Header.RenderViewport.Size.w = l_TextureSize.w/2;
    l_EyeTexture[0].OGL.Header.RenderViewport.Size.h = l_TextureSize.h;
    l_EyeTexture[0].OGL.TexId = m_renderBuffer.tex;

    // Right eye the same, except for the x-position in the texture...
    l_EyeTexture[1] = l_EyeTexture[0];
    l_EyeTexture[1].OGL.Header.RenderViewport.Pos.x = (l_TextureSize.w+1) / 2;

    return 0;
}

int RiftAppSkeleton::ConfigureClientRendering()
{
    if (m_Hmd == NULL)
        return 1;

    ovrSizei l_TextureSizeLeft = ovrHmd_GetFovTextureSize(m_Hmd, ovrEye_Left, m_Hmd->DefaultEyeFov[0], 1.0f);
    ovrSizei l_TextureSizeRight = ovrHmd_GetFovTextureSize(m_Hmd, ovrEye_Right, m_Hmd->DefaultEyeFov[1], 1.0f);
    ovrSizei l_TextureSize;
    l_TextureSize.w = l_TextureSizeLeft.w + l_TextureSizeRight.w;
    l_TextureSize.h = std::max(l_TextureSizeLeft.h, l_TextureSizeRight.h);

    // Renderbuffer init - we can use smaller subsets of it easily
    deallocateFBO(m_renderBuffer);
    allocateFBO(m_renderBuffer, l_TextureSize.w, l_TextureSize.h);


    l_EyeTexture[0].OGL.Header.API = ovrRenderAPI_OpenGL;
    l_EyeTexture[0].OGL.Header.TextureSize.w = l_TextureSize.w;
    l_EyeTexture[0].OGL.Header.TextureSize.h = l_TextureSize.h;
    l_EyeTexture[0].OGL.Header.RenderViewport.Pos.x = 0;
    l_EyeTexture[0].OGL.Header.RenderViewport.Pos.y = 0;
    l_EyeTexture[0].OGL.Header.RenderViewport.Size.w = l_TextureSize.w/2;
    l_EyeTexture[0].OGL.Header.RenderViewport.Size.h = l_TextureSize.h;
    l_EyeTexture[0].OGL.TexId = m_renderBuffer.tex;

    // Right eye the same, except for the x-position in the texture...
    l_EyeTexture[1] = l_EyeTexture[0];
    l_EyeTexture[1].OGL.Header.RenderViewport.Pos.x = (l_TextureSize.w+1) / 2;

    m_RenderViewports[0] = l_EyeTexture[0].OGL.Header.RenderViewport;
    m_RenderViewports[1] = l_EyeTexture[1].OGL.Header.RenderViewport;

    const int distortionCaps =
        ovrDistortionCap_Chromatic |
        ovrDistortionCap_TimeWarp |
        ovrDistortionCap_Vignette;

    // Generate distortion mesh for each eye
    for (int eyeNum = 0; eyeNum < 2; eyeNum++)
    {
        // Allocate & generate distortion mesh vertices.
        ovrHmd_CreateDistortionMesh(
            m_Hmd,
            m_EyeRenderDesc[eyeNum].Eye,
            m_EyeRenderDesc[eyeNum].Fov,
            distortionCaps,
            &m_DistMeshes[eyeNum]);

        ovrHmd_GetRenderScaleAndOffset(
            m_EyeRenderDesc[eyeNum].Fov,
            l_TextureSize,
            m_RenderViewports[eyeNum],
            &m_uvScaleOffsetOut[2*eyeNum]);
    }
    return 0;
}

///@brief The HSW will be displayed by default when using SDK rendering.
void RiftAppSkeleton::DismissHealthAndSafetyWarning() const
{
    ovrHSWDisplayState hswDisplayState;
    ovrHmd_GetHSWDisplayState(m_Hmd, &hswDisplayState);
    if (hswDisplayState.Displayed)
    {
        ovrHmd_DismissHSWDisplay(m_Hmd);
    }
}

///@brief The HSW will be displayed by default when using SDK rendering.
/// This function will detect a moderate tap on the Rift via the accelerometer
/// and dismiss the warning.
void RiftAppSkeleton::CheckForTapToDismissHealthAndSafetyWarning() const
{
    // Health and Safety Warning display state.
    ovrHSWDisplayState hswDisplayState;
    ovrHmd_GetHSWDisplayState(m_Hmd, &hswDisplayState);
    if (hswDisplayState.Displayed)
    {
        // Detect a moderate tap on the side of the HMD.
        const ovrTrackingState ts = ovrHmd_GetTrackingState(m_Hmd, ovr_GetTimeInSeconds());
        if (ts.StatusFlags & ovrStatus_OrientationTracked)
        {
            const OVR::Vector3f v(ts.RawSensorData.Accelerometer.x,
                ts.RawSensorData.Accelerometer.y,
                ts.RawSensorData.Accelerometer.z);
            // Arbitrary value and representing moderate tap on the side of the DK2 Rift.
            if (v.LengthSq() > 250.f)
            {
                ovrHmd_DismissHSWDisplay(m_Hmd);
            }
        }
    }
}

void RiftAppSkeleton::_resetGLState() const
{
    glClearDepth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthRangef(0.0f, 1.0f);
    glDepthFunc(GL_LESS);

    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
}

void RiftAppSkeleton::resize(int w, int h)
{
    (void)w;
    (void)h;
    //m_Cfg.OGL.Header.RTSize.w = w;
    //m_Cfg.OGL.Header.RTSize.h = h;

    //const int l_DistortionCaps = ovrDistortionCap_Chromatic | ovrDistortionCap_TimeWarp;
    ///@warning this crashes the app. What are we supposed to do here???
    //ovrHmd_ConfigureRendering(m_Hmd, &m_Cfg.Config, l_DistortionCaps, m_EyeFov, m_EyeRenderDesc);
}

void RiftAppSkeleton::mouseWheel(int x, int y)
{
    (void)x;
    const float curscale = m_fboScale;
    const float incr = 1.05f;

    const int delta = y;
    m_fboScale = curscale * pow(incr, static_cast<float>(delta));
    m_fboScale = std::max(0.05f, m_fboScale);
    m_fboScale = std::min(1.0f, m_fboScale);
}

void RiftAppSkeleton::timestep(float dt)
{
    for (std::vector<IScene*>::iterator it = m_scenes.begin();
        it != m_scenes.end();
        ++it)
    {
        IScene* pScene = *it;
        if (pScene != NULL)
        {
            pScene->timestep(dt);
        }
    }

    glm::vec3 hydraMove = glm::vec3(0.0f, 0.0f, 0.0f);
#ifdef USE_SIXENSE
    const sixenseAllControllerData& state = m_fm.GetCurrentState();
    for (int i = 0; i<2; ++i)
    {
        hydraMove.x += state.controllers[i].joystick_x;

        const FlyingMouse::Hand h = static_cast<FlyingMouse::Hand>(i);
        if (m_fm.IsPressed(h, SIXENSE_BUTTON_JOYSTICK)) ///@note left hand does not work
            hydraMove.y += state.controllers[i].joystick_y;
        else
            hydraMove.z -= state.controllers[i].joystick_y;
    }
#endif

    glm::vec3 move_dt = (m_keyboardMove + m_joystickMove + m_mouseMove + hydraMove) * dt;
    ovrVector3f kbm;
    kbm.x = move_dt.x;
    kbm.y = move_dt.y;
    kbm.z = move_dt.z;

    // Move in the direction the chassis is facing.
    OVR::Vector3f kbmVec = OVR::Matrix4f::RotationY(-m_chassisYaw).Transform(OVR::Vector3f(kbm));
    m_chassisPos.x += kbmVec.x;
    m_chassisPos.y += kbmVec.y;
    m_chassisPos.z += kbmVec.z;

    m_chassisYaw += (m_keyboardYaw + m_joystickYaw + m_mouseDeltaYaw) * dt;
}

void RiftAppSkeleton::_drawSceneMono() const
{
    _resetGLState();
    glClearColor(0.8f, 0.3f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const int w = m_Cfg.OGL.Header.RTSize.w;
    const int h = m_Cfg.OGL.Header.RTSize.h;

    const glm::vec3 EyePos(m_chassisPos.x, m_chassisPos.y, m_chassisPos.z);
    const glm::vec3 LookVec(0.0f, 0.0f, -1.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);

    glm::mat4 lookat = glm::lookAt(EyePos, EyePos + LookVec, up);

    lookat = glm::translate(lookat, EyePos);
    lookat = glm::rotate(lookat, m_chassisYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    lookat = glm::translate(lookat, -EyePos);

    glm::mat4 persp = glm::perspective(
        90.0f,
        static_cast<float>(w)/static_cast<float>(h),
        0.004f,
        500.0f);

    for (std::vector<IScene*>::const_iterator it = m_scenes.begin();
        it != m_scenes.end();
        ++it)
    {
        const IScene* pScene = *it;
        if (pScene != NULL)
        {
            pScene->RenderForOneEye(
                glm::value_ptr(lookat),
                glm::value_ptr(persp),
                NULL);
        }
    }
}

void RiftAppSkeleton::display_raw() const
{
    const int w = m_Cfg.OGL.Header.RTSize.w;
    const int h = m_Cfg.OGL.Header.RTSize.h;
    glViewport(0, 0, w, h);

    _drawSceneMono();
}

void RiftAppSkeleton::display_buffered(bool setViewport) const
{
    bindFBO(m_renderBuffer, m_fboScale);
    _drawSceneMono();
    unbindFBO();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (setViewport)
    {
        const int w = m_Cfg.OGL.Header.RTSize.w;
        const int h = m_Cfg.OGL.Header.RTSize.h;
        glViewport(0, 0, w, h);
    }

    // Present FBO to screen
    const GLuint prog = m_presentFbo.prog();
    glUseProgram(prog);
    m_presentFbo.bindVAO();
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_renderBuffer.tex);
        glUniform1i(m_presentFbo.GetUniLoc("fboTex"), 0);

        // This is the only uniform that changes per-frame
        glUniform1f(m_presentFbo.GetUniLoc("fboScale"), m_fboScale);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

///@todo Even though this function shares most of its code with client rendering,
/// which appears to work fine, it is non-convergable. It appears that the projection
/// matrices for each eye are too far apart? Could be modelview...
void RiftAppSkeleton::display_stereo_undistorted() //const
{
    ovrHmd hmd = m_Hmd;
    if (hmd == NULL)
        return;

    //ovrFrameTiming hmdFrameTiming =
    ovrHmd_BeginFrameTiming(hmd, 0);

    bindFBO(m_renderBuffer, m_fboScale);

    glClearColor(0.8f, 0.3f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (int eyeIndex = 0; eyeIndex < ovrEye_Count; eyeIndex++)
    {
        ovrEyeType eye = hmd->EyeRenderOrder[eyeIndex];
        ovrPosef eyePose = ovrHmd_GetEyePose(hmd, eye);

        const ovrGLTexture& otex = l_EyeTexture[eye];
        const ovrRecti& rvp = otex.OGL.Header.RenderViewport;
        glViewport(
            static_cast<int>(m_fboScale * rvp.Pos.x),
            static_cast<int>(m_fboScale * rvp.Pos.y),
            static_cast<int>(m_fboScale * rvp.Size.w),
            static_cast<int>(m_fboScale * rvp.Size.h));

        OVR::Quatf orientation = OVR::Quatf(eyePose.Orientation);
        OVR::Matrix4f proj = ovrMatrix4f_Projection(
            m_EyeRenderDesc[eye].Fov,
            0.01f, 10000.0f, true);

        //m_EyeRenderDesc[eye].DistortedViewport;
        OVR::Vector3f EyePos = m_chassisPos;
        OVR::Matrix4f view = OVR::Matrix4f(orientation.Inverted())
            * OVR::Matrix4f::RotationY(m_chassisYaw)
            * OVR::Matrix4f::Translation(-EyePos);
        OVR::Matrix4f eyeview = OVR::Matrix4f::Translation(m_EyeRenderDesc[eye].ViewAdjust) * view;

        _resetGLState();

        // Draw the scene for the given eye
        for (std::vector<IScene*>::const_iterator it = m_scenes.begin();
            it != m_scenes.end();
            ++it)
        {
            const IScene* pScene = *it;
            if (pScene != NULL)
            {
                pScene->RenderForOneEye(
                    &eyeview.Transposed().M[0][0],
                    &proj.Transposed().M[0][0],
                    NULL);
            }
        }
    }
    unbindFBO();

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Present FBO to screen
    const GLuint prog = m_presentFbo.prog();
    glUseProgram(prog);
    m_presentFbo.bindVAO();
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_renderBuffer.tex);
        glUniform1i(m_presentFbo.GetUniLoc("fboTex"), 0);

        // This is the only uniform that changes per-frame
        glUniform1f(m_presentFbo.GetUniLoc("fboScale"), m_fboScale);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);

    ovrHmd_EndFrameTiming(hmd);
}

void RiftAppSkeleton::display_sdk() //const
{
    ovrHmd hmd = m_Hmd;
    if (hmd == NULL)
        return;

    //const ovrFrameTiming hmdFrameTiming =
    ovrHmd_BeginFrame(m_Hmd, 0);

    bindFBO(m_renderBuffer);

    glClearColor(0.8f, 0.3f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // For passing to EndFrame once rendering is done
    ovrPosef renderPose[2];
    ovrTexture eyeTexture[2];

    for (int l_EyeIndex=0; l_EyeIndex<ovrEye_Count; l_EyeIndex++)
    {
        ovrEyeType l_Eye = hmd->EyeRenderOrder[l_EyeIndex];
        ovrPosef l_EyePose = ovrHmd_GetEyePose(m_Hmd, l_Eye);

        glViewport(
            l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Pos.x,
            l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Pos.y,
            l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Size.w,
            l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Size.h
            );

        // Get Projection and ModelView matrici from the device...
        OVR::Matrix4f l_ProjectionMatrix = ovrMatrix4f_Projection(
            m_EyeRenderDesc[l_Eye].Fov, 0.3f, 100.0f, true);

        OVR::Vector3f EyePos = m_chassisPos;
        OVR::Quatf l_Orientation = OVR::Quatf(l_EyePose.Orientation);
        OVR::Matrix4f l_ModelViewMatrix = OVR::Matrix4f(l_Orientation.Inverted())
            * OVR::Matrix4f::RotationY(m_chassisYaw)
            * OVR::Matrix4f::Translation(-EyePos);

        _resetGLState();

        // Draw the scene for the given eye
        for (std::vector<IScene*>::const_iterator it = m_scenes.begin();
            it != m_scenes.end();
            ++it)
        {
            const IScene* pScene = *it;
            if (pScene != NULL)
            {
                pScene->RenderForOneEye(
                    &l_ModelViewMatrix.Transposed().M[0][0],
                    &l_ProjectionMatrix.Transposed().M[0][0],
                    NULL);
            }
        }

        renderPose[l_EyeIndex] = l_EyePose;
        eyeTexture[l_EyeIndex] = l_EyeTexture[l_Eye].Texture;
    }
    unbindFBO();

    ovrHmd_EndFrame(m_Hmd, renderPose, eyeTexture);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void RiftAppSkeleton::display_client() //const
{
    ovrHmd hmd = m_Hmd;
    if (hmd == NULL)
        return;

    //ovrFrameTiming hmdFrameTiming =
    ovrHmd_BeginFrameTiming(hmd, 0);

    bindFBO(m_renderBuffer, m_fboScale);

    glClearColor(0.8f, 0.3f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (int eyeIndex = 0; eyeIndex < ovrEye_Count; eyeIndex++)
    {
        ovrEyeType eye = hmd->EyeRenderOrder[eyeIndex];
        ovrPosef eyePose = ovrHmd_GetEyePose(hmd, eye);

        const ovrGLTexture& otex = l_EyeTexture[eye];
        const ovrRecti& rvp = otex.OGL.Header.RenderViewport;
        glViewport(
            static_cast<int>(m_fboScale * rvp.Pos.x),
            static_cast<int>(m_fboScale * rvp.Pos.y),
            static_cast<int>(m_fboScale * rvp.Size.w),
            static_cast<int>(m_fboScale * rvp.Size.h));

        OVR::Quatf orientation = OVR::Quatf(eyePose.Orientation);
        OVR::Matrix4f proj = ovrMatrix4f_Projection(
            m_EyeRenderDesc[eye].Fov,
            0.01f, 10000.0f, true);

        //m_EyeRenderDesc[eye].DistortedViewport;
        OVR::Vector3f EyePos = m_chassisPos;
        OVR::Matrix4f view = OVR::Matrix4f(orientation.Inverted())
            * OVR::Matrix4f::RotationY(m_chassisYaw)
            * OVR::Matrix4f::Translation(-EyePos);
        OVR::Matrix4f eyeview = OVR::Matrix4f::Translation(m_EyeRenderDesc[eye].ViewAdjust) * view;

        _resetGLState();

        // Draw the scene for the given eye
        for (std::vector<IScene*>::const_iterator it = m_scenes.begin();
            it != m_scenes.end();
            ++it)
        {
            const IScene* pScene = *it;
            if (pScene != NULL)
            {
                pScene->RenderForOneEye(
                    &eyeview.Transposed().M[0][0],
                    &proj.Transposed().M[0][0],
                    NULL);
            }
        }
    }
    unbindFBO();


    // Set full viewport...?
    const int w = m_Cfg.OGL.Header.RTSize.w;
    const int h = m_Cfg.OGL.Header.RTSize.h;
    glViewport(0, 0, w, h);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Now draw the distortion mesh...
    for(int eyeNum = 0; eyeNum < 2; eyeNum++)
    {
        const ShaderWithVariables& eyeShader = eyeNum == 0 ?
            m_presentDistMeshL :
            m_presentDistMeshR;
        const GLuint prog = eyeShader.prog();
        glUseProgram(prog);
        //glBindVertexArray(eyeShader.m_vao);
        {
            const ovrDistortionMesh& mesh = m_DistMeshes[eyeNum];
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            const int a_pos =  glGetAttribLocation(prog, "vPosition");
            glVertexAttribPointer(a_pos, 4, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), &mesh.pVertexData[0].ScreenPosNDC.x);
            glEnableVertexAttribArray(a_pos);

            const int a_texR =  glGetAttribLocation(prog, "vTexR");
            if (a_texR > -1)
            {
                glVertexAttribPointer(a_texR, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), &mesh.pVertexData[0].TanEyeAnglesR);
                glEnableVertexAttribArray(a_texR);
            }

            const int a_texG =  glGetAttribLocation(prog, "vTexG");
            if (a_texG > -1)
            {
                glVertexAttribPointer(a_texG, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), &mesh.pVertexData[0].TanEyeAnglesG);
                glEnableVertexAttribArray(a_texG);
            }

            const int a_texB =  glGetAttribLocation(prog, "vTexB");
            if (a_texB > -1)
            {
                glVertexAttribPointer(a_texB, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), &mesh.pVertexData[0].TanEyeAnglesB);
                glEnableVertexAttribArray(a_texB);
            }

            ovrVector2f uvoff =
                m_uvScaleOffsetOut[2*eyeNum + 1];
                //DistortionData.UVScaleOffset[eyeNum][0];
            ovrVector2f uvscale =
                m_uvScaleOffsetOut[2*eyeNum + 0];
                //DistortionData.UVScaleOffset[eyeNum][1];

            glUniform2f(eyeShader.GetUniLoc("EyeToSourceUVOffset"), uvoff.x, uvoff.y);
            glUniform2f(eyeShader.GetUniLoc("EyeToSourceUVScale"), uvscale.x, uvscale.y);


#if 0
            // Setup shader constants
            DistortionData.Shaders->SetUniform2f(
                "EyeToSourceUVScale",
                DistortionData.UVScaleOffset[eyeNum][0].x,
                DistortionData.UVScaleOffset[eyeNum][0].y);
            DistortionData.Shaders->SetUniform2f(
                "EyeToSourceUVOffset",
                DistortionData.UVScaleOffset[eyeNum][1].x,
                DistortionData.UVScaleOffset[eyeNum][1].y);

            if (distortionCaps & ovrDistortionCap_TimeWarp)
            { // TIMEWARP - Additional shader constants required
                ovrMatrix4f timeWarpMatrices[2];
                ovrHmd_GetEyeTimewarpMatrices(HMD, (ovrEyeType)eyeNum, eyeRenderPoses[eyeNum], timeWarpMatrices);
                //WARNING!!! These matrices are transposed in SetUniform4x4f, before being used by the shader.
                DistortionData.Shaders->SetUniform4x4f("EyeRotationStart", Matrix4f(timeWarpMatrices[0]));
                DistortionData.Shaders->SetUniform4x4f("EyeRotationEnd", Matrix4f(timeWarpMatrices[1]));
            }

            // Perform distortion
            pRender->Render(
                &distortionShaderFill,
                DistortionData.MeshVBs[eyeNum],
                DistortionData.MeshIBs[eyeNum]);
#endif

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_renderBuffer.tex);
            glUniform1i(eyeShader.GetUniLoc("fboTex"), 0);

            // This is the only uniform that changes per-frame
            glUniform1f(eyeShader.GetUniLoc("fboScale"), m_fboScale);


            glDrawElements(
                GL_TRIANGLES,
                mesh.IndexCount,
                GL_UNSIGNED_SHORT,
                &mesh.pIndexData[0]);
        }
        glBindVertexArray(0);
        glUseProgram(0);
    }

    ovrHmd_EndFrameTiming(hmd);
}
