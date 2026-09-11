#include "store/store_scripting.h"

#include "core/app/module.h"

namespace nxm::store {
namespace {

/// Owns nothing but the neutral `host.store_*` surface (store_scripting.cpp)
/// - it registers no service of its own. Every storefront backend
/// (store_steam, ...) depends on this module and registers whichever of the
/// five interfaces in store_service.h its SDK actually implements; this
/// module never names a concrete store, the same discipline the scripting
/// backends already keep for language neutrality.
class StoreModule final : public nxe::Module {
public:
  [[nodiscard]] nxe::ModuleDescriptor descriptor() const noexcept override {
    nxe::ModuleDescriptor out{};
    out.id = "store";
    out.version = {1, 0, 0};
    out.platforms = nxe::ModulePlatform::All;
    return out;
  }

  void on_expose_scripts(nxe::script::Host &host,
                         nxe::ModuleContext &ctx) override {
    expose_store_services(host, ctx);
  }
};

}
}

NX_DECLARE_MODULE(store, nxm::store::StoreModule)
