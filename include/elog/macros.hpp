#pragma once

#include "aggregate.hpp"
#include "emit.hpp"
#include "format.hpp"
#include "level.hpp"
#include "logger.hpp"

#define ELOG_INTERNAL_F(logger_expr, lvl_value, fmt_str, ...)                   \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_F(...) ELOG_INTERNAL_F(::elog::default_logger(), ::elog::Level::TRACE, __VA_ARGS__)
#define LOG_DEBUG_F(...) ELOG_INTERNAL_F(::elog::default_logger(), ::elog::Level::DEBUG, __VA_ARGS__)
#define LOG_INFO_F(...)  ELOG_INTERNAL_F(::elog::default_logger(), ::elog::Level::INFO,  __VA_ARGS__)
#define LOG_WARN_F(...)  ELOG_INTERNAL_F(::elog::default_logger(), ::elog::Level::WARN,  __VA_ARGS__)
#define LOG_ERROR_F(...) ELOG_INTERNAL_F(::elog::default_logger(), ::elog::Level::ERROR, __VA_ARGS__)
#define LOG_FATAL_F(...) ELOG_INTERNAL_F(::elog::default_logger(), ::elog::Level::FATAL, __VA_ARGS__)

#define LOGGER_F(logger, lvl, ...) ELOG_INTERNAL_F(logger, lvl, __VA_ARGS__)

// =====================================================================
//  Aggregation / rate-limit macros.
//
//  Each adds an extra "gate" before emit. The gate runs ONLY after the
//  level check passes. The format args appear only inside the emit_f
//  call; if the gate returns false, args are never evaluated (lazy).
// =====================================================================

// ---- LOG_*_EVERY_N(n, fmt, ...) ----
#define ELOG_INTERNAL_EVERY_N(logger_expr, lvl_value, n_val, fmt_str, ...)      \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        static ::elog::detail::EveryNState __elog_st;                           \
        if (!__elog_st.tick(static_cast<std::uint64_t>(n_val))) break;          \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_EVERY_N(n, ...) ELOG_INTERNAL_EVERY_N(::elog::default_logger(), ::elog::Level::TRACE, n, __VA_ARGS__)
#define LOG_DEBUG_EVERY_N(n, ...) ELOG_INTERNAL_EVERY_N(::elog::default_logger(), ::elog::Level::DEBUG, n, __VA_ARGS__)
#define LOG_INFO_EVERY_N(n, ...)  ELOG_INTERNAL_EVERY_N(::elog::default_logger(), ::elog::Level::INFO,  n, __VA_ARGS__)
#define LOG_WARN_EVERY_N(n, ...)  ELOG_INTERNAL_EVERY_N(::elog::default_logger(), ::elog::Level::WARN,  n, __VA_ARGS__)
#define LOG_ERROR_EVERY_N(n, ...) ELOG_INTERNAL_EVERY_N(::elog::default_logger(), ::elog::Level::ERROR, n, __VA_ARGS__)
#define LOG_FATAL_EVERY_N(n, ...) ELOG_INTERNAL_EVERY_N(::elog::default_logger(), ::elog::Level::FATAL, n, __VA_ARGS__)
#define LOGGER_EVERY_N(logger, lvl, n, ...) ELOG_INTERNAL_EVERY_N(logger, lvl, n, __VA_ARGS__)

// ---- LOG_*_EVERY_N_SEC(s, fmt, ...) ----
#define ELOG_INTERNAL_EVERY_TIME(logger_expr, lvl_value, period_ns_val, fmt_str, ...) \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        static ::elog::detail::EveryTimeState __elog_st;                        \
        if (!__elog_st.tick(static_cast<std::int64_t>(period_ns_val))) break;   \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_EVERY_N_SEC(s, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::TRACE, (static_cast<std::int64_t>(s) * 1000000000LL), __VA_ARGS__)
