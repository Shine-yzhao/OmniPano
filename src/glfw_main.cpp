// glfw_main.cpp
// With humongous thanks to cThrough 2014 (Daniel Dekkers)

#include <GL/glew.h>

#if defined(_WIN32)
#  define WINDOWS_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  define GLFW_EXPOSE_NATIVE_WIN32
#  define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__linux__)
#  include <X11/X.h>
#  include <X11/extensions/Xrandr.h>
#  define GLFW_EXPOSE_NATIVE_X11
#  define GLFW_EXPOSE_NATIVE_GLX
#endif

#ifdef _WIN32
# include "win/dirent.h"
#else
# include <dirent.h>
#endif

#include <GLFW/glfw3.h>

#if !defined(__APPLE__)
#  include <GLFW/glfw3native.h>
#endif

#include <glm/gtc/type_ptr.hpp>

#ifdef USE_ANTTWEAKBAR
#  include <AntTweakBar.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sstream>
#include <iostream>

#include "RiftAppSkeleton.h"
#include "RenderingMode.h"
#include "Timer.h"
#include "FPSTimer.h"
#include "Logger.h"

RiftAppSkeleton g_app;
RenderingMode g_renderMode;
Timer g_timer;
FPSTimer g_fps;

int m_keyStates[GLFW_KEY_LAST];

// mouse motion internal state
int oldx, oldy, newx, newy;
int which_button = -1;
int modifier_mode = 0;

ShaderWithVariables g_auxPresent;
GLFWwindow* g_pHMDWindow = NULL;
GLFWwindow* g_AuxWindow = NULL;
int g_auxWindow_w = 1280 / 2;
int g_auxWindow_h = 800;

int g_joystickIdx = -1;

enum panoType
{
    Mono = 0,
    StereoOverUnder = 1,
};

struct pano
{
    std::string filename;
    panoType type;
};

std::vector<pano> g_panos;

#ifdef USE_ANTTWEAKBAR
TwBar* g_pTweakbar = NULL;
#endif

GLFWwindow* initializeAuxiliaryWindow(GLFWwindow* pRiftWindow);
void destroyAuxiliaryWindow(GLFWwindow* pAuxWindow);

static void ErrorCallback(int p_Error, const char* p_Description)
{
    printf("ERROR: %d, %s\n", p_Error, p_Description);
    LOG_INFO("ERROR: %d, %s\n", p_Error, p_Description);
}


