# store module

The neutral storefront interface. `modules/store` itself talks to no
storefront - it only declares five independently-optional services
(`store_service.h`) and exposes the neutral `host.store_*` Luau surface
(`store_scripting.cpp`) that forwards to whichever backend module is active.
A storefront integration is a separate backend module - `modules/store_steam`
is the first, fully built and verified one; see "Adding a new backend" below
for the recipe the rest (EGS, GOG Galaxy, Stove, Microsoft Store, ...)
follow.

This module never names a concrete store, the same discipline the scripting
backends already keep for language neutrality - `grep`ping this module for a
store name should never turn up a hit.

## The five services

Declared in `store_service.h`, registered by a backend through
`ModuleContext::service_registrar().provide(id, version, service)` and read
through `ModuleContext::services().find<T>(id)` - the same mechanism
`modules/example`, `modules/spine` and `modules/live2d` already use for their
own services.

| id | interface | required? |
|---|---|---|
| `store.core` | `StoreCore` - ownership/entitlement checks | every backend provides this |
| `store.iap` | `StoreIap` - product listing + purchase flow | optional |
| `store.achievements` | `StoreAchievements` | optional |
| `store.cloud_saves` | `StoreCloudSaves` - small key/value saves | optional |
| `store.presence` | `StorePresence` - rich presence + friends | optional |

"Optional" means exactly what `ServiceProvider::find<T>` already does for any
absent service: it returns `nullptr`. A backend whose SDK has no cloud-save
or presence subsystem (Stove, for one - its SDK genuinely has neither) simply
never registers `StoreCloudSaves`/`StorePresence`, and every neutral Luau
function above degrades to a safe `false`/empty return. There is no separate
capability-flag API to check first.

`order_modules()` already rejects two modules that both declare the same
`provided_services` entry, so if a build somehow enabled two store backends
at once, startup fails loudly (`"exported by both"`) instead of silently
picking one - nothing new needed for "only one store is active."

## Build exclusion

A backend is a normal module: `-DNX_MODULE_STORE_<NAME>=ON`, off by default.
For a project, `python nx.py modules <project> --add store --add store_steam`
adds them to `project.json`'s `"modules"` list, which `nx.py build --project`
now derives `-DNX_MODULE_<NAME>=ON`/`OFF` from **for every known module,
explicitly** - so a Steam-only project structurally cannot end up with any
other backend compiled in, with no manual flag to remember.

## Config convention

Each backend owns its own INI file, matching `modules/modio`'s config
convention exactly: `nx::ini::parse` + `nx::vfs::read_text` against
`/config/store_<name>.ini` (authored at `assets/config/store_<name>.ini`,
passed through the cook pipeline byte-for-byte), read automatically in
`on_attach`. One file per backend, not one shared file, so a project that
only ships to Steam never has to know EOS's config schema, and can
`.gitignore` just the backends whose credentials are genuinely sensitive.

## SDK delivery convention

A storefront SDK is developer-provided and **never committed** - vendors
either gate the download behind a partner account (Steamworks, EOS, GOG
Galaxy, Stove) or there's nothing to vendor at all (Microsoft Store's
`Windows.Services.Store` ships with the Windows SDK already on the machine).
Each backend that does need one follows `modules/live2d`'s precedent for a
big vendor SDK with its own idiosyncratic shape:

- `modules/store_<name>/third_party/` is gitignored per-module.
- A small `NxStore<Name>.cmake` resolves the extracted SDK root
  (`cmake/NxModules.cmake`'s `_nx_resolve_vendor_root`) and builds an
  `IMPORTED SHARED` target for the vendor's DLL/`.so`
  (`_nx_imported_shared_library`, which also stages the runtime file next to
  the built executable).
- A missing SDK is a `FATAL_ERROR` naming the vendor's download page, not a
  silent skip.

`cmake/NxModules.cmake`'s `nx_module_prebuilt()` is a *different* mechanism -
a portable, multi-toolset **static**-lib convention meant for a developer's
own small prebuilt middleware, not for a vendor's own DLL layout. Storefront
backends don't use it.

## Adding a new backend

Copy `modules/store_steam`'s shape: `NxStore<Name>.cmake` (if it vendors an
SDK) + `<name>_module.cpp` (provide whichever of the five services its SDK
supports, `on_attach` reads `store_<name>.ini`) + `<name>_config.h/.cpp` +
`manifest.json` + `tests/`. See that module's own comments for the concrete
worked example, and the CMake infra above for what's already shared.
