#ifndef SCIP_CLANG_TRACING_H
#define SCIP_CLANG_TRACING_H

#include "perfetto/perfetto.h"

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("preprocessor")
        .SetDescription("Events related to preprocessing"),
    perfetto::Category("semantic-analysis")
        .SetDescription("Events related to semantic analysis"),
    perfetto::Category("planning")
        .SetDescription("Events related to header planning"),
    perfetto::Category("ipc").SetDescription(
        "Events related to IPC communication"),
    perfetto::Category("serialization")
        .SetDescription("Events related to (de)serialization of indexes"),
    perfetto::Category("index-merging")
        .SetDescription("Events related to index merging"));

namespace scip_clang {
void initializeTracing();
}

#endif // SCIP_CLANG_TRACING_H