void keyboard(GLFWwindow* pWindow, int key, int codes, int action, int mods)
{
    (void)pWindow;
    (void)codes;

    if ((key > -1) && (key <= GLFW_KEY_LAST))
    {
        m_keyStates[key] = action;
        //printf("key ac  %d %d\n", key, action);
    }

    if (action == GLFW_PRESS)
    {
    switch (key)
    {
        default:
            g_app.DismissHealthAndSafetyWarning();
            break;

        case GLFW_KEY_F1:
            if (mods & GLFW_MOD_CONTROL)
            {
                g_renderMode.toggleRenderingTypeReverse();
                LOG_INFO("Called toggleRenderingTypeReverse");
            }
            else
            {
                g_renderMode.toggleRenderingType();
                LOG_INFO("Called toggleRenderingType");
            }
            break;

        case GLFW_KEY_F2:
            g_renderMode.toggleRenderingTypeMono();
            LOG_INFO("Called toggleRenderingTypeMono");
            break;

        case GLFW_KEY_F3:
            g_renderMode.toggleRenderingTypeHMD();
            LOG_INFO("Called toggleRenderingTypeHMD");
            break;

        case GLFW_KEY_F4:
            g_renderMode.toggleRenderingTypeDistortion();
            LOG_INFO("Called toggleRenderingTypeDistortion");
            break;

        case '`':
            if (g_AuxWindow == NULL)
            {
                g_AuxWindow = initializeAuxiliaryWindow(g_pHMDWindow);
            }
            else
            {
                destroyAuxiliaryWindow(g_AuxWindow);
                glfwMakeContextCurrent(g_pHMDWindow);
            }
            break;

        case GLFW_KEY_ESCAPE:
            if (g_AuxWindow == NULL)
            {
                exit(0);
            }
            else
            {
                destroyAuxiliaryWindow(g_AuxWindow);
                glfwMakeContextCurrent(g_pHMDWindow);
            }
            break;
        }
    }

    //g_app.keyboard(key, action, 0,0);

    // Handle keyboard movement(WASD keys)
    glm::vec3 keyboardMove(0.0f, 0.0f, 0.0f);
    if (m_keyStates['W'] != GLFW_RELEASE)
    {
        keyboardMove += glm::vec3(0.0f, 0.0f, -1.0f);
    }
    if (m_keyStates['S'] != GLFW_RELEASE)
    {
        keyboardMove += glm::vec3(0.0f, 0.0f, 1.0f);
    }
    if (m_keyStates['A'] != GLFW_RELEASE)
    {
        keyboardMove += glm::vec3(-1.0f, 0.0f, 0.0f);
    }
    if (m_keyStates['D'] != GLFW_RELEASE)
    {
        keyboardMove += glm::vec3(1.0f, 0.0f, 0.0f);
    }
    if (m_keyStates['Q'] != GLFW_RELEASE)
    {
        keyboardMove += glm::vec3(0.0f, -1.0f, 0.0f);
    }
    if (m_keyStates['E'] != GLFW_RELEASE)
    {
        keyboardMove += glm::vec3(0.0f, 1.0f, 0.0f);
    }

    float mag = 1.0f;
    if (m_keyStates[GLFW_KEY_LEFT_SHIFT ] != GLFW_RELEASE)
        mag *= 0.1f;
    if (m_keyStates[GLFW_KEY_LEFT_CONTROL ] != GLFW_RELEASE)
        mag *= 10.0f;

    // Yaw keys
    g_app.m_keyboardYaw = 0.0f;
    const float dyaw = 0.5f * mag; // radians at 60Hz timestep
    if (m_keyStates['1'] != GLFW_RELEASE)
    {
        g_app.m_keyboardYaw = -dyaw;
    }
    if (m_keyStates['3'] != GLFW_RELEASE)
    {
        g_app.m_keyboardYaw = dyaw;
    }

    g_app.m_keyboardMove = mag * keyboardMove;
}

void joystick()
{
    if (g_joystickIdx == -1)
        return;

    ///@todo Do these calls take time? We can move them out if so
    int joyStick1Present = GL_FALSE;
    joyStick1Present = glfwJoystickPresent(g_joystickIdx);
    if (joyStick1Present != GL_TRUE)
        return;

    // Poll joystick
    int numAxes = 0;
    const float* pAxisStates = glfwGetJoystickAxes(g_joystickIdx, &numAxes);
    if (numAxes < 2)
        return;

    int numButtons = 0;
    const unsigned char* pButtonStates = glfwGetJoystickButtons(g_joystickIdx, &numButtons);
    if (numButtons < 8)
        return;

    glm::vec3 joystickMove(0.0f, 0.0f, 0.0f);
    // Map joystick buttons to move directions
    glm::vec3 moveDirs[8] = {
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
    };

    for (int i=0; i<numButtons; ++i)
    {
        if (pButtonStates[i] == GLFW_PRESS)
        {
            joystickMove += moveDirs[i];
        }
    }

    float mag = 1.0f;
    g_app.m_joystickMove = mag * joystickMove;


    float x_move = pAxisStates[0];
    const float deadzone = 0.2f;
    if (fabs(x_move) < deadzone)
        x_move = 0.0f;
    g_app.m_joystickYaw = 0.5f * static_cast<float>(x_move);
}

void mouseDown(GLFWwindow* pWindow, int button, int action, int mods)
{
    (void)mods;

    double xd, yd;
    glfwGetCursorPos(pWindow, &xd, &yd);
    const int x = static_cast<int>(xd);
    const int y = static_cast<int>(yd);

    which_button = button;
    oldx = newx = x;
    oldy = newy = y;
    if (action == GLFW_RELEASE)
    {
        which_button = -1;
    }
}

