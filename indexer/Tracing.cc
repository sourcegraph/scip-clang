#include "perfetto/perfetto.h"

#include "indexer/Tracing.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace scip_clang {

void initializeTracing() {
  perfetto::TracingInitArgs args{};
  args.backends |= perfetto::kSystemBackend;
  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();
}

} // namespace scip_clang