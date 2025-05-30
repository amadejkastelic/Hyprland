#include "ShortcutsInhibit.hpp"
#include <algorithm>
#include "../Compositor.hpp"
#include "core/Compositor.hpp"

CKeyboardShortcutsInhibitor::CKeyboardShortcutsInhibitor(SP<CZwpKeyboardShortcutsInhibitorV1> resource_, SP<CWLSurfaceResource> surf) : resource(resource_), pSurface(surf) {
    if UNLIKELY (!resource->resource())
        return;

    resource->setDestroy([this](CZwpKeyboardShortcutsInhibitorV1* pMgr) { PROTO::shortcutsInhibit->destroyInhibitor(this); });
    resource->setOnDestroy([this](CZwpKeyboardShortcutsInhibitorV1* pMgr) { PROTO::shortcutsInhibit->destroyInhibitor(this); });

    // I don't really care about following the spec here that much,
    // let's make the app believe it's always active
    resource->sendActive();
}

SP<CWLSurfaceResource> CKeyboardShortcutsInhibitor::surface() {
    return pSurface.lock();
}

bool CKeyboardShortcutsInhibitor::good() {
    return resource->resource();
}

CKeyboardShortcutsInhibitProtocol::CKeyboardShortcutsInhibitProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CKeyboardShortcutsInhibitProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_vManagers.emplace_back(makeUnique<CZwpKeyboardShortcutsInhibitManagerV1>(client, ver, id)).get();
    RESOURCE->setOnDestroy([this](CZwpKeyboardShortcutsInhibitManagerV1* p) { this->onManagerResourceDestroy(p->resource()); });

    RESOURCE->setDestroy([this](CZwpKeyboardShortcutsInhibitManagerV1* pMgr) { this->onManagerResourceDestroy(pMgr->resource()); });
    RESOURCE->setInhibitShortcuts(
        [this](CZwpKeyboardShortcutsInhibitManagerV1* pMgr, uint32_t id, wl_resource* surface, wl_resource* seat) { this->onInhibit(pMgr, id, surface, seat); });
}

void CKeyboardShortcutsInhibitProtocol::onManagerResourceDestroy(wl_resource* res) {
    std::erase_if(m_vManagers, [&](const auto& other) { return other->resource() == res; });
}

void CKeyboardShortcutsInhibitProtocol::destroyInhibitor(CKeyboardShortcutsInhibitor* inhibitor) {
    std::erase_if(m_vInhibitors, [&](const auto& other) { return other.get() == inhibitor; });
}

void CKeyboardShortcutsInhibitProtocol::onInhibit(CZwpKeyboardShortcutsInhibitManagerV1* pMgr, uint32_t id, wl_resource* surface, wl_resource* seat) {
    SP<CWLSurfaceResource> surf   = CWLSurfaceResource::fromResource(surface);
    const auto             CLIENT = pMgr->client();

    for (auto const& in : m_vInhibitors) {
        if LIKELY (in->surface() != surf)
            continue;

        pMgr->error(ZWP_KEYBOARD_SHORTCUTS_INHIBIT_MANAGER_V1_ERROR_ALREADY_INHIBITED, "Already inhibited for surface resource");
        return;
    }

    const auto RESOURCE =
        m_vInhibitors.emplace_back(makeUnique<CKeyboardShortcutsInhibitor>(makeShared<CZwpKeyboardShortcutsInhibitorV1>(CLIENT, pMgr->version(), id), surf)).get();

    if UNLIKELY (!RESOURCE->good()) {
        pMgr->noMemory();
        m_vInhibitors.pop_back();
        LOGM(ERR, "Failed to create an inhibitor resource");
        return;
    }
}

bool CKeyboardShortcutsInhibitProtocol::isInhibited() {
    if (!g_pCompositor->m_lastFocus)
        return false;

    if (const auto PWINDOW = g_pCompositor->getWindowFromSurface(g_pCompositor->m_lastFocus.lock()); PWINDOW && PWINDOW->m_sWindowData.noShortcutsInhibit.valueOrDefault())
        return false;

    for (auto const& in : m_vInhibitors) {
        if (in->surface() != g_pCompositor->m_lastFocus)
            continue;

        return true;
    }

    return false;
}