void mouseMove(GLFWwindow* pWindow, double xd, double yd)
{
    glfwGetCursorPos(pWindow, &xd, &yd);
    const int x = static_cast<int>(xd);
    const int y = static_cast<int>(yd);

    oldx = newx;
    oldy = newy;
    newx = x;
    newy = y;
    const int mmx = x-oldx;
    const int mmy = y-oldy;

    g_app.m_mouseDeltaYaw = 0.0f;
    g_app.m_mouseMove = glm::vec3(0.0f);

    if (which_button == GLFW_MOUSE_BUTTON_1)
    {
        const float spinMagnitude = 0.05f;
        g_app.m_mouseDeltaYaw += static_cast<float>(mmx) * spinMagnitude;
    }
    else if (which_button == GLFW_MOUSE_BUTTON_2) // Right click
    {
        const float moveMagnitude = 0.5f;
        g_app.m_mouseMove.x += static_cast<float>(mmx) * moveMagnitude;
        g_app.m_mouseMove.z += static_cast<float>(mmy) * moveMagnitude;
    }
}

void mouseWheel(GLFWwindow* pWindow, double x, double y)
{
    (void)pWindow;
    g_app.mouseWheel(static_cast<int>(x), static_cast<int>(y));
}

void resize(GLFWwindow* pWindow, int w, int h)
{
    (void)pWindow;
    g_app.resize(w,h);
}

#ifdef USE_ANTTWEAKBAR
///@param state 0=off, 1=on, -1=adaptive
// Set vsync for both contexts.
static void SetVsync(int state)
{
    // Since AuxWindow holds the tweakbar, this should never be NULL
    if (g_AuxWindow != NULL)
    {
        glfwMakeContextCurrent(g_AuxWindow);
        glfwSwapInterval(state);
    }
    glfwMakeContextCurrent(g_pHMDWindow);
    glfwSwapInterval(state);
}
static void TW_CALL EnableVSyncCB(void*) { SetVsync(1); }
static void TW_CALL DisableVSyncCB(void*) { SetVsync(0); }
static void TW_CALL AdaptiveVSyncCB(void*) { SetVsync(-1); }

static void TW_CALL GetDisplayFPS(void *value, void *clientData)
{
    *(unsigned int *)value = static_cast<unsigned int>(g_fps.GetFPS());
}

static void TW_CALL ChoosePanoCB(void* pArg)
{
    const pano* pPano = reinterpret_cast<const pano*>(pArg);
    if (pPano == NULL)
        return;
    const std::string& name = pPano->filename;

    if (pPano->type == Mono)
    {
        g_app.m_panoramaScene.LoadMonoPanoFromJpeg(name.c_str());
    }
    else
    {
        g_app.m_panoramaScene.LoadStereoPanoFromOverUnderJpeg(name.c_str());
    }
}
void InitializeBar()
{
    ///@note Bad size errors will be thrown if this is not called at init
    TwWindowSize(g_auxWindow_w, g_auxWindow_h);

    // Create a tweak bar
    g_pTweakbar = TwNewBar("TweakBar");

    TwAddVarCB(g_pTweakbar, "Display FPS", TW_TYPE_UINT32, NULL, GetDisplayFPS, NULL, " group='Performance' ");
    TwAddButton(g_pTweakbar, "Disable VSync", DisableVSyncCB, NULL, " group='Performance' ");
    TwAddButton(g_pTweakbar, "Enable VSync", EnableVSyncCB, NULL, " group='Performance' ");
    TwAddButton(g_pTweakbar, "Adaptive VSync", AdaptiveVSyncCB, NULL, " group='Performance' ");

    for (std::vector<pano>::const_iterator it = g_panos.begin();
        it != g_panos.end();
        ++it)
    {
        const pano& p = *it;
        const std::string& name = p.filename;
        TwAddButton(g_pTweakbar, name.c_str(), ChoosePanoCB, (void*)&name, " group='PanoramaScene' ");
    }
}
#endif

void keyboard_Aux(GLFWwindow* pWindow, int key, int codes, int action, int mods)
{
#ifdef USE_ANTTWEAKBAR
    int ant = TwEventKeyGLFW(key, action);
    if (ant != 0)
        return;
#endif
    keyboard(pWindow, key, codes, action, mods);
}

