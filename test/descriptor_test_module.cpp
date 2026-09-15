#include <commrat/commrat.hpp>
#include <commrat/module_main.hpp>

namespace descriptor_test {

struct OutputData {
    float value{0.0F};
};

struct ResetCommand {
    struct Reply {
        bool success{false};
    };
};

struct ModuleParams {
    float gain{1.5F};
    int sample_count{8};
};

using DescriptorApp = commrat::CommRaT<
    commrat::DataWithCommands<OutputData, ResetCommand>>;

class DescriptorModule : public DescriptorApp::Module2<
    commrat::Output<OutputData>,
    commrat::Period<commrat::Milliseconds(25)>,
    commrat::Params<ModuleParams>> {
public:
    using DescriptorApp::Module2<
        commrat::Output<OutputData>,
        commrat::Period<commrat::Milliseconds(25)>,
        commrat::Params<ModuleParams>>::Module2;

protected:
    void process(OutputData& output) override {
        output.value = params_.gain;
    }
};

} // namespace descriptor_test

COMMRAT_MODULE_MAIN(descriptor_test::DescriptorModule)