#define LOG_DEBUG_EVERY_N_SEC(s, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::DEBUG, (static_cast<std::int64_t>(s) * 1000000000LL), __VA_ARGS__)
#define LOG_INFO_EVERY_N_SEC(s, ...)  ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::INFO,  (static_cast<std::int64_t>(s) * 1000000000LL), __VA_ARGS__)
#define LOG_WARN_EVERY_N_SEC(s, ...)  ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::WARN,  (static_cast<std::int64_t>(s) * 1000000000LL), __VA_ARGS__)
#define LOG_ERROR_EVERY_N_SEC(s, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::ERROR, (static_cast<std::int64_t>(s) * 1000000000LL), __VA_ARGS__)
#define LOG_FATAL_EVERY_N_SEC(s, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::FATAL, (static_cast<std::int64_t>(s) * 1000000000LL), __VA_ARGS__)
#define LOGGER_EVERY_N_SEC(logger, lvl, s, ...) ELOG_INTERNAL_EVERY_TIME(logger, lvl, (static_cast<std::int64_t>(s) * 1000000000LL), __VA_ARGS__)

// ---- LOG_*_EVERY_N_MS(ms, fmt, ...) ----
#define LOG_TRACE_EVERY_N_MS(ms, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::TRACE, (static_cast<std::int64_t>(ms) * 1000000LL), __VA_ARGS__)
#define LOG_DEBUG_EVERY_N_MS(ms, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::DEBUG, (static_cast<std::int64_t>(ms) * 1000000LL), __VA_ARGS__)
#define LOG_INFO_EVERY_N_MS(ms, ...)  ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::INFO,  (static_cast<std::int64_t>(ms) * 1000000LL), __VA_ARGS__)
#define LOG_WARN_EVERY_N_MS(ms, ...)  ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::WARN,  (static_cast<std::int64_t>(ms) * 1000000LL), __VA_ARGS__)
#define LOG_ERROR_EVERY_N_MS(ms, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::ERROR, (static_cast<std::int64_t>(ms) * 1000000LL), __VA_ARGS__)
#define LOG_FATAL_EVERY_N_MS(ms, ...) ELOG_INTERNAL_EVERY_TIME(::elog::default_logger(), ::elog::Level::FATAL, (static_cast<std::int64_t>(ms) * 1000000LL), __VA_ARGS__)
#define LOGGER_EVERY_N_MS(logger, lvl, ms, ...) ELOG_INTERNAL_EVERY_TIME(logger, lvl, (static_cast<std::int64_t>(ms) * 1000000LL), __VA_ARGS__)

// ---- LOG_*_FIRST_N(n, fmt, ...) ----
#define ELOG_INTERNAL_FIRST_N(logger_expr, lvl_value, n_val, fmt_str, ...)      \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        static ::elog::detail::FirstNState __elog_st;                           \
        if (!__elog_st.tick(static_cast<std::uint64_t>(n_val))) break;          \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_FIRST_N(n, ...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::TRACE, n, __VA_ARGS__)
#define LOG_DEBUG_FIRST_N(n, ...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::DEBUG, n, __VA_ARGS__)
#define LOG_INFO_FIRST_N(n, ...)  ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::INFO,  n, __VA_ARGS__)
#define LOG_WARN_FIRST_N(n, ...)  ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::WARN,  n, __VA_ARGS__)
#define LOG_ERROR_FIRST_N(n, ...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::ERROR, n, __VA_ARGS__)
#define LOG_FATAL_FIRST_N(n, ...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::FATAL, n, __VA_ARGS__)
#define LOGGER_FIRST_N(logger, lvl, n, ...) ELOG_INTERNAL_FIRST_N(logger, lvl, n, __VA_ARGS__)