void mouseDown_Aux(GLFWwindow* pWindow, int button, int action, int mods)
{
    (void)pWindow;
    (void)mods;

#ifdef USE_ANTTWEAKBAR
    int ant = TwEventMouseButtonGLFW(button, action);
    if (ant != 0)
        return;
#endif
    mouseDown(pWindow, button, action, mods);
}

void mouseMove_Aux(GLFWwindow* pWindow, double xd, double yd)
{
    (void)pWindow;

#ifdef USE_ANTTWEAKBAR
    int ant = TwEventMousePosGLFW(static_cast<int>(xd), static_cast<int>(yd));
    if (ant != 0)
        return;
#endif
    mouseMove(pWindow, xd, yd);
}

void mouseWheel_Aux(GLFWwindow* pWindow, double x, double y)
{
#ifdef USE_ANTTWEAKBAR
    int ant = TwEventMouseWheelGLFW(static_cast<int>(x));
    if (ant != 0)
        return;
#endif
    mouseWheel(pWindow, x, y);
}

void resize_Aux(GLFWwindow* pWindow, int w, int h)
{
    (void)pWindow;
    g_auxWindow_w = w;
    g_auxWindow_h = h;

#ifdef USE_ANTTWEAKBAR
    TwWindowSize(w, h);
#endif
}


