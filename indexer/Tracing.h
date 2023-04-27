#ifndef SCIP_CLANG_TRACING_H
#define SCIP_CLANG_TRACING_H

#include "perfetto/perfetto.h"

namespace scip_clang {

void initializeTracing();

namespace tracing {

constexpr const char *preprocessor = "preprocessor";
constexpr const char *planning = "planning";
constexpr const char *scheduling = "scheduling";
constexpr const char *ipc = "ipc";
constexpr const char *indexIo = "index-i/o";
constexpr const char *indexMerging = "indexMerging";
constexpr const char *indexing = "indexing";

} // namespace tracing

} // namespace scip_clang

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(scip_clang::tracing::preprocessor)
        .SetDescription("Events related to preprocessing"),
    perfetto::Category(scip_clang::tracing::planning)
        .SetDescription("Events related to header planning"),
    perfetto::Category(scip_clang::tracing::scheduling)
        .SetDescription("Events related to scheduling jobs"),
    perfetto::Category(scip_clang::tracing::ipc)
        .SetDescription("Events related to IPC communication"),
    perfetto::Category(scip_clang::tracing::indexIo)
        .SetDescription("Events related to (de)serialization of indexes"),
    perfetto::Category(scip_clang::tracing::indexMerging)
        .SetDescription("Events related to index merging"),
    perfetto::Category(scip_clang::tracing::indexing)
        .SetDescription("Events related to indexing in a worker"));

#endif // SCIP_CLANG_TRACING_H