// ---- LOG_*_ONCE(fmt, ...) (alias of FIRST_N(1)) ----
#define LOG_TRACE_ONCE(...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::TRACE, 1, __VA_ARGS__)
#define LOG_DEBUG_ONCE(...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::DEBUG, 1, __VA_ARGS__)
#define LOG_INFO_ONCE(...)  ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::INFO,  1, __VA_ARGS__)
#define LOG_WARN_ONCE(...)  ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::WARN,  1, __VA_ARGS__)
#define LOG_ERROR_ONCE(...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::ERROR, 1, __VA_ARGS__)
#define LOG_FATAL_ONCE(...) ELOG_INTERNAL_FIRST_N(::elog::default_logger(), ::elog::Level::FATAL, 1, __VA_ARGS__)
#define LOGGER_ONCE(logger, lvl, ...) ELOG_INTERNAL_FIRST_N(logger, lvl, 1, __VA_ARGS__)

// ---- LOG_*_SAMPLED(prob_percent, fmt, ...) ----
#define ELOG_INTERNAL_SAMPLED(logger_expr, lvl_value, prob_val, fmt_str, ...)   \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        if (!::elog::detail::SampledState::tick(                                \
                static_cast<std::uint32_t>(prob_val))) break;                   \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_SAMPLED(p, ...) ELOG_INTERNAL_SAMPLED(::elog::default_logger(), ::elog::Level::TRACE, p, __VA_ARGS__)
#define LOG_DEBUG_SAMPLED(p, ...) ELOG_INTERNAL_SAMPLED(::elog::default_logger(), ::elog::Level::DEBUG, p, __VA_ARGS__)
#define LOG_INFO_SAMPLED(p, ...)  ELOG_INTERNAL_SAMPLED(::elog::default_logger(), ::elog::Level::INFO,  p, __VA_ARGS__)
#define LOG_WARN_SAMPLED(p, ...)  ELOG_INTERNAL_SAMPLED(::elog::default_logger(), ::elog::Level::WARN,  p, __VA_ARGS__)
#define LOG_ERROR_SAMPLED(p, ...) ELOG_INTERNAL_SAMPLED(::elog::default_logger(), ::elog::Level::ERROR, p, __VA_ARGS__)
#define LOG_FATAL_SAMPLED(p, ...) ELOG_INTERNAL_SAMPLED(::elog::default_logger(), ::elog::Level::FATAL, p, __VA_ARGS__)
#define LOGGER_SAMPLED(logger, lvl, p, ...) ELOG_INTERNAL_SAMPLED(logger, lvl, p, __VA_ARGS__)

// ---- LOG_*_BURST(burst, refill_per_sec, fmt, ...) ----
#define ELOG_INTERNAL_BURST(logger_expr, lvl_value, burst_val, refill_val, fmt_str, ...) \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        static ::elog::detail::BurstState __elog_st;                            \
        if (!__elog_st.tick(static_cast<std::uint32_t>(burst_val),              \
                            static_cast<double>(refill_val))) break;            \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_BURST(b, r, ...) ELOG_INTERNAL_BURST(::elog::default_logger(), ::elog::Level::TRACE, b, r, __VA_ARGS__)
#define LOG_DEBUG_BURST(b, r, ...) ELOG_INTERNAL_BURST(::elog::default_logger(), ::elog::Level::DEBUG, b, r, __VA_ARGS__)
#define LOG_INFO_BURST(b, r, ...)  ELOG_INTERNAL_BURST(::elog::default_logger(), ::elog::Level::INFO,  b, r, __VA_ARGS__)
#define LOG_WARN_BURST(b, r, ...)  ELOG_INTERNAL_BURST(::elog::default_logger(), ::elog::Level::WARN,  b, r, __VA_ARGS__)
#define LOG_ERROR_BURST(b, r, ...) ELOG_INTERNAL_BURST(::elog::default_logger(), ::elog::Level::ERROR, b, r, __VA_ARGS__)
#define LOG_FATAL_BURST(b, r, ...) ELOG_INTERNAL_BURST(::elog::default_logger(), ::elog::Level::FATAL, b, r, __VA_ARGS__)
#define LOGGER_BURST(logger, lvl, b, r, ...) ELOG_INTERNAL_BURST(logger, lvl, b, r, __VA_ARGS__)

