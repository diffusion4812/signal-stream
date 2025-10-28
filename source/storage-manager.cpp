#include "storage-manager.h"

SubmitResult ProducerToken::try_submit(std::vector<std::byte>&& batch) const {
    if (!mgr_) return SubmitResult::UnknownStream;
    return mgr_->submit_batch_for_stream(streamId_, std::move(batch));
}