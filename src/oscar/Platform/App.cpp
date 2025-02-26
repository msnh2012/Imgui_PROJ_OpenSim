#include "App.hpp"

#include "oscar/Bindings/SDL2Helpers.hpp"
#include "oscar/Bindings/ImGuiHelpers.hpp"
#include "oscar/Graphics/GraphicsContext.hpp"
#include "oscar/Platform/AppClock.hpp"
#include "oscar/Platform/Config.hpp"
#include "oscar/Platform/Log.hpp"
#include "oscar/Platform/os.hpp"
#include "oscar/Platform/RecentFile.hpp"
#include "oscar/Screens/Screen.hpp"
#include "oscar/Utils/Algorithms.hpp"
#include "oscar/Utils/FilesystemHelpers.hpp"
#include "oscar/Utils/Perf.hpp"
#include "oscar/Utils/ScopeGuard.hpp"
#include "oscar/Utils/SynchronizedValue.hpp"
#include "OscarConfiguration.hpp"

#include <glm/vec2.hpp>
#include <IconsFontAwesome5.h>
#include <imgui.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <SDL.h>
#include <SDL_error.h>
#include <SDL_keyboard.h>
#include <SDL_mouse.h>
#include <SDL_stdinc.h>
#include <SDL_timer.h>
#include <SDL_video.h>

#include <algorithm>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