// ---- LOG_*_DEDUP(fmt, ...) ----
// Suppresses consecutive same-call-site emits. The first emit at site A
// passes through; subsequent A-calls bump A.suppressed; when a different
// site B is hit, A's suppressed-summary is flushed before B's emit.
#define ELOG_INTERNAL_DEDUP(logger_expr, lvl_value, fmt_str, ...)               \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        static ::elog::detail::DedupState __elog_st;                            \
        if (!__elog_st.tick(__elog_l, lvl_value, __FILE__, __LINE__)) break;    \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_DEDUP(...) ELOG_INTERNAL_DEDUP(::elog::default_logger(), ::elog::Level::TRACE, __VA_ARGS__)
#define LOG_DEBUG_DEDUP(...) ELOG_INTERNAL_DEDUP(::elog::default_logger(), ::elog::Level::DEBUG, __VA_ARGS__)
#define LOG_INFO_DEDUP(...)  ELOG_INTERNAL_DEDUP(::elog::default_logger(), ::elog::Level::INFO,  __VA_ARGS__)
#define LOG_WARN_DEDUP(...)  ELOG_INTERNAL_DEDUP(::elog::default_logger(), ::elog::Level::WARN,  __VA_ARGS__)
#define LOG_ERROR_DEDUP(...) ELOG_INTERNAL_DEDUP(::elog::default_logger(), ::elog::Level::ERROR, __VA_ARGS__)
#define LOG_FATAL_DEDUP(...) ELOG_INTERNAL_DEDUP(::elog::default_logger(), ::elog::Level::FATAL, __VA_ARGS__)
#define LOGGER_DEDUP(logger, lvl, ...) ELOG_INTERNAL_DEDUP(logger, lvl, __VA_ARGS__)

// ---- LOG_*_IF(cond, fmt, ...) ----
#define ELOG_INTERNAL_IF(logger_expr, lvl_value, cond_expr, fmt_str, ...)       \
    do {                                                                        \
        auto& __elog_l = (logger_expr);                                         \
        if (!__elog_l.enabled(lvl_value)) break;                                \
        if (!(cond_expr)) break;                                                \
        static constexpr auto __elog_spec =                                     \
            ::elog::make_spec<                                                  \
                ::elog::count_pieces(fmt_str),                                  \
                ::elog::count_lit_chars(fmt_str)                                \
            >(fmt_str);                                                         \
        ::elog::detail::emit_f(__elog_l, lvl_value, __FILE__, __LINE__,         \
                               __elog_spec, ##__VA_ARGS__);                     \
    } while (0)

#define LOG_TRACE_IF(c, ...) ELOG_INTERNAL_IF(::elog::default_logger(), ::elog::Level::TRACE, c, __VA_ARGS__)
#define LOG_DEBUG_IF(c, ...) ELOG_INTERNAL_IF(::elog::default_logger(), ::elog::Level::DEBUG, c, __VA_ARGS__)
#define LOG_INFO_IF(c, ...)  ELOG_INTERNAL_IF(::elog::default_logger(), ::elog::Level::INFO,  c, __VA_ARGS__)
#define LOG_WARN_IF(c, ...)  ELOG_INTERNAL_IF(::elog::default_logger(), ::elog::Level::WARN,  c, __VA_ARGS__)
#define LOG_ERROR_IF(c, ...) ELOG_INTERNAL_IF(::elog::default_logger(), ::elog::Level::ERROR, c, __VA_ARGS__)
#define LOG_FATAL_IF(c, ...) ELOG_INTERNAL_IF(::elog::default_logger(), ::elog::Level::FATAL, c, __VA_ARGS__)
#define LOGGER_IF(logger, lvl, c, ...) ELOG_INTERNAL_IF(logger, lvl, c, __VA_ARGS__)
