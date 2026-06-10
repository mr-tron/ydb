#pragma once

#include "param_bindings.h"
#include "source_common.h"
#include "types.h"

#include <library/cpp/cgiparam/cgiparam.h>

namespace NMVP::NSupportLinks {

inline TVector<TAdditionalParamBinding> BuildDefaultGrafanaDashboardAdditionalParamBindings() {
    return {};
}

inline std::pair<TString, TCgiParameters> BuildGrafanaDashboardUrlParts(
    TStringBuf grafanaEndpoint,
    TStringBuf url)
{
    TString resolvedUrl = IsAbsoluteUrl(url)
        ? TString(url)
        : JoinUrl(grafanaEndpoint, url);
    TString path = TString(TStringBuf(resolvedUrl).Before('?'));
    const TStringBuf queryString = TStringBuf(resolvedUrl).After('?');

    TCgiParameters queryParameters;
    if (!queryString.empty()) {
        queryParameters.Scan(queryString);
    }

    return {std::move(path), std::move(queryParameters)};
}

inline void ApplyGrafanaDashboardBindingPolicy(
    TCgiParameters& queryParameters,
    const THashMap<TString, TString>& clusterInfo,
    const NHttp::TUrlParameters& requestUrlParameters,
    const TResolvedParamBindings& paramBindings)
{
    for (const auto& [label, value] : BuildAdditionalParamValues(clusterInfo, paramBindings.AdditionalParams)) {
        queryParameters.InsertUnescaped(TStringBuilder() << "var-" << label, value);
    }

    for (const auto& [label, value] : BuildRequestParamValues(requestUrlParameters, paramBindings.RequestParams)) {
        queryParameters.InsertUnescaped(TStringBuilder() << "var-" << label, value);
    }
}

inline TString BuildGrafanaDashboardUrl(
    TStringBuf grafanaEndpoint,
    TStringBuf url,
    const THashMap<TString, TString>& clusterInfo,
    const NHttp::TUrlParameters& requestUrlParameters,
    const TResolvedParamBindings& paramBindings)
{
    auto [path, queryParameters] = BuildGrafanaDashboardUrlParts(grafanaEndpoint, url);
    ApplyGrafanaDashboardBindingPolicy(queryParameters, clusterInfo, requestUrlParameters, paramBindings);

    return queryParameters.empty()
        ? path
        : TStringBuilder() << path << '?' << queryParameters.Print();
}

inline TResolvedParamBindings ResolveGrafanaDashboardParamBindings(
    const TSupportLinkEntryConfig& config,
    ESupportLinksEntityType entityType)
{
    return ResolveParamBindings(config, entityType, BuildDefaultGrafanaDashboardAdditionalParamBindings());
}

} // namespace NMVP::NSupportLinks