///@brief Scan a directory for jpg files to display and return the list of filenames.
std::vector<pano> GetPanoFileList(const std::string& datadir, panoType type)
{
    std::vector<pano> panoFiles;

    // Thank you Toni Ronkko for the dirent Windows compatibility layer.
    // http://stackoverflow.com/questions/612097/how-can-i-get-a-list-of-files-in-a-directory-using-c-or-c
    DIR* dir;
    struct dirent* ent;
    if ((dir = opendir(datadir.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            std::string filename(ent->d_name);

            // Check file suffixes
            if (filename.length() > 4)
            {
                std::string suffix = filename.substr(filename.length()-4, 4);
                if (
                    !suffix.compare(".jpg") ||
                    !suffix.compare(".JPG")
                    )
                {
                    printf("%s\n", filename.c_str());
                    std::string fullname = datadir;
                    fullname.append(filename);

                    pano p;
                    p.filename = fullname;
                    p.type = type;
                    panoFiles.push_back(p);
                }
            }
        }
        closedir(dir);
    }

    return panoFiles;
}

///@brief Find stereo panoramas in a directory, checking parent directories
/// up to 5 levels up.
std::vector<pano> FindPanos(const std::string dirName, panoType type)
{
    std::string overunderdir = dirName;
    const std::string originalDatadir = overunderdir;
    std::vector<pano> panoFiles = GetPanoFileList(overunderdir, type);
    const int outdepth = 5;
    for (int i=0; i<outdepth; ++i)
    {
        if (panoFiles.empty())
        {
            overunderdir = "../" + overunderdir;
            panoFiles = GetPanoFileList(overunderdir, type);
        }
    }

    return panoFiles;
}



void timestep()
{
    float dt = static_cast<float>(g_timer.seconds());
    g_timer.reset();
    g_app.timestep(dt);
}

void printGLContextInfo(GLFWwindow* pW)
{
    // Print some info about the OpenGL context...
    const int l_Major = glfwGetWindowAttrib(pW, GLFW_CONTEXT_VERSION_MAJOR);
    const int l_Minor = glfwGetWindowAttrib(pW, GLFW_CONTEXT_VERSION_MINOR);
    const int l_Profile = glfwGetWindowAttrib(pW, GLFW_OPENGL_PROFILE);
    if (l_Major >= 3) // Profiles introduced in OpenGL 3.0...
    {
        if (l_Profile == GLFW_OPENGL_COMPAT_PROFILE)
        {
            printf("GLFW_OPENGL_COMPAT_PROFILE\n");
            LOG_INFO("GLFW_OPENGL_COMPAT_PROFILE\n");
        }
        else
        {
            printf("GLFW_OPENGL_CORE_PROFILE\n");
            LOG_INFO("GLFW_OPENGL_CORE_PROFILE\n");
        }
    }
    printf("OpenGL: %d.%d ", l_Major, l_Minor);
    printf("Vendor: %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    printf("Renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    LOG_INFO("OpenGL: %d.%d ", l_Major, l_Minor);
    LOG_INFO("Vendor: %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    LOG_INFO("Renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
}

void initAuxPresentFboShader()
{
    g_auxPresent.initProgram("presentfbo");
    g_auxPresent.bindVAO();

    const float verts[] = {
        -1, -1,
        1, -1,
        1, 1,
        -1, 1
    };
    // The aspect ratio of one eye's view is half side-by-side(portrait), so we can chop
    // the top and bottom parts off to present something closer to landscape.
    const float texs[] = {
        0.0f, 0.25f,
        0.5f, 0.25f,
        0.5f, 0.75f,
        0.0f, 0.75f,
    };

    GLuint vertVbo = 0;
    glGenBuffers(1, &vertVbo);
    g_auxPresent.AddVbo("vPosition", vertVbo);
    glBindBuffer(GL_ARRAY_BUFFER, vertVbo);
    glBufferData(GL_ARRAY_BUFFER, 4*2*sizeof(GLfloat), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(g_auxPresent.GetAttrLoc("vPosition"), 2, GL_FLOAT, GL_FALSE, 0, NULL);

    GLuint texVbo = 0;
    glGenBuffers(1, &texVbo);
    g_auxPresent.AddVbo("vTex", texVbo);
    glBindBuffer(GL_ARRAY_BUFFER, texVbo);
    glBufferData(GL_ARRAY_BUFFER, 4*2*sizeof(GLfloat), texs, GL_STATIC_DRAW);
    glVertexAttribPointer(g_auxPresent.GetAttrLoc("vTex"), 2, GL_FLOAT, GL_FALSE, 0, NULL);

    glEnableVertexAttribArray(g_auxPresent.GetAttrLoc("vPosition"));
    glEnableVertexAttribArray(g_auxPresent.GetAttrLoc("vTex"));

    glUseProgram(g_auxPresent.prog());
    {
        const glm::mat4 id(1.0f);
        glUniformMatrix4fv(g_auxPresent.GetUniLoc("mvmtx"), 1, false, glm::value_ptr(id));
        glUniformMatrix4fv(g_auxPresent.GetUniLoc("prmtx"), 1, false, glm::value_ptr(id));
    }
    glUseProgram(0);

    glBindVertexArray(0);
}

void presentSharedFboTexture()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glViewport(0, 0, g_auxWindow_w, g_auxWindow_h);

    // Present FBO to screen
    const GLuint prog = g_auxPresent.prog();
    glUseProgram(prog);
    g_auxPresent.bindVAO();
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_app.getRenderBufferTex());
        glUniform1i(g_auxPresent.GetUniLoc("fboTex"), 0);

        // This is the only uniform that changes per-frame
        const float fboScale = g_renderMode.outputType == RenderingMode::OVR_SDK ?
            1.0f :
            g_app.GetFboScale();
        glUniform1f(g_auxPresent.GetUniLoc("fboScale"), fboScale);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

void displayToHMD()
{
    switch(g_renderMode.outputType)
    {
    case RenderingMode::Mono_Raw:
        g_app.display_raw();
        glfwSwapBuffers(g_pHMDWindow);
        break;

    case RenderingMode::Mono_Buffered:
        g_app.display_buffered();
        glfwSwapBuffers(g_pHMDWindow);
        break;

    case RenderingMode::SideBySide_Undistorted:
        g_app.display_stereo_undistorted();
        glfwSwapBuffers(g_pHMDWindow);
        break;

    case RenderingMode::OVR_SDK:
        g_app.display_sdk();
        // OVR will do its own swap
        break;

    case RenderingMode::OVR_Client:
        g_app.display_client();
        glfwSwapBuffers(g_pHMDWindow);
        break;

    default:
        LOG_ERROR("Unknown display type: %d", g_renderMode.outputType);
        break;
    }
}

///@return An auxiliary "control view" window to display a monoscopic view of the world
/// that the Rift user is inhabiting(on the primary VR window). Yes, this takes resources
/// away from the VR user's rendering and will lower the rendering throughput(MPx/sec)
/// available to the HMD. It should not negatively impact latency until frame rate drops
/// below the display's refresh rate(which will happen sooner with this extra load, but
/// can be tuned). Pixel fill can be tuned by adjusting the FBO render target size with
/// the mouse wheel, but vertex rate cannot and another render pass adds 50%.
///@todo A more palatable solution is to share the FBO render target between this and
/// the Rift window and just present the left half of it.
GLFWwindow* initializeAuxiliaryWindow(GLFWwindow* pRiftWindow)
{
    ///@todo Set size to half FBO target width
    GLFWwindow* pAuxWindow = glfwCreateWindow(g_auxWindow_w, g_auxWindow_h, "Control Window", NULL, pRiftWindow);
    if (pAuxWindow == NULL)
    {
        return NULL;
    }

    glfwMakeContextCurrent(pAuxWindow);
    {
        // Create context-specific data here
        initAuxPresentFboShader();
    }

    glfwSetMouseButtonCallback(pAuxWindow, mouseDown_Aux);
    glfwSetCursorPosCallback(pAuxWindow, mouseMove_Aux);
    glfwSetScrollCallback(pAuxWindow, mouseWheel_Aux);
    glfwSetKeyCallback(pAuxWindow, keyboard_Aux);
    glfwSetWindowSizeCallback(pAuxWindow, resize_Aux);

    // The window will be shown whether we do this or not (on Windows)...
    glfwShowWindow(pAuxWindow);

    glfwMakeContextCurrent(pRiftWindow);

    return pAuxWindow;
}

void destroyAuxiliaryWindow(GLFWwindow* pAuxWindow)
{
    glfwMakeContextCurrent(pAuxWindow);
    g_auxPresent.destroy();
    glfwDestroyWindow(pAuxWindow);
    g_AuxWindow = NULL;
}

int main(void)
{
    ///@todo Command line options
    g_panos = FindPanos("panos/", StereoOverUnder);
    std::vector<pano> monoPanos = FindPanos("panos2d/", Mono);
    g_panos.insert(g_panos.end(), monoPanos.begin(), monoPanos.end());

    if (g_panos.empty())
    {
        //running = GL_FALSE;
        std::ostringstream oss;
        oss << std::endl
            << "No files found, so we are exiting."
            << std::endl
            << "Throw some stereo panoramas in there!"
            << std::endl;

        std::cerr << oss.str().c_str();
        LOG_INFO("%s", oss.str().c_str());

#ifdef _WIN32
        MessageBox(0, oss.str().c_str(), "No pano files found", 0);
        ///@note This will prevent console output from disappearing with the terminal on Windows,
        /// but makes for an unpleasant experience when run with the Rift on.
        //char ch;
        //std::cin >> ch;
#endif

        exit(0);
    }

    GLFWwindow* l_Window = NULL;

    glfwSetErrorCallback(ErrorCallback);

    if (!glfwInit())
    {
        exit(EXIT_FAILURE);
    }

    // This call assumes the Rift display is in extended mode.
    g_app.initHMD();
    const ovrSizei sz = g_app.getHmdResolution();
    const ovrVector2i pos = g_app.getHmdWindowPos();

    glfwWindowHint(GLFW_SAMPLES, 0);
    l_Window = glfwCreateWindow(sz.w, sz.h, "GLFW Oculus Rift Test", NULL, NULL);
    // According to the OVR SDK 0.3.2 Overview, WindowsPos will be set to (0,0)
    // if not supported. This will also be the case if the Rift DK1 display is
    // cloned/duplicated to the main(convenient for debug).
    if ((pos.x == 0) && (pos.y == 0))
    {
        // Windowed mode
        ///@todo Handle resizes in the app
    }
    else
    {
        glfwSetWindowPos(l_Window, pos.x, pos.y);
        g_renderMode.outputType = RenderingMode::OVR_SDK;
    }

    if (!l_Window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(l_Window);
    glfwSetWindowSizeCallback(l_Window, resize);
    glfwSetMouseButtonCallback(l_Window, mouseDown);
    glfwSetCursorPosCallback(l_Window, mouseMove);
    glfwSetScrollCallback(l_Window, mouseWheel);
    glfwSetKeyCallback(l_Window, keyboard);

    // Don't forget to initialize Glew, turn glewExperimental on to
    // avoid problems fetching function pointers...
    glewExperimental = GL_TRUE;
    const GLenum l_Result = glewInit();
    if (l_Result != GLEW_OK)
    {
        printf("glewInit() error.\n");
        LOG_INFO("glewInit() error.\n");
        exit(EXIT_FAILURE);
    }

    printGLContextInfo(l_Window);

    // Required for SDK rendering (to do the buffer swap on its own)
#if defined(_WIN32)
    g_app.setWindow(glfwGetWin32Window(l_Window));
#elif defined(__linux__)
    g_app.setWindow(glfwGetX11Window(l_Window), glfwGetX11Display());
#endif

    LOG_INFO("Calling initGL...");
    g_app.initGL();
    LOG_INFO("Calling initVR...");
    g_app.initVR();
    LOG_INFO("initVR complete.");

#ifdef USE_ANTTWEAKBAR
    TwInit(TW_OPENGL, NULL);
    InitializeBar();
#endif

    memset(m_keyStates, 0, GLFW_KEY_LAST*sizeof(int));

    // joysticks
    printf("\n\n");
    for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; ++i)
    {
        if (GL_FALSE == glfwJoystickPresent(i))
            continue;

        const char* pJoyName = glfwGetJoystickName(i);
        if (pJoyName == NULL)
            continue;

        printf("Opened Joystick %d: %s\n", i, pJoyName);
        g_joystickIdx = i;
        break;
    }

    glfwMakeContextCurrent(l_Window);
    g_pHMDWindow = l_Window;

    while (!glfwWindowShouldClose(l_Window))
    {
        g_app.CheckForTapToDismissHealthAndSafetyWarning();
        glfwPollEvents();
        joystick();
        timestep();
        g_fps.OnFrame();

        displayToHMD();

#ifndef _LINUX
        // Indicate FPS in window title
        // This is absolute death for performance in Ubuntu Linux 12.04
        {
            std::ostringstream oss;
            oss << "GLFW Oculus Rift Test - "
                << static_cast<int>(g_fps.GetFPS())
                << " fps";
            glfwSetWindowTitle(l_Window, oss.str().c_str());
            if (g_AuxWindow != NULL)
                glfwSetWindowTitle(g_AuxWindow, oss.str().c_str());
        }
#endif

        // Optionally display to auxiliary mono view
        if (g_AuxWindow != NULL)
        {
            glfwMakeContextCurrent(g_AuxWindow);
            glClearColor(0,1,0,1);
            glClear(GL_COLOR_BUFFER_BIT);

            ///@note VAOs *cannot* be shared between contexts.
            ///@note GLFW windows are inextricably tied to their own unique context.
            /// For these two reasons, calling draw a third time for the auxiliary window
            /// is not possible. Furthermore, it is not strictly desirable for the extra
            /// rendering cost.
            /// Instead, we share the render target texture from the stereo render and present
            /// just the left eye to the aux window.
            presentSharedFboTexture();

#ifdef USE_ANTTWEAKBAR
            TwRefreshBar(g_pTweakbar);
            TwDraw(); ///@todo Should this go first? Will it write to a depth buffer?
#endif

            glfwSwapBuffers(g_AuxWindow);

            if (glfwWindowShouldClose(g_AuxWindow))
            {
                destroyAuxiliaryWindow(g_AuxWindow);
            }

            // Set context to Rift window when done
            glfwMakeContextCurrent(l_Window);
        }
    }

    g_app.exitVR();
    glfwDestroyWindow(l_Window);
    glfwTerminate();

    exit(EXIT_SUCCESS);
}
