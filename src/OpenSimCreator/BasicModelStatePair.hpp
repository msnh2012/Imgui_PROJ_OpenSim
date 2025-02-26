#pragma once

#include "OpenSimCreator/VirtualModelStatePair.hpp"

#include <oscar/Utils/ClonePtr.hpp>

namespace OpenSim { class Model; }
namespace SimTK { class State; }

namespace osc
{
    // an `OpenSim::Model` + `SimTK::State` that is a value type, constructed with:
    //
    // - `osc::Initialize`
    // - (if creating a new state) `model.equilibrateMuscles(State&)`
    // - (if creating a new state) `model.realizeAcceleration(State&)`
    //
    // this is a *basic* class that only guarantees the model is *initialized* this way. It
    // does not guarantee that everything is up-to-date after a caller mutates the model.
    class BasicModelStatePair final : public VirtualModelStatePair {
    public:
        BasicModelStatePair();
        explicit BasicModelStatePair(VirtualModelStatePair const&);
        BasicModelStatePair(OpenSim::Model const&, SimTK::State const&);
        BasicModelStatePair(BasicModelStatePair const&);
        BasicModelStatePair(BasicModelStatePair&&) noexcept;
        BasicModelStatePair& operator=(BasicModelStatePair const&);
        BasicModelStatePair& operator=(BasicModelStatePair&&) noexcept;
        ~BasicModelStatePair() noexcept override;

    private:
        OpenSim::Model const& implGetModel() const final;
        SimTK::State const& implGetState() const final;

        float implGetFixupScaleFactor() const final;
        void implSetFixupScaleFactor(float) final;

        class Impl;
        ClonePtr<Impl> m_Impl;
    };
}
