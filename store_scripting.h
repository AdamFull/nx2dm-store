#pragma once

namespace nxe {
class ModuleContext;
namespace script {
class Host;
}
}

namespace nxm::store {

/// The neutral `host.store_*` surface - defined exactly once, here, so a
/// backend module never has to (that would duplicate the same functions
/// once per store). Every function forwards through
/// `ctx.services().find<T>(...)` and reports "unsupported"/false when the
/// active backend doesn't provide that service, rather than a game script
/// needing to know which store is running.
void expose_store_services(nxe::script::Host &host, nxe::ModuleContext &ctx);

}
