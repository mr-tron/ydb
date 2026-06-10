#pragma once

#include "param_bindings.h"
#include "source_common.h"

#include <ydb/mvp/meta/support_links/source.h>

#include <library/cpp/cgiparam/cgiparam.h>
#include <library/cpp/json/json_writer.h>

#include <util/generic/yexception.h>
#include <util/string/escape.h>

#include <utility>

namespace NMVP::NSupportLinks {

inline constexpr TStringBuf GRAFANA_LOGGING_DEFAULT_URL = "/explore";

inline TVector<TAdditionalParamBinding> BuildDefaultGrafanaLoggingAdditionalParamBindings() {
    return {};
}

inline TString BuildGrafanaLoggingExpr(const TVector<std::pair<TString, TString>>& bindings) {
    TString expr = "{";
    bool first = true;
    for (const auto& [name, value] : bindings) {
        if (!first) {
            expr += ", ";
        }
        first = false;
        expr += TStringBuilder() << name << "=\"" << EscapeC(value) << '"';
    }
    expr += "}";
    return expr;
}

inline NJson::TJsonValue BuildGrafanaLoggingPanesJson(const TString& datasource, const TVector<std::pair<TString, TString>>& bindings) {
    NJson::TJsonValue panesJson(NJson::JSON_MAP);
    NJson::TJsonValue paneJson(NJson::JSON_MAP);
    NJson::TJsonValue queryJson(NJson::JSON_MAP);

    queryJson["refId"] = "A";
    queryJson["expr"] = BuildGrafanaLoggingExpr(bindings);
    queryJson["queryType"] = "range";
    queryJson["direction"] = "backward";
    queryJson["datasource"]["type"] = "loki";
    queryJson["datasource"]["uid"] = datasource;

    paneJson["datasource"] = datasource;
    paneJson["queries"].AppendValue(std::move(queryJson));
    paneJson["range"]["from"] = "now-1h";
    paneJson["range"]["to"] = "now";

    panesJson["x"] = std::move(paneJson);
    return panesJson;
}

inline TResolvedParamBindings ResolveGrafanaLoggingParamBindings(
    const TSupportLinkEntryConfig& config,
    ESupportLinksEntityType entityType)
{
    return ResolveParamBindings(config, entityType, BuildDefaultGrafanaLoggingAdditionalParamBindings());
}

inline bool TryBuildGrafanaLoggingUrl(
    TStringBuf grafanaEndpoint,
    TStringBuf url,
    const THashMap<TString, TString>& clusterInfo,
    const NHttp::TUrlParameters& requestQueryParameters,
    const TResolvedParamBindings& paramBindings,
    TString& resolvedUrl,
    TString& errorMessage)
{
    const TStringBuf path = url.empty() ? GRAFANA_LOGGING_DEFAULT_URL : url;
    resolvedUrl = IsAbsoluteUrl(path) ? TString(path) : JoinUrl(grafanaEndpoint, path);

    if (resolvedUrl.Contains('?')) {
        errorMessage = "query parameters are not supported in url for source=grafana/logging";
        return false;
    }

    const auto datasourceIt = clusterInfo.find("datasource_logging");
    if (datasourceIt == clusterInfo.end() || datasourceIt->second.empty()) {
        errorMessage = "datasource_logging is required in cluster info for source=grafana/logging";
        return false;
    }

    TVector<std::pair<TString, TString>> bindings = BuildRequestParamValues(
        requestQueryParameters,
        paramBindings.RequestParams);
    for (auto& binding : BuildAdditionalParamValues(clusterInfo, paramBindings.AdditionalParams)) {
        bindings.push_back(std::move(binding));
    }

    TCgiParameters queryParameters;
    queryParameters.InsertUnescaped("schemaVersion", "1");
    queryParameters.InsertUnescaped("panes", NJson::WriteJson(
        BuildGrafanaLoggingPanesJson(datasourceIt->second, bindings),
        false));
    queryParameters.InsertUnescaped("orgId", "1");

    resolvedUrl += TStringBuilder() << '?' << queryParameters.Print();
    return true;
}

} // namespace NMVP::NSupportLinks

namespace NMVP {

class TGrafanaLoggingSource : public ILinkSource {
public:
    TGrafanaLoggingSource(
        TString sourceName,
        TString title,
        TString url,
        TString grafanaEndpoint,
        NSupportLinks::TResolvedParamBindings paramBindings)
        : SourceName(std::move(sourceName))
        , Title(std::move(title))
        , Url(std::move(url))
        , GrafanaEndpoint(std::move(grafanaEndpoint))
        , ParamBindings(std::move(paramBindings))
    {}

    TResolveOutput Resolve(const TLinkResolveInput& input, const TResolveContext&) const override {
        TResolveOutput result{
            .Name = SourceName,
        };

        TString resolvedUrl;
        TString errorMessage;
        if (!NSupportLinks::TryBuildGrafanaLoggingUrl(
                GrafanaEndpoint,
                Url,
                input.ClusterInfo,
                input.UrlParameters,
                ParamBindings,
                resolvedUrl,
                errorMessage))
        {
            result.Errors.emplace_back(NSupportLinks::TSupportError{
                .Source = SourceName,
                .Message = std::move(errorMessage),
            });
            return result;
        }

        result.Links.emplace_back(NSupportLinks::TResolvedLink{
            .Title = Title,
            .Url = std::move(resolvedUrl),
        });
        return result;
    }

private:
    TString SourceName;
    TString Title;
    TString Url;
    TString GrafanaEndpoint;
    NSupportLinks::TResolvedParamBindings ParamBindings;
};

inline void ValidateGrafanaLoggingSourceConfig(
    const TSupportLinkEntryConfig& config,
    ESupportLinksEntityType entityType,
    const TMetaSettings& metaSettings)
{
    const TStringBuf url = config.GetUrl().empty()
        ? NSupportLinks::GRAFANA_LOGGING_DEFAULT_URL
        : TStringBuf(config.GetUrl());
    if (url.Contains('?')) {
        ythrow yexception() << "query parameters are not supported in url for source=" << config.GetSource();
    }
    if (!NSupportLinks::IsAbsoluteUrl(url) && metaSettings.SupportLinks.GrafanaEndpoint.empty()) {
        ythrow yexception() << "grafana.endpoint is required for relative url";
    }
    NSupportLinks::ValidateResolvedParamBindings(
        NSupportLinks::ResolveGrafanaLoggingParamBindings(config, entityType),
        config);
}

inline std::shared_ptr<ILinkSource> MakeGrafanaLoggingSource(
    TSupportLinkEntryConfig config,
    ESupportLinksEntityType entityType,
    const TMetaSettings& metaSettings)
{
    ValidateGrafanaLoggingSourceConfig(config, entityType, metaSettings);
    auto paramBindings = NSupportLinks::ResolveGrafanaLoggingParamBindings(config, entityType);
    return std::make_shared<TGrafanaLoggingSource>(
        config.GetSource(),
        config.GetTitle(),
        config.GetUrl(),
        metaSettings.SupportLinks.GrafanaEndpoint,
        std::move(paramBindings)
    );
}

} // namespace NMVP
