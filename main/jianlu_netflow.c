// main/jianlu_netflow.c —— 见 jianlu_netflow.h。
#include "jianlu_netflow.h"

bool jianlu_netflow_event(jianlu_nf_t *state, jianlu_nf_ev_t ev)
{
    jianlu_nf_t s = *state;
    switch (ev) {
    case JIANLU_NF_START_NO_CREDS:
    case JIANLU_NF_REPROVISION:
        *state = JIANLU_NF_PROVISIONING;
        return true;
    case JIANLU_NF_START_WITH_CREDS:
        *state = JIANLU_NF_CONNECTING;
        return true;
    case JIANLU_NF_GOT_CREDS:
        if (s != JIANLU_NF_PROVISIONING) return false;
        *state = JIANLU_NF_CONNECTING;
        return true;
    case JIANLU_NF_GOT_IP:
        if (s != JIANLU_NF_CONNECTING && s != JIANLU_NF_PROVISIONING) return false;
        *state = JIANLU_NF_DISCOVERING;
        return true;
    case JIANLU_NF_CONNECT_FAIL:
        if (s != JIANLU_NF_CONNECTING) return false;
        *state = JIANLU_NF_PROVISIONING;
        return true;
    case JIANLU_NF_DISCONNECT:
        if (s != JIANLU_NF_READY) return false;
        *state = JIANLU_NF_CONNECTING;
        return true;
    case JIANLU_NF_HUB_READY:
        if (s != JIANLU_NF_DISCOVERING) return false;
        *state = JIANLU_NF_LOADING;
        return true;
    case JIANLU_NF_HUB_FAIL:
        if (s != JIANLU_NF_DISCOVERING) return false;
        *state = JIANLU_NF_ERROR;
        return true;
    case JIANLU_NF_FETCH_OK:
        if (s != JIANLU_NF_LOADING) return false;
        *state = JIANLU_NF_READY;
        return true;
    case JIANLU_NF_FETCH_FAIL:
        if (s != JIANLU_NF_LOADING) return false;
        *state = JIANLU_NF_ERROR;
        return true;
    }
    return false;
}

jianlu_view_t jianlu_netflow_view(jianlu_nf_t state)
{
    switch (state) {
    case JIANLU_NF_PROVISIONING: return JIANLU_VIEW_PROVISIONING;
    case JIANLU_NF_CONNECTING:   return JIANLU_VIEW_CONNECTING;
    case JIANLU_NF_DISCOVERING:  return JIANLU_VIEW_DISCOVERING;
    case JIANLU_NF_LOADING:      return JIANLU_VIEW_LOADING;
    case JIANLU_NF_READY:        return JIANLU_VIEW_READY;
    default:                     return JIANLU_VIEW_ERROR;
    }
}
