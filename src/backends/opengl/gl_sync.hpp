#pragma once

// GL completion fences, host-value semaphores and query pools.
//
// Context ownership: each instance owns one GL context and one graphics
// queue. Every native call takes Context::mutex and makes the context current
// on the calling thread, so any thread may record and submit, but GL work is
// serialized. A submission completes on the GPU before it returns, so RHI
// fences and semaphores are host timeline values reached at submit.
//
// With TRUFFLE_GL_HAS_SYNC_OBJECTS (GL 3.2 sync objects; macOS GL 4.1),
// completion is a glFenceSync waited with glClientWaitSync and a bounded
// timeout, and occlusion query pools map to GL_SAMPLES_PASSED. Timestamp
// query pools are unsupported: Apple's GL 4.1 reports zero
// GL_QUERY_COUNTER_BITS for GL_TIMESTAMP and returns zero counters. Pipeline
// statistics queries need GL 4.6 and are unsupported.

#include "foundation_backend.hpp"

#include "gl_api.hpp"
#if defined(TRUFFLE_GL_CONTEXT_CGL)
#include "cgl_context.hpp"
#else
#include "egl_context.hpp"
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace truffle::rhi::detail::gl_probe {

// One submission or presentation that has not completed in this time is
// reported as device loss instead of blocking the caller forever.
inline constexpr std::uint64_t kCompletionTimeoutNanoseconds =
    10'000'000'000ULL;

// Waits until all GL work issued on the current context has completed.
[[nodiscard]] inline Status wait_for_completion(std::string_view what) {
#if defined(TRUFFLE_GL_HAS_SYNC_OBJECTS)
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (sync == nullptr) {
        return Status::failure(StatusCode::backend_validation_failed,
                               std::string{what} +
                                   ": glFenceSync did not create a fence");
    }
    const auto result = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
                                         kCompletionTimeoutNanoseconds);
    glDeleteSync(sync);
    switch (result) {
    case GL_ALREADY_SIGNALED:
    case GL_CONDITION_SATISFIED:
        return Status::success();
    case GL_TIMEOUT_EXPIRED:
        return Status::failure(StatusCode::device_lost,
                               std::string{what} +
                                   " did not complete within 10 seconds");
    default:
        return Status::failure(StatusCode::backend_validation_failed,
                               std::string{what} +
                                   ": glClientWaitSync failed");
    }
#else
    (void)what;
    glFinish();
    return Status::success();
#endif
}

#if defined(TRUFFLE_GL_HAS_SYNC_OBJECTS)

// Semaphores are host timeline values. The foundation waits for them before
// native submission, and the single GL queue completes each submission
// before its signals are published, so no GL object is needed per point.
struct SemaphoreResource {
    std::shared_ptr<Context> context;
};

[[nodiscard]] inline Result<std::shared_ptr<void>> create_semaphore(
    const std::shared_ptr<void>& nativeContext, const SemaphoreDesc&) {
    auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    auto semaphore = std::make_shared<SemaphoreResource>();
    semaphore->context = std::move(context);
    return std::static_pointer_cast<void>(std::move(semaphore));
}

struct QueryPoolResource {
    ~QueryPoolResource() {
        if (!context || names.empty()) {
            return;
        }
        std::lock_guard contextLock{context->mutex};
        if (make_current(*context).ok()) {
            glDeleteQueries(static_cast<GLsizei>(names.size()), names.data());
        }
    }

    std::shared_ptr<Context> context;
    QueryType type = QueryType::occlusion;
    std::vector<GLuint> names;
    // A GL query name has no result until it has been begun and ended; these
    // are guarded by Context::mutex like every other GL call.
    std::vector<bool> ended;
    std::uint32_t active = UINT32_MAX;
};

[[nodiscard]] inline Result<std::shared_ptr<void>> create_query_pool(
    const std::shared_ptr<void>& nativeContext, const QueryPoolDesc& desc) {
    auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    if (desc.type == QueryType::timestamp) {
        return Status::failure(
            StatusCode::unsupported,
            "GL timestamp queries are unavailable: the context reports zero "
            "GL_TIMESTAMP counter bits");
    }
    if (desc.type != QueryType::occlusion) {
        return Status::failure(
            StatusCode::unsupported,
            "GL pipeline statistics queries require GL 4.6");
    }
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    auto pool = std::make_shared<QueryPoolResource>();
    pool->type = desc.type;
    pool->names.resize(desc.count);
    pool->ended.assign(desc.count, false);
    glGenQueries(static_cast<GLsizei>(desc.count), pool->names.data());
    if (glGetError() != GL_NO_ERROR) {
        glDeleteQueries(static_cast<GLsizei>(desc.count), pool->names.data());
        return Status::failure(StatusCode::backend_validation_failed,
                               "GL could not create query objects");
    }
    pool->context = std::move(context);
    return std::static_pointer_cast<void>(std::move(pool));
}

// Executes one query command on the current context. resolve_queries reads
// each result (waiting for it) and writes uint64 values into the
// destination buffer object.
[[nodiscard]] inline Status execute_query(const NativeCommand& command,
                                          GLuint destinationBuffer) {
    const auto pool = std::static_pointer_cast<QueryPoolResource>(command.object);
    if (!pool) {
        return Status::failure(StatusCode::invalid_argument,
                               "GL query pool is invalid");
    }
    const auto query = static_cast<std::uint32_t>(command.arguments[0]);
    switch (command.kind) {
    case NativeCommandKind::begin_occlusion_query:
        if (pool->active != UINT32_MAX) {
            return Status::failure(StatusCode::invalid_state,
                                   "a GL occlusion query is already active");
        }
        glBeginQuery(GL_SAMPLES_PASSED, pool->names[query]);
        pool->active = query;
        pool->ended[query] = false;
        return Status::success();
    case NativeCommandKind::end_occlusion_query:
        if (pool->active != query) {
            return Status::failure(StatusCode::invalid_state,
                                   "GL occlusion query end does not match begin");
        }
        glEndQuery(GL_SAMPLES_PASSED);
        pool->active = UINT32_MAX;
        pool->ended[query] = true;
        return Status::success();
    case NativeCommandKind::resolve_queries: {
        const auto count = static_cast<std::uint32_t>(command.arguments[1]);
        const auto offset = static_cast<GLintptr>(command.arguments[2]);
        if (destinationBuffer == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "GL query resolve has no destination buffer");
        }
        std::vector<GLuint64> values(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            if (!pool->ended[query + index]) {
                return Status::failure(StatusCode::invalid_state,
                                       "GL query result is not available");
            }
            glGetQueryObjectui64v(pool->names[query + index], GL_QUERY_RESULT,
                                  &values[index]);
        }
        glBindBuffer(GL_COPY_WRITE_BUFFER, destinationBuffer);
        glBufferSubData(GL_COPY_WRITE_BUFFER, offset,
                        static_cast<GLsizeiptr>(values.size() *
                                                sizeof(GLuint64)),
                        values.data());
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        return Status::success();
    }
    default:
        return Status::failure(StatusCode::unsupported,
                               "GL timestamp queries are unavailable");
    }
}

#endif

} // namespace truffle::rhi::detail::gl_probe
