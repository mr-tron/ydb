#include "source.h"
#include "grafana_dashboard_source.h"
#include "grafana_dashboard_search_source.h"
#include "grafana_logging_source.h"

#include <util/generic/yexception.h>

namespace NMVP {

void ValidateSupportLinksConfig(const TSupportLinksConfig& supportLinks, const TMetaSettings& metaSettings) {
    for (int i = 0; i < supportLinks.GetCluster().size(); ++i) {
        ValidateLinkSourceConfig(supportLinks.GetCluster(i), ESupportLinksEntityType::Cluster, metaSettings);
    }
    for (int i = 0; i < supportLinks.GetDatabase().size(); ++i) {
        ValidateLinkSourceConfig(supportLinks.GetDatabase(i), ESupportLinksEntityType::Database, metaSettings);
    }
}

void ValidateLinkSourceConfig(
    const TSupportLinkEntryConfig& config,
    ESupportLinksEntityType entityType,
    const TMetaSettings& metaSettings)
{
    if (config.GetSource().empty()) {
        ythrow yexception() << "source is required";
    }

    if (config.GetSource() == "grafana/dashboard") {
        ValidateGrafanaDashboardSourceConfig(config, entityType, metaSettings);
        return;
    }
    if (config.GetSource() == "grafana/dashboard/search") {
        ValidateGrafanaDashboardSearchSourceConfig(config, entityType, metaSettings);
        return;
    }
    if (config.GetSource() == "grafana/logging") {
        ValidateGrafanaLoggingSourceConfig(config, entityType, metaSettings);
        return;
    }

    ythrow yexception() << "unsupported support_links source: " << config.GetSource();
}

std::shared_ptr<ILinkSource> MakeLinkSource(
    TSupportLinkEntryConfig config,
    ESupportLinksEntityType entityType,
    const TMetaSettings& metaSettings)
{
    ValidateLinkSourceConfig(config, entityType, metaSettings);

    if (config.GetSource() == "grafana/dashboard") {
        return MakeGrafanaDashboardSource(std::move(config), entityType, metaSettings);
    }
    if (config.GetSource() == "grafana/dashboard/search") {
        return MakeGrafanaDashboardSearchSource(std::move(config), entityType, metaSettings);
    }
    if (config.GetSource() == "grafana/logging") {
        return MakeGrafanaLoggingSource(std::move(config), entityType, metaSettings);
    }

    ythrow yexception() << "unsupported support_links source: " << config.GetSource();
}

} // namespace NMVP