// handy macro for calling SDL_GL_SetAttribute with error checking
#define OSC_SDL_GL_SetAttribute_CHECK(attr, value)                                                                     \
    {                                                                                                                  \
        int rv = SDL_GL_SetAttribute((attr), (value));                                                                 \
        if (rv != 0) {                                                                                                 \
            throw std::runtime_error{std::string{"SDL_GL_SetAttribute failed when setting " #attr " = " #value " : "} +            \
                                     SDL_GetError()};                                                                  \
        }                                                                                                              \
    }

static auto constexpr c_IconRanges = osc::MakeArray<ImWchar>(ICON_MIN_FA, ICON_MAX_FA, 0);

namespace
{
    // install backtrace dumper
    //
    // useful if the application fails in prod: can provide some basic backtrace
    // info that users can paste into an issue or something, which is *a lot* more
    // information than "yeah, it's broke"
    bool EnsureBacktraceHandlerEnabled()
    {
        static bool const s_BacktraceEnabled = []()
        {
            osc::log::info("enabling backtrace handler");
            osc::InstallBacktraceHandler();
            return true;
        }();
        (void)s_BacktraceEnabled;

        return s_BacktraceEnabled;
    }

    // returns a resource from the config-provided `resources/` dir
    std::filesystem::path GetResource(osc::Config const& c, std::string_view p)
    {
        return c.getResourceDir() / p;
    }

    // initialize the main application window
    sdl::Window CreateMainAppWindow()
    {
        osc::log::info("initializing main application window");

        OSC_SDL_GL_SetAttribute_CHECK(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        OSC_SDL_GL_SetAttribute_CHECK(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        OSC_SDL_GL_SetAttribute_CHECK(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        OSC_SDL_GL_SetAttribute_CHECK(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

        // careful about setting resolution, position, etc. - some people have *very* shitty
        // screens on their laptop (e.g. ultrawide, sub-HD, minus space for the start bar, can
        // be <700 px high)
        int constexpr x = SDL_WINDOWPOS_CENTERED;
        int constexpr y = SDL_WINDOWPOS_CENTERED;
        int constexpr width = 800;
        int constexpr height = 600;
        Uint32 constexpr flags =
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;

        return sdl::CreateWindoww(OSC_APPNAME_STRING, x, y, width, height, flags);
    }

    // returns refresh rate of highest refresh rate display on the computer
    int GetHighestRefreshRateDisplay()
    {
        int numDisplays = SDL_GetNumVideoDisplays();

        if (numDisplays < 1)
        {
            return 60;  // this should be impossible but, you know, coding.
        }

        int highestRefreshRate = 30;
        SDL_DisplayMode modeStruct{};
        for (int display = 0; display < numDisplays; ++display)
        {
            int numModes = SDL_GetNumDisplayModes(display);
            for (int mode = 0; mode < numModes; ++mode)
            {
                SDL_GetDisplayMode(display, mode, &modeStruct);
                highestRefreshRate = std::max(highestRefreshRate, modeStruct.refresh_rate);
            }
        }
        return highestRefreshRate;
    }

    // load the "recent files" file that osc persists to disk
    std::vector<osc::RecentFile> LoadRecentFilesFile(std::filesystem::path const& p)
    {
        std::ifstream fd{p, std::ios::in};

        if (!fd)
        {
            // do not throw, because it probably shouldn't crash the application if this
            // is an issue
            osc::log::error("%s: could not be opened for reading: cannot load recent files list", p.string().c_str());
            return {};
        }

        std::vector<osc::RecentFile> rv;
        std::string line;

        while (std::getline(fd, line))
        {
            std::istringstream ss{line};

            // read line content
            uint64_t timestamp;
            std::filesystem::path path;
            ss >> timestamp;
            ss >> path;

            // calc tertiary data
            bool exists = std::filesystem::exists(path);
            std::chrono::seconds timestampSecs{timestamp};

            rv.push_back(osc::RecentFile{exists, std::move(timestampSecs), std::move(path)});
        }

        return rv;
    }

    // returns the filesystem path to the "recent files" file
    std::filesystem::path GetRecentFilesFilePath()
    {
        return osc::GetUserDataDir() / "recent_files.txt";
    }

    // returns a unix timestamp in seconds since the epoch
    std::chrono::seconds GetCurrentTimeAsUnixTimestamp()
    {
        return std::chrono::seconds(std::time(nullptr));
    }

    osc::AppClock::duration ConvertPerfTicksToFClockDuration(Uint64 ticks, Uint64 frequency)
    {
        double dticks = static_cast<double>(ticks);
        double fq = static_cast<double>(frequency);
        float dur = static_cast<float>(dticks/fq);
        return osc::AppClock::duration{dur};
    }

    osc::AppClock::time_point ConvertPerfCounterToFClock(Uint64 ticks, Uint64 frequency)
    {
        return osc::AppClock::time_point{ConvertPerfTicksToFClockDuration(ticks, frequency)};
    }
}

namespace
{
    // an "active" request for an annotated screenshot
    //
    // has a data depencency on the backend first providing a "raw" image, which is then
    // tagged with annotations
    struct AnnotatedScreenshotRequest final {

        AnnotatedScreenshotRequest(
            uint64_t frameRequested_,
            std::future<osc::Image> underlyingFuture_) :

            frameRequested{std::move(frameRequested_)},
            underlyingScreenshotFuture{std::move(underlyingFuture_)}
        {
        }

        // the frame on which the screenshot was requested
        uint64_t frameRequested;

        // underlying (to-be-waited-on) future for the screenshot
        std::future<osc::Image> underlyingScreenshotFuture;

        // our promise to the caller, who is waiting for an annotated image
        std::promise<osc::AnnotatedImage> resultPromise;

        // annotations made during the requested frame (if any)
        std::vector<osc::ImageAnnotation> annotations;
    };

    // wrapper class for storing std::type_info as a hashable type
    class TypeInfoReference {
    public:
        TypeInfoReference(std::type_info const& typeInfo) noexcept :
            m_TypeInfo{&typeInfo}
        {
        }

        std::type_info const& get() const
        {
            return *m_TypeInfo;
        }
    private:
        std::type_info const* m_TypeInfo;
    };

    bool operator==(TypeInfoReference const& a, TypeInfoReference const& b) noexcept
    {
        return a.get() == b.get();
    }
}

namespace std
{
    template<>
    struct hash<TypeInfoReference> final {
        size_t operator()(TypeInfoReference const& ref) const noexcept
        {
            return ref.get().hash_code();
        }
    };
}

// main application state
//
// this is what "booting the application" actually initializes
class osc::App::Impl final {
public:
    void show(std::unique_ptr<Screen> s)
    {
        log::info("showing screen %s", s->name());

        if (m_CurrentScreen)
        {
            throw std::runtime_error{"tried to call App::show when a screen is already being shown: you should use `requestTransition` instead"};
        }

        m_CurrentScreen = std::move(s);
        m_NextScreen.reset();

        // ensure retained screens are destroyed when exiting this guarded path
        //
        // this means callers can call .show multiple times on the same app
        OSC_SCOPE_GUARD({ m_CurrentScreen.reset(); });
        OSC_SCOPE_GUARD({ m_NextScreen.reset(); });

        mainLoopUnguarded();
    }

    void requestTransition(std::unique_ptr<Screen> s)
    {
        m_NextScreen = std::move(s);
    }

    bool isTransitionRequested() const
    {
        return m_NextScreen != nullptr;
    }

    void requestQuit()
    {
        m_QuitRequested = true;
    }

    glm::ivec2 idims() const
    {
        return sdl::GetWindowSize(m_MainWindow.get());
    }

    glm::vec2 dims() const
    {
        return glm::vec2{sdl::GetWindowSize(m_MainWindow.get())};
    }

    float aspectRatio() const
    {
        glm::vec2 v = dims();
        return v.x / v.y;
    }

    void setShowCursor(bool v)
    {
        SDL_ShowCursor(v ? SDL_ENABLE : SDL_DISABLE);
        SDL_SetWindowGrab(m_MainWindow.get(), v ? SDL_FALSE : SDL_TRUE);
    }

    bool isWindowFocused() const
    {
        return SDL_GetWindowFlags(m_MainWindow.get()) & SDL_WINDOW_INPUT_FOCUS;
    }

    void makeFullscreen()
    {
        SDL_SetWindowFullscreen(m_MainWindow.get(), SDL_WINDOW_FULLSCREEN);
    }

    void makeWindowedFullscreen()
    {
        SDL_SetWindowFullscreen(m_MainWindow.get(), SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    void makeWindowed()
    {
        SDL_SetWindowFullscreen(m_MainWindow.get(), 0);
    }

    int32_t getMSXAASamplesRecommended() const
    {
        return m_CurrentMSXAASamples;
    }

    void setMSXAASamplesRecommended(int32_t s)
    {
        if (s <= 0)
        {
            throw std::runtime_error{"tried to set number of samples to <= 0"};
        }

        if (s > getMSXAASamplesMax())
        {
            throw std::runtime_error{"tried to set number of multisamples higher than supported by hardware"};
        }

        if (NumBitsSetIn(s) != 1)
        {
            throw std::runtime_error{"tried to set number of multisamples to an invalid value. Must be 1, or a multiple of 2 (1x, 2x, 4x, 8x...)"};
        }

        m_CurrentMSXAASamples = s;
    }

    int32_t getMSXAASamplesMax() const
    {
        return m_GraphicsContext.getMaxMSXAASamples();
    }

    bool isInDebugMode() const
    {
        return m_GraphicsContext.isInDebugMode();
    }

    void enableDebugMode()
    {
        m_GraphicsContext.enableDebugMode();
    }

    void disableDebugMode()
    {
        m_GraphicsContext.disableDebugMode();
    }

    bool isVsyncEnabled() const
    {
        return m_GraphicsContext.isVsyncEnabled();
    }

    void setVsync(bool v)
    {
        if (v)
        {
            m_GraphicsContext.enableVsync();
        }
        else
        {
            m_GraphicsContext.disableVsync();
        }
    }

    void enableVsync()
    {
        m_GraphicsContext.enableVsync();
    }

    void disableVsync()
    {
        m_GraphicsContext.disableVsync();
    }

    void addFrameAnnotation(std::string_view label, Rect screenRect)
    {
        m_FrameAnnotations.push_back(ImageAnnotation{std::string{label}, std::move(screenRect)});
    }

    std::future<Image> requestScreenshot()
    {
        return m_GraphicsContext.requestScreenshot();
    }

    std::future<AnnotatedImage> requestAnnotatedScreenshot()
    {
        AnnotatedScreenshotRequest& req = m_ActiveAnnotatedScreenshotRequests.emplace_back(m_FrameCounter, requestScreenshot());
        return req.resultPromise.get_future();
    }

    std::string getGraphicsBackendVendorString() const
    {
        return m_GraphicsContext.getBackendVendorString();
    }

    std::string getGraphicsBackendRendererString() const
    {
        return m_GraphicsContext.getBackendRendererString();
    }

    std::string getGraphicsBackendVersionString() const
    {
        return m_GraphicsContext.getBackendVersionString();
    }

    std::string getGraphicsBackendShadingLanguageVersionString() const
    {
        return m_GraphicsContext.getBackendShadingLanguageVersionString();
    }

    uint64_t getFrameCount() const
    {
        return m_FrameCounter;
    }

    uint64_t getTicks() const
    {
        return SDL_GetPerformanceCounter();
    }

    uint64_t getTickFrequency() const
    {
        return SDL_GetPerformanceFrequency();
    }

    osc::AppClock::time_point getCurrentTime() const
    {
        return ConvertPerfCounterToFClock(SDL_GetPerformanceCounter(), m_AppCounterFq);
    }

    osc::AppClock::time_point getAppStartupTime() const
    {
        return m_AppStartupTime;
    }

    osc::AppClock::time_point getFrameStartTime() const
    {
        return m_FrameStartTime;
    }

    osc::AppClock::duration getDeltaSinceAppStartup() const
    {
        return getCurrentTime() - m_AppStartupTime;
    }

    osc::AppClock::duration getDeltaSinceLastFrame() const
    {
        return m_TimeSinceLastFrame;
    }

    bool isMainLoopWaiting() const
    {
        return m_InWaitMode;
    }

    void setMainLoopWaiting(bool v)
    {
        m_InWaitMode = v;
        requestRedraw();
    }

    void makeMainEventLoopWaiting()
    {
        setMainLoopWaiting(true);
    }

    void makeMainEventLoopPolling()
    {
        setMainLoopWaiting(false);
    }

    void requestRedraw()
    {
        SDL_Event e{};
        e.type = SDL_USEREVENT;
        m_NumFramesToPoll += 2;  // some parts of ImGui require rendering 2 frames before it shows something
        SDL_PushEvent(&e);
    }

    void clearScreen(Color const& color)
    {
        m_GraphicsContext.clearScreen(color);
    }

    osc::MouseState getMouseState() const
    {
        glm::ivec2 mouseLocal{};
        Uint32 ms = SDL_GetMouseState(&mouseLocal.x, &mouseLocal.y);

        MouseState rv{};
        rv.LeftDown = ms & SDL_BUTTON(SDL_BUTTON_LEFT);
        rv.RightDown = ms & SDL_BUTTON(SDL_BUTTON_RIGHT);
        rv.MiddleDown = ms & SDL_BUTTON(SDL_BUTTON_MIDDLE);
        rv.X1Down = ms & SDL_BUTTON(SDL_BUTTON_X1);
        rv.X2Down = ms & SDL_BUTTON(SDL_BUTTON_X2);

        if (isWindowFocused())
        {
            static bool const s_CanUseGlobalMouseState = strncmp(SDL_GetCurrentVideoDriver(), "wayland", 7) != 0;

            if (s_CanUseGlobalMouseState)
            {
                glm::ivec2 mouseGlobal;
                SDL_GetGlobalMouseState(&mouseGlobal.x, &mouseGlobal.y);
                glm::ivec2 mouseWindow;
                SDL_GetWindowPosition(m_MainWindow.get(), &mouseWindow.x, &mouseWindow.y);

                rv.pos = mouseGlobal - mouseWindow;
            }
            else
            {
                rv.pos = mouseLocal;
            }
        }

        return rv;
    }

    void warpMouseInWindow(glm::vec2 v) const
    {
        SDL_WarpMouseInWindow(m_MainWindow.get(), static_cast<int>(v.x), static_cast<int>(v.y));
    }

    bool isShiftPressed() const
    {
        return SDL_GetModState() & KMOD_SHIFT;
    }

    bool isCtrlPressed() const
    {
        return SDL_GetModState() & KMOD_CTRL;
    }

    bool isAltPressed() const
    {
        return SDL_GetModState() & KMOD_ALT;
    }

    void setMainWindowSubTitle(std::string_view sv)
    {
        // use global + mutex to prevent hopping into the OS too much
        static SynchronizedValue<std::string> s_CurrentWindowSubTitle;

        auto lock = s_CurrentWindowSubTitle.lock();

        if (sv == *lock)
        {
            return;
        }

        *lock = sv;

        std::string const newTitle = sv.empty() ?
            OSC_APPNAME_STRING :
            (std::string{sv} + " - " + OSC_APPNAME_STRING);

        SDL_SetWindowTitle(m_MainWindow.get(), newTitle.c_str());
    }

    void unsetMainWindowSubTitle()
    {
        setMainWindowSubTitle("");
    }

    osc::Config const& getConfig() const
    {
        return *m_ApplicationConfig;
    }

    Config& updConfig()
    {
        return *m_ApplicationConfig;
    }

    std::filesystem::path getResource(std::string_view p) const
    {
        return ::GetResource(*m_ApplicationConfig, p);
    }

    std::string slurpResource(std::string_view p) const
    {
        std::filesystem::path path = getResource(p);
        return SlurpFileIntoString(path);
    }

    std::vector<uint8_t> slurpBinaryResource(std::string_view p) const
    {
        std::filesystem::path path = getResource(p);
        return SlurpFileIntoVector(path);
    }

    std::vector<osc::RecentFile> getRecentFiles() const
    {
        std::filesystem::path p = GetRecentFilesFilePath();

        if (!std::filesystem::exists(p))
        {
            return {};
        }

        return LoadRecentFilesFile(p);
    }

    void addRecentFile(std::filesystem::path const& p)
    {
        std::filesystem::path recentFilesPath = GetRecentFilesFilePath();

        // load existing list
        std::vector<RecentFile> rfs;
        if (std::filesystem::exists(recentFilesPath))
        {
            rfs = LoadRecentFilesFile(recentFilesPath);
        }

        // clear potentially duplicate entries from existing list
        osc::RemoveErase(rfs, [&p](RecentFile const& rf) { return rf.path == p; });

        // write by truncating existing list file
        std::ofstream fd{recentFilesPath, std::ios::trunc};

        if (!fd)
        {
            osc::log::error("%s: could not be opened for writing: cannot update recent files list", recentFilesPath.string().c_str());
        }

        // re-serialize the n newest entries (the loaded list is sorted oldest -> newest)
        auto begin = rfs.end() - (rfs.size() < 10 ? static_cast<int>(rfs.size()) : 10);
        for (auto it = begin; it != rfs.end(); ++it)
        {
            fd << it->lastOpenedUnixTimestamp.count() << ' ' << it->path << std::endl;
        }

        // append the new entry
        fd << GetCurrentTimeAsUnixTimestamp().count() << ' ' << std::filesystem::absolute(p) << std::endl;
    }

    std::shared_ptr<void> updSingleton(std::type_info const& typeinfo, std::function<std::shared_ptr<void>()> const& ctor)
    {
        auto lock = m_Singletons.lock();
        auto const [it, inserted] = lock->try_emplace(typeinfo, nullptr);
        if (inserted)
        {
            it->second = ctor();
        }
        return it->second;
    }

    // used by ImGui backends

    sdl::Window& updWindow()
    {
        return m_MainWindow;
    }

    osc::GraphicsContext& updGraphicsContext()
    {
        return m_GraphicsContext;
    }

    void* updRawGLContextHandle()
    {
        return m_GraphicsContext.updRawGLContextHandle();
    }

private:
    // perform a screen transntion between two top-level `osc::Screen`s
    void transitionToNextScreen()
    {
        if (!m_NextScreen)
        {
            return;
        }

        log::info("unmounting screen %s", m_CurrentScreen->name());

        try
        {
            m_CurrentScreen->onUnmount();
        }
        catch (std::exception const& ex)
        {
            log::error("error unmounting screen %s: %s", m_CurrentScreen->name(), ex.what());
            m_CurrentScreen.reset();
            throw;
        }

        m_CurrentScreen.reset();
        m_CurrentScreen = std::move(m_NextScreen);

        // the next screen might need to draw a couple of frames
        // to "warm up" (e.g. because it's using ImGui)
        m_NumFramesToPoll = 2;

        log::info("mounting screen %s", m_CurrentScreen->name());
        m_CurrentScreen->onMount();
        log::info("transitioned main screen to %s", m_CurrentScreen->name());
    }

    // the main application loop
    //
    // this is what he application enters when it `show`s the first screen
    void mainLoopUnguarded()
    {
        // perform initial screen mount
        m_CurrentScreen->onMount();

        // ensure current screen is unmounted when exiting the main loop
        OSC_SCOPE_GUARD_IF(m_CurrentScreen, { m_CurrentScreen->onUnmount(); });

        // reset counters
        m_AppCounter = SDL_GetPerformanceCounter();
        m_FrameCounter = 0;
        m_FrameStartTime = ConvertPerfCounterToFClock(m_AppCounter, m_AppCounterFq);
        m_TimeSinceLastFrame = AppClock::duration{1.0f/60.0f};  // (estimated value for first frame)

        while (true)  // gameloop
        {
            // pump events
            {
                OSC_PERF("App/pumpEvents");

                bool shouldWait = m_InWaitMode && m_NumFramesToPoll <= 0;
                m_NumFramesToPoll = std::max(0, m_NumFramesToPoll - 1);

                for (SDL_Event e; shouldWait ? SDL_WaitEventTimeout(&e, 1000) : SDL_PollEvent(&e);)
                {
                    shouldWait = false;

                    if (e.type == SDL_WINDOWEVENT)
                    {
                        // window was resized and should be drawn a couple of times quickly
                        // to ensure any datastructures in the screens (namely: imgui) are
                        // updated
                        m_NumFramesToPoll = 2;
                    }

                    // let screen handle the event
                    m_CurrentScreen->onEvent(e);

                    if (m_QuitRequested)
                    {
                        // screen requested application quit, so exit this function
                        return;
                    }

                    if (m_NextScreen)
                    {
                        // screen requested a new screen, so perform the transition
                        transitionToNextScreen();
                    }

                    if (e.type == SDL_DROPTEXT || e.type == SDL_DROPFILE)
                    {
                        SDL_free(e.drop.file);  // SDL documentation mandates that the caller frees this
                    }
                }
            }

            // update clocks
            {
                auto counter = SDL_GetPerformanceCounter();

                Uint64 deltaTicks = counter - m_AppCounter;

                m_AppCounter = counter;
                m_FrameStartTime = ConvertPerfCounterToFClock(counter, m_AppCounterFq);
                m_TimeSinceLastFrame = ConvertPerfTicksToFClockDuration(deltaTicks, m_AppCounterFq);
            }

            // "tick" the screen
            {
                OSC_PERF("App/onTick");
                m_CurrentScreen->onTick();
            }

            if (m_QuitRequested)
            {
                // screen requested application quit, so exit this function
                return;
            }

            if (m_NextScreen)
            {
                // screen requested a new screen, so perform the transition
                transitionToNextScreen();
                continue;
            }

            // "draw" the screen into the window framebuffer
            {
                OSC_PERF("App/onDraw");
                m_CurrentScreen->onDraw();
            }

            // "present" the rendered screen to the user (can block on VSYNC)
            {
                OSC_PERF("App/doSwapBuffers");
                m_GraphicsContext.doSwapBuffers(*m_MainWindow);
            }

            // handle annotated screenshot requests (if any)
            {
                // save this frame's annotations into the requests, if necessary
                for (AnnotatedScreenshotRequest& req : m_ActiveAnnotatedScreenshotRequests)
                {
                    if (req.frameRequested == m_FrameCounter)
                    {
                        req.annotations = m_FrameAnnotations;
                    }
                }
                m_FrameAnnotations.clear();  // this frame's annotations are now saved (if necessary)

                // complete any requests for which screenshot data has arrived
                for (AnnotatedScreenshotRequest& req : m_ActiveAnnotatedScreenshotRequests)
                {
                    if (req.underlyingScreenshotFuture.valid() &&
                        req.underlyingScreenshotFuture.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
                    {
                        // screenshot is ready: create an annotated screenshot and send it to
                        // the caller
                        req.resultPromise.set_value(AnnotatedImage{req.underlyingScreenshotFuture.get(), std::move(req.annotations)});
                    }
                }

                // gc any invalid (i.e. handled) requests
                osc::RemoveErase(m_ActiveAnnotatedScreenshotRequests, [](AnnotatedScreenshotRequest const& req) { return !req.underlyingScreenshotFuture.valid(); });
            }

            // care: only update the frame counter here because the above methods
            // and checks depend on it being consistient throughout a single crank
            // of the application loop
            ++m_FrameCounter;

            if (m_QuitRequested)
            {
                // screen requested application quit, so exit this function
                return;
            }

            if (m_NextScreen)
            {
                // screen requested a new screen, so perform the transition
                transitionToNextScreen();
                continue;
            }
        }
    }

    // init/load the application config first
    std::unique_ptr<Config> m_ApplicationConfig = Config::load();

    // install the backtrace handler (if necessary - once per process)
    bool m_IsBacktraceHandlerInstalled = EnsureBacktraceHandlerEnabled();

    // init SDL context (windowing, etc.)
    sdl::Context m_SDLContext{SDL_INIT_VIDEO};

    // init main application window
    sdl::Window m_MainWindow = CreateMainAppWindow();

    // init graphics context
    GraphicsContext m_GraphicsContext{*m_MainWindow};

    // get performance counter frequency (for the delta clocks)
    Uint64 m_AppCounterFq = SDL_GetPerformanceFrequency();

    // current performance counter value (recorded once per frame)
    Uint64 m_AppCounter = 0;

    // number of frames the application has drawn
    uint64_t m_FrameCounter = 0;

    // when the application started up (set now)
    AppClock::time_point m_AppStartupTime = ConvertPerfCounterToFClock(SDL_GetPerformanceCounter(), m_AppCounterFq);

    // when the current frame started (set each frame)
    AppClock::time_point m_FrameStartTime = m_AppStartupTime;

    // time since the frame before the current frame (set each frame)
    AppClock::duration m_TimeSinceLastFrame = {};

    // global cache of application-wide singletons (usually, for caching)
    SynchronizedValue<std::unordered_map<TypeInfoReference, std::shared_ptr<void>>> m_Singletons;

    // how many samples the implementation should actually use
    int32_t m_CurrentMSXAASamples = std::min(m_GraphicsContext.getMaxMSXAASamples(), m_ApplicationConfig->getNumMSXAASamples());

    // set to true if the application should quit
    bool m_QuitRequested = false;

    // set to true if the main loop should pause on events
    //
    // CAREFUL: this makes the app event-driven
    bool m_InWaitMode = false;

    // set >0 to force that `n` frames are polling-driven: even in waiting mode
    int32_t m_NumFramesToPoll = 0;

    // current screen being shown (if any)
    std::unique_ptr<Screen> m_CurrentScreen = nullptr;

    // the *next* screen the application should show
    std::unique_ptr<Screen> m_NextScreen = nullptr;

    // frame annotations made during this frame
    std::vector<ImageAnnotation> m_FrameAnnotations;

    // any active promises for an annotated frame
    std::vector<AnnotatedScreenshotRequest> m_ActiveAnnotatedScreenshotRequests;
};

// public API

osc::App* osc::App::g_Current = nullptr;

std::filesystem::path osc::App::resource(std::string_view s)
{
    return get().getResource(s);
}

std::string osc::App::slurp(std::string_view s)
{
    return get().slurpResource(s);
}

std::vector<uint8_t> osc::App::slurpBinary(std::string_view s)
{
    return get().slurpBinaryResource(std::move(s));
}

osc::App::App() : m_Impl{new Impl{}}
{
    g_Current = this;
}

osc::App::App(App&&) noexcept = default;

osc::App& osc::App::operator=(App&&) noexcept = default;

osc::App::~App() noexcept
{
    g_Current = nullptr;
}

void osc::App::show(std::unique_ptr<Screen> s)
{
    m_Impl->show(std::move(s));
}

void osc::App::requestTransition(std::unique_ptr<Screen> s)
{
    m_Impl->requestTransition(std::move(s));
}

bool osc::App::isTransitionRequested() const
{
    return m_Impl->isTransitionRequested();
}

void osc::App::requestQuit()
{
    m_Impl->requestQuit();
}

glm::ivec2 osc::App::idims() const
{
    return m_Impl->idims();
}

glm::vec2 osc::App::dims() const
{
    return m_Impl->dims();
}

float osc::App::aspectRatio() const
{
    return m_Impl->aspectRatio();
}

void osc::App::setShowCursor(bool v)
{
    m_Impl->setShowCursor(std::move(v));
}

bool osc::App::isWindowFocused() const
{
    return m_Impl->isWindowFocused();
}

void osc::App::makeFullscreen()
{
    m_Impl->makeFullscreen();
}

void osc::App::makeWindowedFullscreen()
{
    m_Impl->makeWindowedFullscreen();
}

void osc::App::makeWindowed()
{
    m_Impl->makeWindowed();
}

int32_t osc::App::getMSXAASamplesRecommended() const
{
    return m_Impl->getMSXAASamplesRecommended();
}

void osc::App::setMSXAASamplesRecommended(int32_t s)
{
    m_Impl->setMSXAASamplesRecommended(std::move(s));
}

int32_t osc::App::getMSXAASamplesMax() const
{
    return m_Impl->getMSXAASamplesMax();
}

bool osc::App::isInDebugMode() const
{
    return m_Impl->isInDebugMode();
}

void osc::App::enableDebugMode()
{
    m_Impl->enableDebugMode();
}

void osc::App::disableDebugMode()
{
    m_Impl->disableDebugMode();
}

bool osc::App::isVsyncEnabled() const
{
    return m_Impl->isVsyncEnabled();
}

void osc::App::setVsync(bool v)
{
    m_Impl->setVsync(std::move(v));
}

void osc::App::enableVsync()
{
    m_Impl->enableVsync();
}

void osc::App::disableVsync()
{
    m_Impl->disableVsync();
}

void osc::App::addFrameAnnotation(std::string_view label, Rect screenRect)
{
    m_Impl->addFrameAnnotation(std::move(label), std::move(screenRect));
}

std::future<osc::Image> osc::App::requestScreenshot()
{
    return m_Impl->requestScreenshot();
}

std::future<osc::AnnotatedImage> osc::App::requestAnnotatedScreenshot()
{
    return m_Impl->requestAnnotatedScreenshot();
}

std::string osc::App::getGraphicsBackendVendorString() const
{
    return m_Impl->getGraphicsBackendVendorString();
}

std::string osc::App::getGraphicsBackendRendererString() const
{
    return m_Impl->getGraphicsBackendRendererString();
}

std::string osc::App::getGraphicsBackendVersionString() const
{
    return m_Impl->getGraphicsBackendVersionString();
}

std::string osc::App::getGraphicsBackendShadingLanguageVersionString() const
{
    return m_Impl->getGraphicsBackendShadingLanguageVersionString();
}

uint64_t osc::App::getFrameCount() const
{
    return m_Impl->getFrameCount();
}

uint64_t osc::App::getTicks() const
{
    return m_Impl->getTicks();
}

uint64_t osc::App::getTickFrequency() const
{
    return m_Impl->getTickFrequency();
}

osc::AppClock::time_point osc::App::getCurrentTime() const
{
    return m_Impl->getCurrentTime();
}

osc::AppClock::time_point osc::App::getAppStartupTime() const
{
    return m_Impl->getAppStartupTime();
}

osc::AppClock::time_point osc::App::getFrameStartTime() const
{
    return m_Impl->getFrameStartTime();
}

osc::AppClock::duration osc::App::getDeltaSinceAppStartup() const
{
    return m_Impl->getDeltaSinceAppStartup();
}

osc::AppClock::duration osc::App::getDeltaSinceLastFrame() const
{
    return m_Impl->getDeltaSinceLastFrame();
}

bool osc::App::isMainLoopWaiting() const
{
    return m_Impl->isMainLoopWaiting();
}

void osc::App::setMainLoopWaiting(bool v)
{
    m_Impl->setMainLoopWaiting(std::move(v));
}

void osc::App::makeMainEventLoopWaiting()
{
    m_Impl->makeMainEventLoopWaiting();
}

void osc::App::makeMainEventLoopPolling()
{
    m_Impl->makeMainEventLoopPolling();
}

void osc::App::requestRedraw()
{
    m_Impl->requestRedraw();
}

void osc::App::clearScreen(Color const& color)
{
    m_Impl->clearScreen(color);
}

osc::MouseState osc::App::getMouseState() const
{
    return m_Impl->getMouseState();
}

void osc::App::warpMouseInWindow(glm::vec2 v) const
{
    m_Impl->warpMouseInWindow(std::move(v));
}

bool osc::App::isShiftPressed() const
{
    return m_Impl->isShiftPressed();
}

bool osc::App::isCtrlPressed() const
{
    return m_Impl->isCtrlPressed();
}

bool osc::App::isAltPressed() const
{
    return m_Impl->isAltPressed();
}

void osc::App::setMainWindowSubTitle(std::string_view sv)
{
    m_Impl->setMainWindowSubTitle(std::move(sv));
}

void osc::App::unsetMainWindowSubTitle()
{
    m_Impl->unsetMainWindowSubTitle();
}

osc::Config const& osc::App::getConfig() const
{
    return m_Impl->getConfig();
}

osc::Config& osc::App::updConfig()
{
    return m_Impl->updConfig();
}

std::filesystem::path osc::App::getResource(std::string_view p) const
{
    return m_Impl->getResource(std::move(p));
}

std::string osc::App::slurpResource(std::string_view p) const
{
    return m_Impl->slurpResource(std::move(p));
}

std::vector<uint8_t> osc::App::slurpBinaryResource(std::string_view p) const
{
    return m_Impl->slurpBinaryResource(std::move(p));
}

std::vector<osc::RecentFile> osc::App::getRecentFiles() const
{
    return m_Impl->getRecentFiles();
}

void osc::App::addRecentFile(std::filesystem::path const& p)
{
    m_Impl->addRecentFile(p);
}

std::shared_ptr<void> osc::App::updSingleton(std::type_info const& typeInfo, std::function<std::shared_ptr<void>()> const& ctor)
{
    return m_Impl->updSingleton(typeInfo, ctor);
}

void osc::ImGuiInit()
{
    // init ImGui top-level context
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // configure ImGui from OSC's (toml) configuration
    {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        if (App::get().getConfig().isMultiViewportEnabled())
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        }
    }

    // make it so that windows can only ever be moved from the title bar
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

    // load application-level ImGui config, then the user one,
    // so that the user config takes precedence
    {
        std::string defaultIni = App::resource("imgui_base_config.ini").string();
        ImGui::LoadIniSettingsFromDisk(defaultIni.c_str());

        // CARE: the reason this filepath is `static` is because ImGui requires that
        // the string outlives the ImGui context
        static std::string const s_UserImguiIniFilePath = (osc::GetUserDataDir() / "imgui.ini").string();

        ImGui::LoadIniSettingsFromDisk(s_UserImguiIniFilePath.c_str());
        io.IniFilename = s_UserImguiIniFilePath.c_str();
    }

    ImFontConfig baseConfig;
    baseConfig.SizePixels = 15.0f;
    baseConfig.PixelSnapH = true;
    baseConfig.OversampleH = 2;
    baseConfig.OversampleV = 2;
    std::string baseFontFile = App::resource("fonts/Ruda-Bold.ttf").string();
    io.Fonts->AddFontFromFileTTF(baseFontFile.c_str(), baseConfig.SizePixels, &baseConfig);

    // add FontAwesome icon support
    {
        ImFontConfig config = baseConfig;
        config.MergeMode = true;
        config.GlyphMinAdvanceX = std::floor(1.5f * config.SizePixels);
        config.GlyphMaxAdvanceX = std::floor(1.5f * config.SizePixels);

        std::string const fontFile = App::resource("fonts/fa-solid-900.ttf").string();
        io.Fonts->AddFontFromFileTTF(
            fontFile.c_str(),
            config.SizePixels,
            &config,
            c_IconRanges.data()
        );
    }

    // init ImGui for SDL2 /w OpenGL
    App::Impl& impl = *App::upd().m_Impl;
    ImGui_ImplSDL2_InitForOpenGL(impl.updWindow().get(), impl.updRawGLContextHandle());

    // init ImGui for OpenGL
    ImGui_ImplOpenGL3_Init(OSC_GLSL_VERSION);

    ImGuiApplyDarkTheme();
}

void osc::ImGuiShutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

bool osc::ImGuiOnEvent(SDL_Event const& e)
{
    ImGui_ImplSDL2_ProcessEvent(&e);

    ImGuiIO const& io  = ImGui::GetIO();

    bool handledByImgui = false;

    if (io.WantCaptureKeyboard && (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP))
    {
        handledByImgui = true;
    }

    if (io.WantCaptureMouse && (e.type == SDL_MOUSEWHEEL || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEBUTTONDOWN))
    {
        handledByImgui = true;
    }

    return handledByImgui;
}

void osc::ImGuiNewFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(App::upd().m_Impl->updWindow().get());
    ImGui::NewFrame();
}

void osc::ImGuiRender()
{
    // bound program can sometimes cause issues
    App::upd().m_Impl->updGraphicsContext().clearProgram();

    {
        OSC_PERF("ImGuiRender/Render");
        ImGui::Render();
    }

    {
        OSC_PERF("ImGuiRender/ImGui_ImplOpenGL3_RenderDrawData");

        // HACK: convert all ImGui-provided colors from sRGB to linear
        //
        // this is necessary because the ImGui OpenGL backend's shaders
        // assume all color vertices and colors from textures are in
        // sRGB, but OSC can provide ImGui with linear OR sRGB textures
        // because OSC assumes the OpenGL backend is using automatic
        // color conversion support (in ImGui, it isn't)
        //
        // so what we do here is linearize all colors from ImGui and
        // always provide textures in the OSC style. The shaders in ImGui
        // then write linear color values to the screen, but because we
        // are *also* enabling GL_FRAMEBUFFER_SRGB, the OpenGL backend
        // will correctly convert those linear colors to sRGB if necessary
        // automatically
        //
        // (this shitshow is because ImGui's OpenGL backend behaves differently
        //  from OSCs - ultimately, we need an ImGui_ImplOSC backend)
        ConvertDrawDataFromSRGBToLinear(*ImGui::GetDrawData());
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // ImGui: handle multi-viewports if the user has requested them
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        SDL_Window* backupCurrentWindow = SDL_GL_GetCurrentWindow();
        SDL_GLContext backupCurrentContext = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backupCurrentWindow, backupCurrentContext);
    }
}
