#pragma once

#include "source.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>

namespace NMVP::NSupportLinks {

struct TAdditionalParamBinding {
    enum class EValueSource {
        ClusterInfo,
        Value,
    };

    TString Label;
    EValueSource ValueSource = EValueSource::ClusterInfo;
    TString SourceValue;
};

struct TResolvedParamBindings {
    TVector<std::pair<TString, TString>> RequestParams;
    TVector<TAdditionalParamBinding> AdditionalParams;
};

inline TStringBuf GetSupportLinksEntityName(ESupportLinksEntityType entityType) {
    switch (entityType) {
        case ESupportLinksEntityType::Cluster:
            return "cluster";
        case ESupportLinksEntityType::Database:
            return "database";
    }
    ythrow yexception() << "unsupported support_links entity type";
}

inline TVector<TString> BuildDefaultRequestParamNames(ESupportLinksEntityType entityType) {
    TVector<TString> requestParamNames;
    requestParamNames.push_back("cluster");
    if (entityType == ESupportLinksEntityType::Database) {
        requestParamNames.push_back("database");
    }
    return requestParamNames;
}

inline THashSet<TString> BuildAllowedRequestParamNames(ESupportLinksEntityType entityType) {
    THashSet<TString> allowedRequestParamNames;
    for (const auto& requestParamName : BuildDefaultRequestParamNames(entityType)) {
        allowedRequestParamNames.insert(requestParamName);
    }
    return allowedRequestParamNames;
}

inline const TSupportLinkEntryConfig::TRequestParamConfig* FindRequestParamOverride(
    const TSupportLinkEntryConfig& config,
    TStringBuf requestParamName)
{
    const auto& requestParams = config.GetRequestParams();
    if (requestParamName == "cluster") {
        return requestParams.HasCluster() ? &requestParams.GetCluster() : nullptr;
    }
    if (requestParamName == "database") {
        return requestParams.HasDatabase() ? &requestParams.GetDatabase() : nullptr;
    }
    ythrow yexception() << "unsupported request parameter override name: " << requestParamName;
}

inline void ValidateUnsupportedRequestParamOverride(
    const TSupportLinkEntryConfig& config,
    ESupportLinksEntityType entityType,
    const THashSet<TString>& allowedRequestParamNames,
    TStringBuf requestParamName)
{
    if (allowedRequestParamNames.contains(requestParamName) || !FindRequestParamOverride(config, requestParamName)) {
        return;
    }

    ythrow yexception()
        << "request_params." << requestParamName
        << " is not supported for entity=" << GetSupportLinksEntityName(entityType)
        << " and source=" << config.GetSource();
}

inline TVector<std::pair<TString, TString>> ResolveRequestParamBindings(
    const TSupportLinkEntryConfig& config,
    ESupportLinksEntityType entityType)
{
    const THashSet<TString> allowedRequestParamNames = BuildAllowedRequestParamNames(entityType);
    ValidateUnsupportedRequestParamOverride(config, entityType, allowedRequestParamNames, "cluster");
    ValidateUnsupportedRequestParamOverride(config, entityType, allowedRequestParamNames, "database");

    TVector<std::pair<TString, TString>> requestParams;
    for (const auto& requestParamName : BuildDefaultRequestParamNames(entityType)) {
        const auto* requestParamConfig = FindRequestParamOverride(config, requestParamName);
        if (!requestParamConfig) {
            requestParams.emplace_back(requestParamName, requestParamName);
            continue;
        }

        if (!requestParamConfig->GetForward()) {
            if (!requestParamConfig->GetForwardTo().empty()) {
                ythrow yexception()
                    << "request_params." << requestParamName
                    << " cannot set both forward=false and forward_to for source=" << config.GetSource();
            }
            continue;
        }

        if (requestParamConfig->GetForwardTo().empty()) {
            requestParams.emplace_back(requestParamName, requestParamName);
            continue;
        }

        requestParams.emplace_back(requestParamName, requestParamConfig->GetForwardTo());
    }

    return requestParams;
}

inline TVector<TAdditionalParamBinding> ResolveAdditionalParamBindings(
    const TSupportLinkEntryConfig& config,
    const TVector<TAdditionalParamBinding>& defaultAdditionalParams)
{
    if (config.AdditionalParamsSize() == 0) {
        return defaultAdditionalParams;
    }

    TVector<TAdditionalParamBinding> additionalParams;
    additionalParams.reserve(config.AdditionalParamsSize());

    const int additionalParamsSize = config.AdditionalParamsSize();
    for (int i = 0; i < additionalParamsSize; ++i) {
        const auto& additionalParamConfig = config.GetAdditionalParams(i);
        if (additionalParamConfig.GetLabel().empty()) {
            ythrow yexception() << "additional_params.label is required for source=" << config.GetSource();
        }

        const bool hasFromClusterInfo = !additionalParamConfig.GetFromClusterInfo().empty();
        const bool hasValue = !additionalParamConfig.GetValue().empty();
        if (hasFromClusterInfo == hasValue) {
            ythrow yexception()
                << "additional_params.label=" << additionalParamConfig.GetLabel()
                << " must set exactly one of from_cluster_info or value for source=" << config.GetSource();
        }

        TAdditionalParamBinding binding;
        binding.Label = additionalParamConfig.GetLabel();
        if (hasFromClusterInfo) {
            binding.ValueSource = TAdditionalParamBinding::EValueSource::ClusterInfo;
            binding.SourceValue = additionalParamConfig.GetFromClusterInfo();
        } else {
            binding.ValueSource = TAdditionalParamBinding::EValueSource::Value;
            binding.SourceValue = additionalParamConfig.GetValue();
        }
        additionalParams.push_back(std::move(binding));
    }

    return additionalParams;
}

inline TResolvedParamBindings ResolveParamBindings(
    const TSupportLinkEntryConfig& config,
    ESupportLinksEntityType entityType,
    const TVector<TAdditionalParamBinding>& defaultAdditionalParams)
{
    return TResolvedParamBindings{
        .RequestParams = ResolveRequestParamBindings(config, entityType),
        .AdditionalParams = ResolveAdditionalParamBindings(config, defaultAdditionalParams),
    };
}

inline void ValidateResolvedParamBindings(
    const TResolvedParamBindings& paramBindings,
    const TSupportLinkEntryConfig& config)
{
    THashSet<TString> labels;

    for (const auto& [requestParamName, targetLabel] : paramBindings.RequestParams) {
        Y_UNUSED(requestParamName);
        if (!labels.insert(targetLabel).second) {
            ythrow yexception()
                << "duplicate target label '" << targetLabel
                << "' in request_params for source=" << config.GetSource();
        }
    }

    for (const auto& additionalParam : paramBindings.AdditionalParams) {
        if (!labels.insert(additionalParam.Label).second) {
            ythrow yexception()
                << "duplicate target label '" << additionalParam.Label
                << "' in additional_params for source=" << config.GetSource();
        }
    }
}

inline TVector<std::pair<TString, TString>> BuildRequestParamValues(
    const NHttp::TUrlParameters& requestUrlParameters,
    const TVector<std::pair<TString, TString>>& requestParamBindings)
{
    TVector<std::pair<TString, TString>> paramValues;

    for (const auto& [requestParamName, targetLabel] : requestParamBindings) {
        const TString value = requestUrlParameters[requestParamName];
        if (!value.empty()) {
            paramValues.emplace_back(targetLabel, value);
        }
    }

    return paramValues;
}

inline TVector<std::pair<TString, TString>> BuildAdditionalParamValues(
    const THashMap<TString, TString>& clusterInfo,
    const TVector<TAdditionalParamBinding>& additionalParamBindings)
{
    TVector<std::pair<TString, TString>> paramValues;

    for (const auto& additionalParam : additionalParamBindings) {
        if (additionalParam.ValueSource == TAdditionalParamBinding::EValueSource::Value) {
            paramValues.emplace_back(additionalParam.Label, additionalParam.SourceValue);
            continue;
        }

        const auto it = clusterInfo.find(additionalParam.SourceValue);
        if (it != clusterInfo.end() && !it->second.empty()) {
            paramValues.emplace_back(additionalParam.Label, it->second);
        }
    }

    return paramValues;
}

} // namespace NMVP::NSupportLinks
