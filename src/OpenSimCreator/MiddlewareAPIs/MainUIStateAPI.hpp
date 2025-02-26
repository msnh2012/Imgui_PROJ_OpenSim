#pragma once

#include <oscar/Tabs/TabHost.hpp>

namespace osc { class ParamBlock; }
namespace osc { class OutputExtractor; }

namespace osc
{
    // API access to shared state between main UI tabs
    //
    // this is how individual UI tabs inter-communicate (e.g. by sharing data, closing other tabs, etc.)
    class MainUIStateAPI : public TabHost {
    protected:
        MainUIStateAPI() = default;
        MainUIStateAPI(MainUIStateAPI const&) = default;
        MainUIStateAPI(MainUIStateAPI&&) noexcept = default;
        MainUIStateAPI& operator=(MainUIStateAPI const&) = default;
        MainUIStateAPI& operator=(MainUIStateAPI&&) noexcept = default;
    public:
        virtual ~MainUIStateAPI() noexcept = default;

        ParamBlock const& getSimulationParams() const
        {
            return implGetSimulationParams();
        }
        ParamBlock& updSimulationParams()
        {
            return implUpdSimulationParams();
        }

        int getNumUserOutputExtractors() const
        {
            return implGetNumUserOutputExtractors();
        }
        OutputExtractor const& getUserOutputExtractor(int index) const
        {
            return implGetUserOutputExtractor(index);
        }
        void addUserOutputExtractor(OutputExtractor const& extractor)
        {
            return implAddUserOutputExtractor(extractor);
        }
        void removeUserOutputExtractor(int index)
        {
            implRemoveUserOutputExtractor(index);
        }
        bool hasUserOutputExtractor(OutputExtractor const& extractor) const
        {
            return implHasUserOutputExtractor(extractor);
        }
        bool removeUserOutputExtractor(OutputExtractor const& extractor)
        {
            return implRemoveUserOutputExtractor(extractor);
        }

    private:
        virtual ParamBlock const& implGetSimulationParams() const = 0;
        virtual ParamBlock& implUpdSimulationParams() = 0;

        virtual int implGetNumUserOutputExtractors() const = 0;
        virtual OutputExtractor const& implGetUserOutputExtractor(int) const = 0;
        virtual void implAddUserOutputExtractor(OutputExtractor const&) = 0;
        virtual void implRemoveUserOutputExtractor(int) = 0;
        virtual bool implHasUserOutputExtractor(OutputExtractor const&) const = 0;
        virtual bool implRemoveUserOutputExtractor(OutputExtractor const&) = 0;
    };
}
