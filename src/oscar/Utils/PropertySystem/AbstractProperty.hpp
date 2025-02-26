#pragma once

#include "oscar/Utils/PropertySystem/PropertyType.hpp"
#include "oscar/Utils/CStringView.hpp"

namespace osc { class Component; }
namespace osc { template<typename TValue> class Property; }

namespace osc
{
    // type-erased base class for a property
    class AbstractProperty {
    private:
        // only a Property<TValue> may construct this base class
        template<typename TValue>
        friend class Property;

        AbstractProperty() = default;
        AbstractProperty(AbstractProperty const&) = default;
        AbstractProperty(AbstractProperty&&) noexcept = default;
        AbstractProperty& operator=(AbstractProperty const&) = default;
        AbstractProperty& operator=(AbstractProperty&&) noexcept = default;
    public:
        virtual ~AbstractProperty() noexcept = default;

        Component const& getOwner() const
        {
            return implGetOwner();
        }

        Component& updOwner()
        {
            return implUpdOwner();
        }

        CStringView getName() const
        {
            return implGetName();
        }

        CStringView getDescription() const
        {
            return implGetDescription();
        }

        PropertyType getPropertyType() const
        {
            return implGetPropertyType();
        }

    private:
        virtual Component const& implGetOwner() const = 0;
        virtual Component& implUpdOwner() = 0;
        virtual CStringView implGetName() const = 0;
        virtual CStringView implGetDescription() const = 0;
        virtual PropertyType implGetPropertyType() const = 0;
    };
}