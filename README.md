# store module

The neutral storefront interface. `modules/store` itself talks to no
storefront - it only declares five independently-optional services
(`store_service.h`) and exposes the neutral `host.store_*` Luau surface
(`store_scripting.cpp`) that forwards to whichever backend module is active.
A storefront integration is a separate backend module - `modules/store_steam`,
`modules/store_egs`, `modules/store_gog` and `modules/store_stove` are built
and verified so far; see "Adding a new backend" below for the recipe the
rest (Microsoft Store, ...) follow.

The four show the same neutral interface accommodating structurally
different SDKs: Steam's calls are mostly synchronous (a local client cache),
while every single EOS call is asynchronous (a plain C completion callback
resolved by `EOS_Platform_Tick`, even for a "simple" read) - `store_egs`
handles this with small per-service caches populated by each query's
callback, with the neutral interface's synchronous methods reading whatever
is cached so far. EOS also splits its own API in two by auth method: a
silent, headless device-id login unlocks achievements/stats/cloud saves/
leaderboards, but entitlements/IAP/presence/friends need a real Epic account
session that has no headless equivalent - `store_egs` attempts the closest
thing (silently reusing a previously cached Epic login, if any) and
degrades those four to empty/`false` when it's unavailable, the same
absent-service shape Stove's missing cloud-saves/presence already
established. GOG Galaxy sits between the two: most calls are synchronous
local-cache reads like Steam's (`IStorage`, `IFriends`'s own persona/friend
accessors, `IStats`'s Get/Set once signed in), but DLC ownership, the
stats/achievements cache load, and the friends list are still listener-based
like EOS's - and unlike either, Galaxy has no per-call return-value error
signaling at all, so `store_gog` checks a thread-local `galaxy::api::
GetError()` after every call that can fail. Its sign-in is also the
narrowest of the three: `IUser::SignInGalaxy()` requires a real, locally
installed and running GOG Galaxy Client - there is no device-id-style
headless option the way EOS has.

STOVE is the outlier of the four: every one of its callbacks is a **plain C
function pointer with no userdata parameter at all** (contrast EOS's
trailing `void* ClientData` or GOG's listener-object dispatch), so
`store_stove` routes each callback through a static "current instance"
pointer per service class instead - safe only because exactly one store
backend module is ever active in a process at a time, the same invariant
`order_modules()` already enforces below. STOVE also assumes a launcher
already authenticated the user before the game process started (there is no
Login call in its SDK at all, only read-only accessors for the
already-signed-in session), and its achievements are entirely
server-computed from a Stat crossing a goal value the SDK never names -
`StoveAchievements::unlock()` has no honest implementation against this SDK
and always refuses, even though the rest of that service (numeric stats,
reading unlock status, listing achievement ids) works normally. This is a
different kind of gap than a whole service being absent (see below): one
method degraded within an otherwise-real service, the same way
`store_gog`'s `bytes_used()`/`bytes_total()` always report 0 for a missing
quota API while the rest of `StoreCloudSaves` works.

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
| `store.core` | `StoreCore` - ownership/entitlement checks, owned DLC listing | every backend provides this |
| `store.iap` | `StoreIap` - product listing + purchase flow | optional |
| `store.achievements` | `StoreAchievements` - achievements + numeric stats | optional |
| `store.cloud_saves` | `StoreCloudSaves` - key/value saves, listing, quota | optional |
| `store.presence` | `StorePresence` - rich presence + a real friends list | optional |

"Optional" means exactly what `ServiceProvider::find<T>` already does for any
absent service: it returns `nullptr`. A backend whose SDK has no cloud-save
or presence subsystem (STOVE, for one - its SDK genuinely has neither, only
a bare `Base_GetCloudSavingPath()` path string with no read/write/list API
attached, and zero friends/presence hits anywhere) simply never registers
`StoreCloudSaves`/`StorePresence`, and every neutral Luau function above
degrades to a safe `false`/empty return. There is no separate capability-flag
API to check first. `store_gog` is the same shape for a different service:
the vendored GOG Galaxy SDK has no purchase/checkout API at all (confirmed
absent from every header - only entitlement checks,
`IApps::IsDlcOwned()`/`IsDlcInstalled()`, which `store.core` already covers),
so it never registers `StoreIap` either.

A backend can also expose capabilities that don't belong in this neutral
interface at all - Steam's Workshop, leaderboards, and explicit overlay
control (`modules/store_steam/store_steam_workshop.h`,
`store_steam_leaderboards.h`, `store_steam_overlay.h`) are the worked
example. These aren't registered through `ServiceRegistry` (nothing outside
that one backend module will ever look them up) and get their own
`host.store_steam_*` Luau surface from a separate scripting file
(`store_steam_scripting.h/.cpp`), keeping `store/store_scripting.cpp` itself
strictly neutral.

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
