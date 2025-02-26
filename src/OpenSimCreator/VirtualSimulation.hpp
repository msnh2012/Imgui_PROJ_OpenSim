#pragma once

#include "OpenSimCreator/SimulationClock.hpp"
#include "OpenSimCreator/SimulationReport.hpp"
#include "OpenSimCreator/SimulationStatus.hpp"

#include <oscar/Utils/SynchronizedValue.hpp>

#include <nonstd/span.hpp>

#include <vector>

namespace osc { class OutputExtractor; }
namespace osc { class ParamBlock; }
namespace OpenSim { class Model; }

namespace osc
{
    // a virtual simulation could be backed by (e.g.):
    //
    // - a real "live" forward-dynamic simulation
    // - an .sto file
    //
    // the GUI code shouldn't care about the specifics - it's up to each concrete
    // implementation to ensure this API is obeyed w.r.t. multithreading etc.
    class VirtualSimulation {
    protected:
        VirtualSimulation() = default;
        VirtualSimulation(VirtualSimulation const&) = default;
        VirtualSimulation(VirtualSimulation&&) noexcept = default;
        VirtualSimulation& operator=(VirtualSimulation const&) = default;
        VirtualSimulation& operator=(VirtualSimulation&&) noexcept = default;
    public:
        virtual ~VirtualSimulation() noexcept = default;

        // the reason why the model is mutex-guarded is because OpenSim has a bunch of
        // `const-` interfaces that are only "logically const" in a single-threaded
        // environment.
        //
        // this can lead to mayhem if (e.g.) the model is actually being mutated by
        // multiple threads concurrently
        SynchronizedValueGuard<OpenSim::Model const> getModel() const
        {
            return implGetModel();
        }

        int getNumReports() const
        {
            return implGetNumReports();
        }

        SimulationReport getSimulationReport(int reportIndex) const
        {
            return implGetSimulationReport(reportIndex);
        }

        std::vector<SimulationReport> getAllSimulationReports() const
        {
            return implGetAllSimulationReports();
        }

        SimulationStatus getStatus() const
        {
            return implGetStatus();
        }

        SimulationClock::time_point getCurTime() const
        {
            return implGetCurTime();
        }

        SimulationClock::time_point getStartTime() const
        {
            return implGetStartTime();
        }

        SimulationClock::time_point getEndTime() const
        {
            return implGetEndTime();
        }

        float getProgress() const
        {
            return implGetProgress();
        }

        ParamBlock const& getParams() const
        {
            return implGetParams();
        }

        nonstd::span<OutputExtractor const> getOutputExtractors() const
        {
            return implGetOutputExtractors();
        }

        void requestStop()
        {
            implRequestStop();
        }

        void stop()
        {
            implStop();
        }

        // TODO: these are necessary right now because the fixup scale factor isn't part of the model
        float getFixupScaleFactor() const
        {
            return implGetFixupScaleFactor();
        }

        void setFixupScaleFactor(float newScaleFactor)
        {
            implSetFixupScaleFactor(newScaleFactor);
        }

    private:
        virtual SynchronizedValueGuard<OpenSim::Model const> implGetModel() const = 0;

        virtual int implGetNumReports() const = 0;
        virtual SimulationReport implGetSimulationReport(int reportIndex) const = 0;
        virtual std::vector<SimulationReport> implGetAllSimulationReports() const = 0;

        virtual SimulationStatus implGetStatus() const = 0;
        virtual SimulationClock::time_point implGetCurTime() const = 0;
        virtual SimulationClock::time_point implGetStartTime() const = 0;
        virtual SimulationClock::time_point implGetEndTime() const = 0;
        virtual float implGetProgress() const = 0;
        virtual ParamBlock const& implGetParams() const = 0;
        virtual nonstd::span<OutputExtractor const> implGetOutputExtractors() const = 0;

        virtual void implRequestStop() = 0;
        virtual void implStop() = 0;

        virtual float implGetFixupScaleFactor() const = 0;
        virtual void implSetFixupScaleFactor(float) = 0;
    };
}
