#include "CookiecutterScreen.hpp"

#include "oscar/Graphics/Color.hpp"
#include "oscar/Platform/App.hpp"

#include <imgui.h>

#include <utility>

class osc::CookiecutterScreen::Impl final {
public:

    void onMount()
    {
        // called when app receives the screen, but before it starts pumping events
        // into it, ticking it, drawing it, etc.

        osc::ImGuiInit();  // boot up ImGui support
    }

    void onUnmount()
    {
        // called when the app is going to stop pumping events/ticks/draws into this
        // screen (e.g. because the app is quitting, or transitioning to some other screen)

        osc::ImGuiShutdown();  // shutdown ImGui support
    }

    void onEvent(SDL_Event const& e)
    {
        // called when the app receives an event from the operating system

        if (e.type == SDL_QUIT)
        {
            App::upd().requestQuit();
            return;
        }
        else if (osc::ImGuiOnEvent(e))
        {
            return;  // ImGui handled this particular event
        }
    }

    void onTick()
    {
        // called once per frame, before drawing, with a timedelta from the last call
        // to `tick`

        // use this if you need to regularly update something (e.g. an animation, or
        // file polling)
    }

    void onDraw()
    {
        // called once per frame. Code in here should use drawing primitives, osc::Graphics,
        // ImGui, etc. to draw things into the screen. The application does not clear the
        // screen buffer between frames (it's assumed that your code does this when it needs
        // to)

        osc::ImGuiNewFrame();  // tell ImGui you're about to start drawing a new frame

        App::upd().clearScreen(Color::clear());  // set app window bg color

        ImGui::Begin("cookiecutter panel");
        ImGui::Text("hello world");
        ImGui::Checkbox("checkbox_state", &m_CheckboxState);
        ImGui::End();

        osc::ImGuiRender();  // tell ImGui to render any ImGui widgets since calling ImGuiNewFrame();
    }

private:
    bool m_CheckboxState = false;
};


// public API (PIMPL)

osc::CookiecutterScreen::CookiecutterScreen() :
    m_Impl{std::make_unique<Impl>()}
{
}

osc::CookiecutterScreen::CookiecutterScreen(CookiecutterScreen&&) noexcept = default;
osc::CookiecutterScreen& osc::CookiecutterScreen::operator=(CookiecutterScreen&&) noexcept = default;
osc::CookiecutterScreen::~CookiecutterScreen() noexcept = default;

void osc::CookiecutterScreen::implOnMount()
{
    m_Impl->onMount();
}

void osc::CookiecutterScreen::implOnUnmount()
{
    m_Impl->onUnmount();
}

void osc::CookiecutterScreen::implOnEvent(SDL_Event const& e)
{
    m_Impl->onEvent(e);
}

void osc::CookiecutterScreen::implOnTick()
{
    m_Impl->onTick();
}

void osc::CookiecutterScreen::implOnDraw()
{
    m_Impl->onDraw();
}
