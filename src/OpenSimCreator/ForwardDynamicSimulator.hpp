#pragma once

#include "OpenSimCreator/BasicModelStatePair.hpp"
#include "OpenSimCreator/OutputExtractor.hpp"
#include "OpenSimCreator/SimulationStatus.hpp"

#include <functional>
#include <memory>

namespace osc { struct ForwardDynamicSimulatorParams; }
namespace osc { class SimulationReport; }

namespace osc
{
    // returns outputs (e.g. auxiliary stuff like integration steps) that the
    // FdSimulator writes into the `SimulationReport`s it emits
    int GetNumFdSimulatorOutputExtractors();
    OutputExtractor GetFdSimulatorOutputExtractor(int);

    // a forward-dynamic simulation that immediately starts running on a background thread
    class ForwardDynamicSimulator final {
    public:
        // immediately starts the simulation upon construction
        //
        // care: the callback is called *on the bg thread* - you should know how
        //       to handle it (e.g. mutexes) appropriately
        ForwardDynamicSimulator(
            BasicModelStatePair,
            ForwardDynamicSimulatorParams const& params,
            std::function<void(SimulationReport)> onReportFromBgThread
        );
        ForwardDynamicSimulator(ForwardDynamicSimulator const&) = delete;
        ForwardDynamicSimulator(ForwardDynamicSimulator&&) noexcept;
        ForwardDynamicSimulator& operator=(ForwardDynamicSimulator const&) = delete;
        ForwardDynamicSimulator& operator=(ForwardDynamicSimulator&&) noexcept;
        ~ForwardDynamicSimulator() noexcept;

        SimulationStatus getStatus() const;
        void requestStop();  // asynchronous
        void stop();  // synchronous (blocks until it stops)
        ForwardDynamicSimulatorParams const& params() const;

    private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
